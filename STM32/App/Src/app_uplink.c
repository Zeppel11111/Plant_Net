#include    <stdio.h>
#include    <string.h>
#include    "main.h"           /* HAL_GetTick */
#include    "FreeRTOS.h"
#include    "task.h"
#include    "cmsis_os.h"
#include    "app_config.h"
#include    "app_msg.h"
#include    "app_uplink.h"
#include    "ESP8266.h"
#include    "net_config.h"     /* PN_SERVER_HOST / PN_DATA_PORT（统一网络配置） */
#include    "Debug.h"

/* =====================================================================
 * 上行任务：只负责"把队列里的采样发出去"，不关心采集节奏
 *   · 启动：ESP8266 探活(最多 3 次) → 自动连热点 → 建 TCP
 *   · 循环：等队列（超时用于周期性重连检查）→ 组行 → 发送
 *   · 断线：发送失败置断，冷却后重连；采集任务完全不受影响
 * ===================================================================== */
static const char* TAG = "app_uplink";

#define     UPLINK_WAIT_MS            2000u   /* 队列等待上限：兼作重连检查周期 */
#define     UPLINK_RETRY_COOLDOWN_MS  5000u   /* 重连冷却，避免疯狂重试 */

static int      s_tcp_ok      = 0;
static uint32_t s_last_retry  = 0;

/* 探活 + 连热点（阻塞式，只在启动做一次；失败仅打印，稍后由重连逻辑兜底） */
static void app_uplink_join(void)
{
    int esp_ok = (esp8266_init(BSP_UART3) == 0);
    for (int i = 0; !esp_ok && i < 3; i++)
    {
        osDelay(1000);
        esp_ok = (esp8266_at_test() == 0);
        if (esp_ok) { LOG_I(TAG, "AT 握手成功（引导重试后）"); }
    }
    if (!esp_ok)
    {
        LOG_E(TAG, "模块持续无响应（查 TX/RX 交叉、CH_PD 拉高、供电）");
        return;
    }

    char ip[32] = "?";
    if (esp8266_auto_join() != 0)
    {
        LOG_E(TAG, "热点连接失败（查 net_config.h 的 SSID/密码/热点范围）");
    }
    else if (esp8266_get_ip(ip, sizeof(ip)) != 0)
    {
        LOG_I(TAG, "WiFi 已连接（IP 查询失败）");
    }
    else
    {
        LOG_I(TAG, "WiFi 已连接 IP=%s", ip);
    }
}

/* 连接服务器：Windows 热点模式下默认网关就是 Flask 电脑；
 * 普通路由器模式下若网关没有服务，则回退到 PN_SERVER_HOST。 */
static int app_uplink_connect_server(void)
{
    char gateway[32] = {0};
    int  gateway_ok;

    gateway_ok = (esp8266_get_gateway(gateway, sizeof(gateway)) == 0);
    if (!gateway_ok)
    {
        /* 查询不到网关通常表示 STA 已离线。复用现有入网流程恢复 WiFi，
         * 成功后必须重新读取 DHCP 网关，不能沿用旧地址。 */
        LOG_I(TAG, "未取得默认网关，重新加入热点");
        if (esp8266_auto_join() == 0)
        {
            gateway_ok = (esp8266_get_gateway(gateway, sizeof(gateway)) == 0);
        }
    }

    if (gateway_ok)
    {
        LOG_I(TAG, "默认网关=%s", gateway);
        if (esp8266_tcp_connect(gateway, PN_DATA_PORT) == 0)
        {
            LOG_I(TAG, "TCP 已连接 %s:%d（热点网关）", gateway, PN_DATA_PORT);
            return 1;
        }
        LOG_E(TAG, "热点网关连接失败 %s:%d", gateway, PN_DATA_PORT);
    }

    /* 网关与兜底地址相同时不重复尝试，避免额外等待一次连接超时。 */
    if (!gateway_ok || strcmp(gateway, PN_SERVER_HOST) != 0)
    {
        if (esp8266_tcp_connect(PN_SERVER_HOST, PN_DATA_PORT) == 0)
        {
            LOG_I(TAG, "TCP 已连接 %s:%d（配置兜底）",
                  PN_SERVER_HOST, PN_DATA_PORT);
            return 1;
        }
        LOG_E(TAG, "配置兜底连接失败 %s:%d", PN_SERVER_HOST, PN_DATA_PORT);
    }

    return 0;
}

/* 确保 TCP 在线：不在线则按冷却重连。返回 1=可发送 */
static int app_uplink_ensure(void)
{
    if (s_tcp_ok) { return 1; }

    uint32_t now = HAL_GetTick();
    if (now - s_last_retry < UPLINK_RETRY_COOLDOWN_MS) { return 0; }
    s_last_retry = now;

    s_tcp_ok = app_uplink_connect_server();
    if (!s_tcp_ok)
    {
        LOG_E(TAG, "服务器连接失败，%u ms 后重试",
              (unsigned)UPLINK_RETRY_COOLDOWN_MS);
    }
    return s_tcp_ok;
}

void AppUplink_Task(void *argument)
{
    osMessageQueueId_t q = (osMessageQueueId_t)argument;
    sensor_msg_t m;

    app_uplink_join();
    (void)app_uplink_ensure();

    for (;;)
    {
        /* 等采样；超时也走一圈，用于周期检查/恢复链路 */
        if (osMessageQueueGet(q, &m, NULL, pdMS_TO_TICKS(UPLINK_WAIT_MS)) != osOK)
        {
            (void)app_uplink_ensure();
            continue;
        }

        if (!app_uplink_ensure()) { continue; }   /* 断开：本次采样丢弃，下轮再发 */

        char line[32];
        int  n = snprintf(line, sizeof(line), "%d,%d,%d\n",
                          (int)m.temp_x10, (int)m.humi_x10, (int)m.lux);

        if (esp8266_send_data((uint8_t*)line, (uint16_t)n) != 0)
        {
            s_tcp_ok = 0;                        /* 断链：置位，下轮重连 */
            LOG_E(TAG, "发送失败 seq=%u，准备重连", (unsigned)m.seq);
        }
        else
        {
            LOG_D(TAG, "已发送 seq=%u", (unsigned)m.seq);
        }
    }
}
