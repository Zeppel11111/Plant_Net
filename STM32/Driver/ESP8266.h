#ifndef     ESP8266_H
#define     ESP8266_H

#include    <stdint.h>
#include    "bsp_uart.h"
#include    "app_config.h"     /* 模块总开关：必须先于本文件包含 */

/* =====================================================================
 * ESP8266（ESP-01S）WiFi 模块驱动 —— AT 指令固件
 *
 * 器件卡插槽：CFG_ENABLE_ESP8266 = 1 编译真驱动，= 0 时空桩。
 * 接线：USART3 TX(PB10)→模块 RXD，RX(PB11)←模块 TXD；
 *       VCC=3.3V、CH_PD/RST 拉高、GPIO0 悬空。
 * 波特率：115200 8N1（AT 固件默认，已在 CubeMX(.ioc) 配好 USART3）
 * 固件：安信可 v1.5.4.1-a（老 AT 固件，无 MQTT 指令 → 上云走 TCP）
 * 分层约束：驱动只调 BSP 接口，不触碰 HAL 句柄（无穿透）
 * ===================================================================== */

#define   ESP8266_RX_LINE_MAX   128    /* 单行 AT 响应最大长度（含 \0） */

#if CFG_ENABLE_ESP8266

/* WiFi 凭据表结构：在 ESP8266.c 的 ssid_pwd[] 里填写常用热点，
 * esp8266_auto_join 会扫描附近热点并自动连上第一个匹配的 */
typedef struct
{
    const char *ssid;
    const char *pwd;
} ssid_pwd_t;

/* 初始化：认领串口 + AT 自检（AT→OK）；成功返回 0，-1 串口失败，-2 AT 自检失败 */
int  esp8266_init(bsp_uartx_t uartx);

/* 模块探活：AT → OK；成功返回 0 */
int  esp8266_at_test(void);

/* 核心原语：发一条 AT 指令（自动补 \r\n），等响应行里出现 expect 关键字。
 * 成功返回 0；失败为负：
 *   -1 参数错    -2 未初始化    -3 发送失败
 *   -4 模块明确回 ERROR/FAIL    -5 超时未等到 expect      */
int  esp8266_cmd(const char *cmd, const char *expect, uint32_t timeout_ms);

/* 模式① 手动连指定 WiFi（STA 模式）：先 AT+CWMODE=1 切 STA，再 AT+CWJAP。
 * 成功 0；负值含义同 esp8266_cmd（-4 常见=密码/热点名错，-5 超时） */
int  esp8266_join_ap(const char *ssid, const char *pwd);

/* 模式② 自动连：AT+CWLAP 扫描附近热点，自动连上 ssid_pwd[] 表格里
 * 第一个在范围内的（按表格顺序）。成功 0；-1 无匹配（都不在范围），
 * 其余负值含义同 esp8266_cmd */
int  esp8266_auto_join(void);

/* 连 TCP 服务器（单连接模式）：AT+CIPSTART="TCP",ip,port。
 * ip 传 IP 或域名均可；成功 0；负值含义同 esp8266_cmd（-4 常见=服务器没开） */
int  esp8266_tcp_connect(const char *ip, uint16_t port);

/* 查询本机 STA IP（AT+CIFSR）：IP 字符串（不含引号）写入 ip_buf。
 * 成功 0；-1 参数错 -2 未初始化 -3 发送失败 -4 模块报错 -5 超时 */
int  esp8266_get_ip(char *ip_buf, uint16_t buf_len);

/* 查询 STA 默认网关（AT+CIPSTA?）：读取完整响应直到最终 OK，网关字符串
 * 写入 gateway_buf。成功 0；-1 参数错 -2 未初始化 -3 发送失败
 * -4 模块报 ERROR/FAIL -5 超时/无网关/网关为 0.0.0.0
 * -6 网关字段格式错误或调用者缓冲不足。失败时不修改 gateway_buf。 */
int  esp8266_get_gateway(char *gateway_buf, uint16_t buf_len);

/* 发送一帧数据（单连接模式）：AT+CIPSEND=<len> → 等 '>'（字节级）→
 * 填 len 字节 → 等 SEND OK。成功 0；-1 参数错 -2 未初始化 -3 发送失败
 * -4 模块报错 -5 超时（无 '>' 或 SEND OK，常见=连接已断开） */
int  esp8266_send_data(const uint8_t *data, uint16_t len);

#else
/* ===== 裁剪桩：开关=0 时模块不编译，调用点无需 #if，直接编成失败 ===== */
static inline int esp8266_init(bsp_uartx_t uartx)                     { (void)uartx; return -1; }
static inline int esp8266_at_test(void)                               { return -1; }
static inline int esp8266_cmd(const char *cmd, const char *expect,
                              uint32_t timeout_ms)                    { (void)cmd; (void)expect; (void)timeout_ms; return -1; }
static inline int esp8266_join_ap(const char *ssid, const char *pwd)  { (void)ssid; (void)pwd; return -1; }
static inline int esp8266_auto_join(void)                             { return -1; }
static inline int esp8266_tcp_connect(const char *ip, uint16_t port)  { (void)ip; (void)port; return -1; }
static inline int esp8266_get_ip(char *ip_buf, uint16_t buf_len)      { (void)ip_buf; (void)buf_len; return -1; }
static inline int esp8266_get_gateway(char *gateway_buf, uint16_t buf_len)
                                                                    { (void)gateway_buf; (void)buf_len; return -1; }
static inline int esp8266_send_data(const uint8_t *data, uint16_t len){ (void)data; (void)len; return -1; }
#endif /* CFG_ENABLE_ESP8266 */

#endif
