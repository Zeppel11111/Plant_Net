#include "mqtt_service.h"

#include "esp_log.h"

static const char *TAG = "mqtt_service";
static esp_mqtt_client_handle_t s_client;

esp_err_t mqtt_service_init(const char *broker_uri)
{
    if (broker_uri == NULL || broker_uri[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_client != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "创建 MQTT 客户端失败");
        return ESP_FAIL;
    }

    s_client = client;
    ESP_LOGI(TAG, "MQTT 客户端已创建，Broker：%s", broker_uri);
    return ESP_OK;
}

esp_err_t mqtt_service_register_event(esp_mqtt_event_id_t event_id,
                                      esp_event_handler_t event_handler,
                                      void *handler_args)
{
    if (event_handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_mqtt_client_register_event(s_client, event_id, event_handler, handler_args);
}

esp_err_t mqtt_service_unregister_event(esp_mqtt_event_id_t event_id,
                                        esp_event_handler_t event_handler)
{
    if (event_handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_mqtt_client_unregister_event(s_client, event_id, event_handler);
}

esp_err_t mqtt_service_start(void)
{
    if (s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "启动 MQTT 客户端失败：%s", esp_err_to_name(err));
    }
    return err;
}


int mqtt_service_publish(const char *topic, const char *data, int len, int qos, int retain)
{
    if (s_client == NULL || topic == NULL) {
        return -1;
    }
    return esp_mqtt_client_publish(s_client, topic, data, len, qos, retain);
}
