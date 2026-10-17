/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "mqtt_client.h"

#include "wifi.h"
#include "mqtt_service.h"
#include "bsp.h"
#include "Sensor_data.h"
#include "TJC.h"

// 屏幕接收逐项更新的快照；MQTT 每轮采集结束后接收一次完整快照。
static QueueHandle_t Sensor_data_Queue;
static QueueHandle_t Mqtt_data_Queue;
static EventGroupHandle_t Mqtt_event_group;
#define MQTT_CONNECTED_BIT BIT0

// 每读完一类传感器就通知屏幕；网络任务只接收一轮结束后的完整快照。
static void queue_screen_snapshot(const sensor_data_t *snapshot, void *context)
{
    (void)context;
    if (xQueueOverwrite(Sensor_data_Queue, snapshot) != pdPASS)
    {
        ESP_LOGE("数据流", "最新数据写入屏幕队列失败");
    }
}

static void sensor_data_get_task(void *arg)
{
    sensor_data_t sensor_data = {0};

    (void)arg;
    for (;;)
    {
        // UART1 上仍只有本任务顺序轮询，保持三只 485 设备的总线访问次序。
        Sensor_Data_get(&sensor_data, queue_screen_snapshot, NULL);

        // 四类传感器都读完后再上报一次，避免一轮采集产生四份 MQTT 消息。
        if (xQueueOverwrite(Mqtt_data_Queue, &sensor_data) != pdPASS)
        {
            ESP_LOGE("数据流", "完整采集快照写入 MQTT 队列失败");
        }

        // 每轮采集完成后等待 5 分钟，再开始下一轮。
        vTaskDelay(pdMS_TO_TICKS(5 * 60 * 1000));
    }
}

// 进行数据本地上报至陶晶驰
static void TJC_show_task(void *arg)
{
    (void)arg;
    sensor_data_t data;
    uint32_t shown_light_revision = 0;
    uint32_t shown_air_revision = 0;
    uint32_t shown_soil_1_revision = 0;
    uint32_t shown_soil_2_revision = 0;
    for (;;)
    {

        if (xQueueReceive(Sensor_data_Queue, &data, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        // 修订号保存在累计快照中；即使队列覆盖了中间消息，也不会漏掉最新字段。
        if (data.air_revision != shown_air_revision)
        {
            shown_air_revision = data.air_revision;
            if (data.air_valid)
            {
                esp_err_t temp_err = TJC_send("Air.tTemp.txt", data.tas_data.air_temp_data, 1);
                esp_err_t hum_err = TJC_send("Air.tHum.txt", data.tas_data.air_water_data, 1);
                if (temp_err != ESP_OK || hum_err != ESP_OK)
                {
                    ESP_LOGE("陶晶驰", "空气数据发送失败：温度 %s，湿度 %s",
                             esp_err_to_name(temp_err), esp_err_to_name(hum_err));
                }
            }
        }

        if (data.light_revision != shown_light_revision)
        {
            shown_light_revision = data.light_revision;
            if (data.light_valid)
            {
                esp_err_t lux_err = TJC_send("Air.tLux.txt", data.tas_data.light_data, 1);
                if (lux_err != ESP_OK)
                {
                    ESP_LOGE("陶晶驰", "光照数据发送失败：%s", esp_err_to_name(lux_err));
                }
            }
        }

        if (data.soil_data_1_revision != shown_soil_1_revision)
        {
            shown_soil_1_revision = data.soil_data_1_revision;
            if (data.soil_data_1_valid)
            {
                // 从站 0x02 对应土壤页面原有的八个文本控件。
                unsigned int send_errors = 0;
                send_errors += TJC_send("soil.tTemp.txt", data.soil_data_1.temperature_c, 1) != ESP_OK;
                send_errors += TJC_send("soil.tMoist.txt", data.soil_data_1.moisture_percent, 1) != ESP_OK;
                send_errors += TJC_send("soil.tEC.txt", data.soil_data_1.ec_us_cm, 0) != ESP_OK;
                send_errors += TJC_send("soil.tPH.txt", data.soil_data_1.ph, 1) != ESP_OK;
                send_errors += TJC_send("soil.tSalt.txt", data.soil_data_1.salt_ppm, 0) != ESP_OK;
                send_errors += TJC_send("soil.tN.txt", data.soil_data_1.nitrogen_mg_kg, 0) != ESP_OK;
                send_errors += TJC_send("soil.tP.txt", data.soil_data_1.phosphorus_mg_kg, 0) != ESP_OK;
                send_errors += TJC_send("soil.tK.txt", data.soil_data_1.potassium_mg_kg, 0) != ESP_OK;

                if (send_errors == 0)
                {
                    ESP_LOGI("陶晶驰", "土壤 0x02 的 8 项指令已写入 UART2");
                }
                else
                {
                    ESP_LOGE("陶晶驰", "土壤 0x02 指令写入失败：%u/8 条", send_errors);
                }
            }
        }

        if (data.soil_data_2_revision != shown_soil_2_revision)
        {
            shown_soil_2_revision = data.soil_data_2_revision;
            if (data.soil_data_2_valid)
            {
                // 从站 0x03 的控件名称是在 0x02 对应名称后加 2。
                unsigned int send_errors = 0;
                send_errors += TJC_send("soil.tTemp2.txt", data.soil_data_2.temperature_c, 1) != ESP_OK;
                send_errors += TJC_send("soil.tMoist2.txt", data.soil_data_2.moisture_percent, 1) != ESP_OK;
                send_errors += TJC_send("soil.tEC2.txt", data.soil_data_2.ec_us_cm, 0) != ESP_OK;
                send_errors += TJC_send("soil.tPH2.txt", data.soil_data_2.ph, 1) != ESP_OK;
                send_errors += TJC_send("soil.tSalt2.txt", data.soil_data_2.salt_ppm, 0) != ESP_OK;
                send_errors += TJC_send("soil.tN2.txt", data.soil_data_2.nitrogen_mg_kg, 0) != ESP_OK;
                send_errors += TJC_send("soil.tP2.txt", data.soil_data_2.phosphorus_mg_kg, 0) != ESP_OK;
                send_errors += TJC_send("soil.tK2.txt", data.soil_data_2.potassium_mg_kg, 0) != ESP_OK;

                if (send_errors == 0)
                {
                    ESP_LOGI("陶晶驰", "土壤 0x03 的 8 项指令已写入 UART2");
                }
                else
                {
                    ESP_LOGE("陶晶驰", "土壤 0x03 指令写入失败：%u/8 条", send_errors);
                }
            }
        }
    }
}

static const char *TAG = "main";

// MQTT 独立消费最新快照；网络发送慢时只会覆盖旧消息，不会阻塞 485 采集。
static void mqtt_publish_task(void *arg)
{
    (void)arg;
    sensor_data_t data;
    char payload[768];

    for (;;)
    {
        if (xQueueReceive(Mqtt_data_Queue, &data, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        if ((xEventGroupGetBits(Mqtt_event_group) & MQTT_CONNECTED_BIT) == 0)
        {
            continue;
        }

        // valid=false 时数值是上一次成功读取的值，订阅者应按有效标记判断新鲜度。
        int len = snprintf(payload, sizeof(payload),
                           "{\"light\":%lu,\"air_temp\":%.1f,\"air_hum\":%.1f,"
                           "\"soil1\":{\"address\":\"0x%02X\",\"temp\":%.1f,\"moist\":%.1f,\"ec\":%u,\"salt\":%u,"
                           "\"n\":%u,\"p\":%u,\"k\":%u,\"ph\":%.1f},"
                           "\"soil2\":{\"address\":\"0x%02X\",\"temp\":%.1f,\"moist\":%.1f,\"ec\":%u,\"salt\":%u,"
                           "\"n\":%u,\"p\":%u,\"k\":%u,\"ph\":%.1f},"
                           "\"valid\":{\"light\":%s,\"air\":%s,\"soil1\":%s,\"soil2\":%s}}",
                           (unsigned long)data.tas_data.light_data,
                           data.tas_data.air_temp_data, data.tas_data.air_water_data,
                           (unsigned)SOIL_SENSOR_DEFAULT_ADDRESS,
                           data.soil_data_1.temperature_c, data.soil_data_1.moisture_percent,
                           (unsigned)data.soil_data_1.ec_us_cm, (unsigned)data.soil_data_1.salt_ppm,
                           (unsigned)data.soil_data_1.nitrogen_mg_kg,
                           (unsigned)data.soil_data_1.phosphorus_mg_kg,
                           (unsigned)data.soil_data_1.potassium_mg_kg, data.soil_data_1.ph,
                           (unsigned)SOIL_SENSOR_SECOND_ADDRESS,
                           data.soil_data_2.temperature_c, data.soil_data_2.moisture_percent,
                           (unsigned)data.soil_data_2.ec_us_cm, (unsigned)data.soil_data_2.salt_ppm,
                           (unsigned)data.soil_data_2.nitrogen_mg_kg,
                           (unsigned)data.soil_data_2.phosphorus_mg_kg,
                           (unsigned)data.soil_data_2.potassium_mg_kg, data.soil_data_2.ph,
                           data.light_valid ? "true" : "false",
                           data.air_valid ? "true" : "false",
                           data.soil_data_1_valid ? "true" : "false",
                           data.soil_data_2_valid ? "true" : "false");
        if (len < 0 || len >= (int)sizeof(payload))
        {
            ESP_LOGE(TAG, "MQTT 数据格式化失败或缓冲区不足");
            continue;
        }

        int msg_id = mqtt_service_publish("plant_net/Zeppel1/test", payload, len, 0, 0);
        if (msg_id < 0)
        {
            ESP_LOGW(TAG, "实时数据提交 MQTT 失败：%d", msg_id);
        }
        else
        {
            ESP_LOGD(TAG, "实时数据已提交 MQTT：msg_id=%d", msg_id);
        }
    }
}

// MQTT 回调在客户端事件任务中执行，不应在这里做长时间阻塞的工作。
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    const esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "已连接 MQTT Broker");
        xEventGroupSetBits(Mqtt_event_group, MQTT_CONNECTED_BIT);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        xEventGroupClearBits(Mqtt_event_group, MQTT_CONNECTED_BIT);
        ESP_LOGW(TAG, "MQTT 连接已断开");
        break;
    case MQTT_EVENT_ERROR:
        if (event->error_handle != NULL) {
            ESP_LOGE(TAG, "MQTT 错误类型：%d", event->error_handle->error_type);
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "传输错误：esp-tls=0x%x，socket errno=%d",
                         event->error_handle->esp_tls_last_esp_err,
                         event->error_handle->esp_transport_sock_errno);
            }
        }
        break;
    default:
        break;
    }
}

void app_main(void)
{

    // 进行引脚资源的初始化
    ESP_LOGI(TAG, "bsp资源初始化开始");

    vTaskDelay(pdMS_TO_TICKS(20));
    //
    esp_err_t bsp_err = bsp_port_init();
    if (bsp_err != ESP_OK)
    {
        ESP_LOGE(TAG, "%s", esp_err_to_name(bsp_err));
        return;
    }
    else
        ESP_LOGI(TAG, "bsp资源初始化成功");

    vTaskDelay(pdMS_TO_TICKS(20));

    // 两个消费者各自使用单元素队列，事件组记录 MQTT 连接状态。
    Sensor_data_Queue = xQueueCreate(1, sizeof(sensor_data_t));
    Mqtt_data_Queue = xQueueCreate(1, sizeof(sensor_data_t));
    Mqtt_event_group = xEventGroupCreate();

    if (Sensor_data_Queue == NULL || Mqtt_data_Queue == NULL || Mqtt_event_group == NULL)
    {
        ESP_LOGE(TAG, "数据队列或 MQTT 事件组创建失败");
        return;
    }

    // 初始化完毕，开始创建各任务
    if (xTaskCreate(sensor_data_get_task, "sensor_data_get", 3072, NULL, 1, NULL) == pdPASS)
    {
        ESP_LOGI(TAG, "塔石采集任务创建成功");
    }
    else
    {
        ESP_LOGE(TAG, "塔石采集任务创建失败");
    }

    if (xTaskCreate(TJC_show_task, "TJC_show_task", 3072, NULL, 1, NULL) == pdPASS)
    {
        ESP_LOGI(TAG, "陶晶驰任务创建成功");
    }
    else
    {
        ESP_LOGE(TAG, "陶晶驰任务创建失败");
    }

    if (xTaskCreate(mqtt_publish_task, "mqtt_publish", 6144, NULL, 1, NULL) == pdPASS)
    {
        ESP_LOGI(TAG, "MQTT 实时上报任务创建成功");
    }
    else
    {
        ESP_LOGE(TAG, "MQTT 实时上报任务创建失败");
    }

    // Wi-Fi 在后台扫描与切换；MQTT 客户端自行等待网络可用并自动重连。
    esp_err_t wifi_err = wifi_init_sta();
    if (wifi_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Wi-Fi 管理任务启动失败：%s", esp_err_to_name(wifi_err));
    }
    else
    {

        // 先创建客户端并绑定回调，再启动连接，避免错过首次连接事件。
        esp_err_t mqtt_err = mqtt_service_init(MQTT_BROKER_URL);
        if (mqtt_err == ESP_OK)
        {
            mqtt_err = mqtt_service_register_event(ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
        }
        if (mqtt_err == ESP_OK)
        {
            mqtt_err = mqtt_service_start();
        }
        if (mqtt_err != ESP_OK)
        {
            ESP_LOGE(TAG, "MQTT 服务初始化或启动失败：%s", esp_err_to_name(mqtt_err));
        }
        else
        {
            ESP_LOGI(TAG, "MQTT 服务已启动");
        }

    }




}
