#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

// 复制本文件为 wifi_config.h，再按优先级填写热点；排在前面的优先连接。
// wifi_config.h 含本机凭据，已在工作区根目录的 .gitignore 中排除。
#define WIFI_CONFIG_NETWORKS \
    { "YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD" }, \
    { "SECOND_WIFI_SSID", "SECOND_WIFI_PASSWORD" }

#endif
