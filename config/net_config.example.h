#ifndef PLANT_NET_CONFIG_H
#define PLANT_NET_CONFIG_H

/* ================================================================
 * Plant_Net 公开配置模板
 * ----------------------------------------------------------------
 * 首次运行 `python tools/deploy_config.py --sync` 会将本文件复制为
 * config/net_config.h，并同步至 STM32 与 ESP-CAM 的编译目录。
 * 请只在本地生成的 net_config.h 中填写真实 Wi-Fi 凭据；该文件不会提交。
 * ================================================================ */

/* ---- WiFi：ESP8266 与 ESP-CAM 共用（必须连同一热点） ---- */
#define PN_WIFI_SSID      "YOUR_WIFI_SSID"
#define PN_WIFI_PASS      "YOUR_WIFI_PASSWORD"

/* ---- 服务器地址兜底 ----
 * Windows 移动热点模式下，固件优先读取 DHCP 默认网关作为服务器地址。
 * 普通路由器或公网模式可填写 Flask 电脑局域网 IP / TCP 隧道域名。
 * ------------------------------------------------------------ */
#define PN_SERVER_HOST    "192.168.137.1"

/* ---- 端口（一般不动） ---- */
#define PN_DATA_PORT      9000
#define PN_PUSH_PORT      9001
#define PN_HTTP_PORT      8080

#endif /* PLANT_NET_CONFIG_H */
