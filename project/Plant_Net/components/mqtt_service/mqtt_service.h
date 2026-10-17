#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include "esp_err.h"
#include "mqtt_service_config.h"
#include "mqtt_client.h"

/**
 * @brief 创建 MQTT 客户端，暂不连接 Broker。
 *
 * 应在 Wi-Fi 获取 IP 后调用。broker_uri 例如 "mqtt://192.168.137.1:1883"。
 * 初始化后先注册需要的事件回调，再调用 mqtt_service_start()。
 *
 * @param broker_uri MQTT Broker 地址，不能为空。
 * @return ESP_OK 创建成功；其他错误码表示参数或创建过程出错。
 */
esp_err_t mqtt_service_init(const char *broker_uri);

/**
 * @brief 为指定 MQTT 事件注册回调；可用 ESP_EVENT_ANY_ID 接收所有事件。
 *
 * 可以多次调用以绑定不同回调。回调在 MQTT 客户端事件任务中执行，不应长时间阻塞。
 * @param handler_args 传递给回调的用户参数，可以为 NULL；须在回调注册期间保持有效。
 */
esp_err_t mqtt_service_register_event(esp_mqtt_event_id_t event_id,
                                      esp_event_handler_t event_handler,
                                      void *handler_args);

/** @brief 取消之前注册的指定事件回调。 */
esp_err_t mqtt_service_unregister_event(esp_mqtt_event_id_t event_id,
                                        esp_event_handler_t event_handler);

/** @brief 启动 MQTT 客户端；应在注册回调之后调用。 */
esp_err_t mqtt_service_start(void);

/**
 * @brief 使用本模块保存的客户端句柄发布消息。
 *
 * @return QoS 0 成功时返回 0；QoS 1/2 成功时返回消息 ID；-1 表示失败，-2 表示发送队列已满。
 */
int mqtt_service_publish(const char *topic, const char *data, int len, int qos, int retain);

#endif
