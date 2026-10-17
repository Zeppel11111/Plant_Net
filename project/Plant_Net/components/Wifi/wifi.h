#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"

/**
 * @brief 初始化 Wi-Fi STA 并启动后台扫描、连接和断线切换任务。
 *
 * @return ESP_OK 表示管理任务已启动；获取 IP 将在后台完成。
 */
esp_err_t wifi_init_sta(void);

#endif
