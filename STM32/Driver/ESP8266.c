#include    <stdint.h>
#include    <string.h>
#include    <stdio.h>
#include    "ESP8266.h"
#include    "ESP8266_AT_Parse.h"
#include    "bsp_uart.h"
#include    "Debug.h"
#include    "../Core/Inc/net_config.h"   /* 统一网络配置（config/net_config.h 的副本） */

/* =====================================================================
 * 模块总开关：CFG_ENABLE_ESP8266 = 0 时整个模块不编译，
 * 调用点靠 ESP8266.h 里的裁剪桩编译通过。
 * 分层约束：本文件只允许调用 BSP 接口（bsp_uart_*），
 * 不直接触碰 HAL 句柄——USART3 波特率 115200 在 CubeMX(.ioc) 配置。
 * ===================================================================== */
#if CFG_ENABLE_ESP8266

static const char* TAG = "ESP8266";

/* ---- 器件状态：记录挂在哪路串口 ---- */
static bsp_uartx_t s_uartx = BSP_UART_COUNT;   /* 哨兵值：esp8266_init 前表示未指定 */

/* 接收分片粒度：等一行响应最多阻塞 10ms，不卡任务调度（与 BSP 切片一致） */
#define   ESP8266_POLL_SLICE_MS   10
/* 拼指令的缓冲上限：AT+CWJAP 或 AT+CIPSTART 的整条指令长度 */
#define   ESP8266_CMD_MAX         128

/* ===== WiFi 凭据表：SSID/密码来自统一配置 net_config.h（PN_WIFI_SSID/PN_WIFI_PASS）。
 * 注意：ssid 区分大小写，必须与热点广播名完全一致；{NULL,NULL} 是表尾哨兵 */
static const ssid_pwd_t ssid_pwd[] = {
    {PN_WIFI_SSID, PN_WIFI_PASS},
    {NULL, NULL}
};

int esp8266_init(bsp_uartx_t uartx)
{
    s_uartx = uartx;

    if (bsp_uart_init(s_uartx) != 0)
    {
        LOG_E(TAG, "串口初始化失败");
        return -1;
    }
    LOG_I(TAG, "USART3 已初始化（115200 8N1）");

    /* AT 自检：模块必须回 OK，否则查 TX/RX 交叉、CH_PD 拉高、供电 */
    if (esp8266_at_test() != 0)
    {
        LOG_E(TAG, "AT 无响应（检查 TX/RX 交叉、CH_PD/RST 拉高、供电）");
        return -2;
    }
    LOG_I(TAG, "AT 握手成功，模块在线");
    return 0;
}

int esp8266_at_test(void)
{
    return esp8266_cmd("AT", "OK", 1000);
}

int esp8266_cmd(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    char     line[ESP8266_RX_LINE_MAX];
    uint32_t waited = 0;

    if (cmd == NULL || expect == NULL)      return -1;   /* 参数错 */
    if (s_uartx >= BSP_UART_COUNT)          return -2;   /* 未初始化 */

    /* 发指令：cmd + \r\n（AT 指令必须回车结尾，由驱动统一补） */
    if (bsp_uart_send(s_uartx, (uint8_t*)cmd, strlen(cmd), 100,
                      BSP_UART_MODE_POLLING) != 0 ||
        bsp_uart_send(s_uartx, (uint8_t*)"\r\n", 2, 100,
                      BSP_UART_MODE_POLLING) != 0)
    {
        return -3;                                        /* 发送失败 */
    }

    /* 逐行收响应，在行里找关键字；分片等待保证 RTOS 友好 */
    while (waited < timeout_ms)
    {
        if (bsp_uart_rec_line(s_uartx, (uint8_t*)line, sizeof(line),
                              ESP8266_POLL_SLICE_MS, BSP_UART_MODE_POLLING) == 0)
        {
            if (strstr(line, expect) != NULL)  return 0;      /* 等到预期关键字 */
            if (strstr(line, "ERROR") != NULL ||
                strstr(line, "FAIL")  != NULL) return -4;     /* 模块明确报错，提前返回 */
        }
        waited += ESP8266_POLL_SLICE_MS;   /* 没等到：继续收下一行，直到预算耗尽 */
    }
    return -5;                             /* 超时 */
}
/* 内部：CWLAP 扫描附近热点，找表格里第一个在范围内的；
 * 返回表格下标，-1 无匹配，-2/-3 未初始化/发送失败，-4 扫描超时 */
static int esp8266_cwlap_match(void)
{
    char     line[ESP8266_RX_LINE_MAX];
    uint32_t waited = 0;
    int      i;

    if (s_uartx >= BSP_UART_COUNT)  return -2;

    if (bsp_uart_send(s_uartx, (uint8_t*)"AT+CWLAP", 8, 100,
                      BSP_UART_MODE_POLLING) != 0 ||
        bsp_uart_send(s_uartx, (uint8_t*)"\r\n", 2, 100,
                      BSP_UART_MODE_POLLING) != 0)
    {
        return -3;
    }

    while (waited < 15000)   /* 扫描最多 15s */
    {
        if (bsp_uart_rec_line(s_uartx, (uint8_t*)line, sizeof(line),
                              ESP8266_POLL_SLICE_MS, BSP_UART_MODE_POLLING) == 0)
        {
            if (strstr(line, "+CWLAP:") != NULL)
            {
                /* 帧形如 +CWLAP:(4,"Zeppelin",-45,"mac",1)，取第一个引号里的 SSID */
                const char *q = strchr(line, '"');
                if (q == NULL)  continue;
                q++;   /* 指向 SSID 首字符 */

                for (i = 0; ssid_pwd[i].ssid != NULL; i++)
                {
                    size_t len = strlen(ssid_pwd[i].ssid);
                    /* SSID 精确匹配到收尾引号为止 */
                    if (strncmp(q, ssid_pwd[i].ssid, len) == 0 && q[len] == '"')
                    {
                        return i;   /* 命中表格第 i 项 */
                    }
                }
            }
            else if (strstr(line, "OK") != NULL)
            {
                return -1;   /* 扫描完成，表格里的都不在范围 */
            }
        }
        waited += ESP8266_POLL_SLICE_MS;
    }
    return -4;   /* 扫描超时 */
}

/* 模式② 自动连：先确保 STA 模式 → 扫描 → 自动连表格里第一个在范围内的热点 */
int esp8266_auto_join(void)
{
    int r, idx;

    r = esp8266_cmd("AT+CWMODE=1", "OK", 2000);   /* 防止停在 AP 模式导致扫描报错 */
    if (r != 0)
    {
        return r;
    }

    idx = esp8266_cwlap_match();
    if (idx < 0)
    {
        return idx;   /* 无匹配或扫描失败 */
    }
    return esp8266_join_ap(ssid_pwd[idx].ssid, ssid_pwd[idx].pwd);
}

/* 连 WiFi（STA 模式）：凭据由调用方传入（模式① 手动连）。
 * 返回值为 esp8266_cmd 语义：0 成功 / -4 失败(密码热点错) / -5 超时 */
int esp8266_join_ap(const char *ssid, const char *pwd)
{
    char cmd[ESP8266_CMD_MAX];
    int  r;

    if (ssid == NULL || pwd == NULL)    return -1;   /* 参数错 */

    /* 先确保 STA 模式（重复设置无害）；失败原样上报 */
    r = esp8266_cmd("AT+CWMODE=1", "OK", 2000);
    if (r != 0)
    {
        return r;
    }

    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
    /* 连热点可能经历 WIFI CONNECTED 阶段，给足 15s */
    return esp8266_cmd(cmd, "OK", 15000);
}

/* 连 TCP 服务器（单连接模式）：AT+CIPSTART="TCP","ip",port
 * 服务器没开时模块回 CONNECT FAIL（含 FAIL → -4） */
int esp8266_tcp_connect(const char *ip, uint16_t port)
{
    char cmd[ESP8266_CMD_MAX];

    if (ip == NULL || port == 0)        return -1;   /* 参数错 */

    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u",
             ip, (unsigned)port);
    return esp8266_cmd(cmd, "OK", 10000);
}

/* 查询本机 STA IP：解析 +CIFSR:STAIP,"x.x.x.x" 里的地址，并读取到
 * 本次响应的最终 OK，避免把残留 OK 留给下一条 AT 指令。 */
int esp8266_get_ip(char *ip_buf, uint16_t buf_len)
{
    char     line[ESP8266_RX_LINE_MAX];
    char     candidate[32];
    uint32_t waited = 0;
    const char *q1, *q2;
    uint16_t len;
    int      found = 0;

    if (ip_buf == NULL || buf_len == 0) return -1;
    if (s_uartx >= BSP_UART_COUNT)      return -2;

    if (bsp_uart_send(s_uartx, (uint8_t*)"AT+CIFSR", strlen("AT+CIFSR"), 100,
                      BSP_UART_MODE_POLLING) != 0 ||
        bsp_uart_send(s_uartx, (uint8_t*)"\r\n", 2, 100,
                      BSP_UART_MODE_POLLING) != 0)
    {
        return -3;
    }

    while (waited < 5000)
    {
        if (bsp_uart_rec_line(s_uartx, (uint8_t*)line, sizeof(line),
                              ESP8266_POLL_SLICE_MS, BSP_UART_MODE_POLLING) == 0)
        {
            if (strstr(line, "+CIFSR:STAIP") != NULL)
            {
                /* +CIFSR:STAIP,"172.20.10.11" → 取两引号之间的部分 */
                q1 = strchr(line, '"');
                if (q1 == NULL)  continue;
                q1++;
                q2 = strchr(q1, '"');
                len = (q2 != NULL) ? (uint16_t)(q2 - q1)
                                   : (uint16_t)strlen(q1);
                if (len >= sizeof(candidate))  len = sizeof(candidate) - 1;
                memcpy(candidate, q1, len);
                candidate[len] = '\0';
                found = 1;
            }
            else if (strcmp(line, "OK") == 0)
            {
                if (!found)  return -5;
                len = (uint16_t)strlen(candidate);
                if (len >= buf_len)  len = buf_len - 1;   /* 保持既有截断成功语义 */
                memcpy(ip_buf, candidate, len);
                ip_buf[len] = '\0';
                return 0;
            }
            else if (strstr(line, "ERROR") != NULL ||
                strstr(line, "FAIL")  != NULL) return -4;
        }
        waited += ESP8266_POLL_SLICE_MS;
    }
    return -5;
}

/* 查询 STA 默认网关：候选值只写局部缓冲，并等到最终 OK 后再提交给调用者。 */
int esp8266_get_gateway(char *gateway_buf, uint16_t buf_len)
{
    char     line[ESP8266_RX_LINE_MAX];
    char     candidate[32];
    uint32_t waited = 0;
    size_t   len;
    int      found = 0;
    int      parse_error = 0;
    int      parse_result;

    if (gateway_buf == NULL || buf_len == 0) return -1;
    if (s_uartx >= BSP_UART_COUNT)            return -2;

    if (bsp_uart_send(s_uartx, (uint8_t*)"AT+CIPSTA?", strlen("AT+CIPSTA?"), 100,
                      BSP_UART_MODE_POLLING) != 0 ||
        bsp_uart_send(s_uartx, (uint8_t*)"\r\n", 2, 100,
                      BSP_UART_MODE_POLLING) != 0)
    {
        return -3;
    }

    while (waited < 5000)
    {
        if (bsp_uart_rec_line(s_uartx, (uint8_t*)line, sizeof(line),
                              ESP8266_POLL_SLICE_MS, BSP_UART_MODE_POLLING) == 0)
        {
            parse_result = esp8266_at_parse_gateway(line, candidate,
                                                     (uint16_t)sizeof(candidate));
            if (parse_result == 0)
            {
                found = 1;
            }
            else if (parse_result == -2 || parse_result == -3)
            {
                parse_error = 1;
            }

            if (strcmp(line, "OK") == 0)
            {
                if (parse_error)                         return -6;
                if (!found || strcmp(candidate, "0.0.0.0") == 0) return -5;

                len = strlen(candidate);
                if (len >= (size_t)buf_len)              return -6;
                memcpy(gateway_buf, candidate, len + 1U);
                return 0;
            }
            if (strstr(line, "ERROR") != NULL ||
                strstr(line, "FAIL")  != NULL)           return -4;
        }
        waited += ESP8266_POLL_SLICE_MS;
    }
    return -5;
}

/* 发送一帧数据（单连接）：AT+CIPSEND=<len> → 等 '>' → 填数 → 等 SEND OK。
 * '>' 提示符不带换行，不能用"行"等待，改为字节级逐个收找 '>' */
int esp8266_send_data(const uint8_t *data, uint16_t len)
{
    char     cmd[ESP8266_CMD_MAX];
    char     line[ESP8266_RX_LINE_MAX];
    uint8_t  c;
    uint32_t waited = 0;

    if (data == NULL || len == 0)   return -1;
    if (s_uartx >= BSP_UART_COUNT)  return -2;

    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", (unsigned)len);
    if (bsp_uart_send(s_uartx, (uint8_t*)cmd, strlen(cmd), 100,
                      BSP_UART_MODE_POLLING) != 0 ||
        bsp_uart_send(s_uartx, (uint8_t*)"\r\n", 2, 100,
                      BSP_UART_MODE_POLLING) != 0)
    {
        return -3;
    }

    /* 阶段1：等 '>' 提示符（可能先收到残留行如 busy p...，逐字节丢弃直到 '>'） */
    while (waited < 3000)
    {
        if (bsp_uart_rec_frame(s_uartx, &c, 1, ESP8266_POLL_SLICE_MS, 10) == 1)
        {
            if (c == '>')
            {
                break;
            }
        }
        waited += ESP8266_POLL_SLICE_MS;
    }
    if (waited >= 3000)
    {
        return -5;   /* 没等到 '>'：连接多半已断开 */
    }

    /* 阶段2：填入 len 字节载荷（原样发送，不附加任何字符） */
    if (bsp_uart_send(s_uartx, (uint8_t*)data, len, 100,
                      BSP_UART_MODE_POLLING) != 0)
    {
        return -3;
    }

    /* 阶段3：等 SEND OK / ERROR，确认发送完成 */
    waited = 0;
    while (waited < 3000)
    {
        if (bsp_uart_rec_line(s_uartx, (uint8_t*)line, sizeof(line),
                              ESP8266_POLL_SLICE_MS, BSP_UART_MODE_POLLING) == 0)
        {
            if (strstr(line, "SEND OK") != NULL)   return 0;
            if (strstr(line, "ERROR") != NULL ||
                strstr(line, "FAIL")  != NULL)     return -4;
        }
        waited += ESP8266_POLL_SLICE_MS;
    }
    return -5;
}

#endif /* CFG_ENABLE_ESP8266 */
