#include "wifi.h"
#include "wifi_config.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_ATTEMPT_FAILED_BIT BIT1
#define WIFI_CONNECT_TIMEOUT_MS 12000
#define WIFI_RESCAN_INTERVAL_MS 5000

typedef struct {
    const char *ssid;
    const char *password;
} wifi_known_network_t;

// 列表顺序就是连接优先级；账号只放在本机的 wifi_config.h 中。
static const wifi_known_network_t s_known_networks[] = { WIFI_CONFIG_NETWORKS };
static const char *TAG = "Wifi";
static EventGroupHandle_t s_wifi_event_group;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_ATTEMPT_FAILED_BIT);
        ESP_LOGW(TAG, "热点断开，原因码：%u", (unsigned)event->reason);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "已获取 IP：" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupClearBits(s_wifi_event_group, WIFI_ATTEMPT_FAILED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_network_visible(const char *ssid, const wifi_ap_record_t *records,
                                 uint16_t count)
{
    for (uint16_t i = 0; i < count; ++i) {
        if (strcmp(ssid, (const char *)records[i].ssid) == 0) {
            return true;
        }
    }
    return false;
}

static bool wifi_try_network(const wifi_known_network_t *network)
{
    size_t ssid_len = strlen(network->ssid);
    size_t password_len = strlen(network->password);
    wifi_config_t config = {0};
    if (ssid_len == 0 || ssid_len >= sizeof(config.sta.ssid) ||
        password_len >= sizeof(config.sta.password)) {
        ESP_LOGE(TAG, "热点配置长度无效：%s", network->ssid);
        return false;
    }

    memcpy(config.sta.ssid, network->ssid, ssid_len);
    memcpy(config.sta.password, network->password, password_len);
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    xEventGroupClearBits(s_wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_ATTEMPT_FAILED_BIT);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "尝试连接已知热点：%s", network->ssid);
        err = esp_wifi_connect();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "连接请求失败：%s，%s", network->ssid, esp_err_to_name(err));
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_ATTEMPT_FAILED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "当前热点：%s", network->ssid);
        return true;
    }

    ESP_LOGW(TAG, "热点连接失败或超时：%s", network->ssid);
    // 只有等待超时才主动断开；失败事件已表明当前连接结束。
    if ((bits & WIFI_ATTEMPT_FAILED_BIT) == 0) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_ATTEMPT_FAILED_BIT);
        if (esp_wifi_disconnect() == ESP_OK) {
            xEventGroupWaitBits(s_wifi_event_group, WIFI_ATTEMPT_FAILED_BIT,
                                pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
        }
    }
    return false;
}

static void wifi_manager_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint16_t count = 0;
        esp_err_t err = esp_wifi_scan_start(NULL, true);
        if (err == ESP_OK) {
            err = esp_wifi_scan_get_ap_num(&count);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "扫描热点失败：%s", esp_err_to_name(err));
            esp_wifi_clear_ap_list();
            vTaskDelay(pdMS_TO_TICKS(WIFI_RESCAN_INTERVAL_MS));
            continue;
        }

        wifi_ap_record_t *records = count ? calloc(count, sizeof(*records)) : NULL;
        if (count && records == NULL) {
            ESP_LOGE(TAG, "扫描结果内存不足");
            esp_wifi_clear_ap_list();
            vTaskDelay(pdMS_TO_TICKS(WIFI_RESCAN_INTERVAL_MS));
            continue;
        }
        if (count) {
            err = esp_wifi_scan_get_ap_records(&count, records);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "读取扫描结果失败：%s", esp_err_to_name(err));
            esp_wifi_clear_ap_list();
            free(records);
            vTaskDelay(pdMS_TO_TICKS(WIFI_RESCAN_INTERVAL_MS));
            continue;
        }
        if (count == 0) esp_wifi_clear_ap_list();

        ESP_LOGI(TAG, "扫描到 %u 个热点", (unsigned)count);
        bool connected = false;
        for (size_t i = 0; i < sizeof(s_known_networks) / sizeof(s_known_networks[0]); ++i) {
            const wifi_known_network_t *network = &s_known_networks[i];
            if (!wifi_network_visible(network->ssid, records, count)) {
                continue;
            }
            if (wifi_try_network(network)) {
                connected = true;
                break;
            }
        }
        free(records);

        if (connected) {
            // 网络正常时睡眠；断线事件唤醒任务，再扫描可用热点。
            xEventGroupWaitBits(s_wifi_event_group, WIFI_ATTEMPT_FAILED_BIT,
                                pdTRUE, pdFALSE, portMAX_DELAY);
        } else {
            ESP_LOGW(TAG, "没有可连接的已知热点，%d 秒后重扫",
                     WIFI_RESCAN_INTERVAL_MS / 1000);
            vTaskDelay(pdMS_TO_TICKS(WIFI_RESCAN_INTERVAL_MS));
        }
    }
}

esp_err_t wifi_init_sta(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败：%s", esp_err_to_name(err));
        return err;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) return ESP_ERR_NO_MEM;

    err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;
    if (esp_netif_create_default_wifi_sta() == NULL) return ESP_FAIL;

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                              event_handler, NULL, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              event_handler, NULL, NULL);
    if (err != ESP_OK) return err;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    // 每次切换账号不写入 flash，避免反复扫描导致 NVS 磨损。
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    if (xTaskCreate(wifi_manager_task, "wifi_manager", 4096, NULL, 2, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Wi-Fi 管理任务已启动，将按配置顺序扫描和连接");
    return ESP_OK;
}
