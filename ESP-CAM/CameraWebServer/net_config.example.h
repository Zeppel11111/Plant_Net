#ifndef PLANT_NET_CONFIG_H
#define PLANT_NET_CONFIG_H

// 复制为 net_config.h。按优先级填写已知热点及其对应的 Flask 电脑地址。
// net_config.h 含密码，已在工作区根目录的 .gitignore 中排除。
#define PN_WIFI_NETWORKS \
    { "YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD", "192.168.137.1" }, \
    { "SECOND_WIFI_SSID", "SECOND_WIFI_PASSWORD", "192.168.1.100" }

#define PN_SERVER_HOST "192.168.137.1"
#define PN_PUSH_PORT 9001

#endif
