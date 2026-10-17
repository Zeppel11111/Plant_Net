#include    <stdio.h>
#include    "TJC.h"
#include    "bsp.h"
#include    "esp_log.h"
#include    "soil_sensor_debug_internal.h"

static const char *TAG = "TJC";

esp_err_t TJC_debug_soil_read_address(uint8_t probe_address, uint8_t *configured_address)
{
    if (probe_address == 0 || probe_address > 247 || configured_address == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t value = 0;
    esp_err_t err = soil_debug_read_holding_register(probe_address, 0x0080, &value);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "土壤传感器地址查询失败，探测地址 0x%02X：%s",
                 probe_address, esp_err_to_name(err));
        return err;
    }
    if (value == 0 || value > 247)
    {
        ESP_LOGE(TAG, "土壤传感器返回无效地址值：0x%04X", value);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *configured_address = (uint8_t)value;
    ESP_LOGI(TAG, "土壤传感器地址查询成功：0x%02X", *configured_address);
    return ESP_OK;
}

esp_err_t TJC_debug_soil_write_address(uint8_t current_address, uint8_t new_address)
{
    if (current_address == 0 || current_address > 247 ||
        new_address == 0 || new_address > 247 || current_address == new_address)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = soil_debug_write_address_register(current_address, new_address);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "土壤传感器地址修改失败：0x%02X -> 0x%02X：%s",
                 current_address, new_address, esp_err_to_name(err));
        return err;
    }

    uint8_t actual_address = 0;
    err = TJC_debug_soil_read_address(new_address, &actual_address);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "地址写入有应答，但新地址 0x%02X 的读回校验失败：%s",
                 new_address, esp_err_to_name(err));
        return err;
    }
    if (actual_address != new_address)
    {
        ESP_LOGE(TAG, "地址读回不匹配：期望 0x%02X，实际 0x%02X",
                 new_address, actual_address);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "土壤传感器地址修改并读回成功：0x%02X -> 0x%02X",
             current_address, new_address);
    return ESP_OK;
}

// 数值统一格式化为文本，自动拼接等号、双引号和 0xFF 0xFF 0xFF 结束符。
esp_err_t TJC_send(const char *cmd, double data, uint8_t decimals)
{
    if (cmd == NULL || decimals > 6)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t const end_flag[3] = {0xFF, 0xFF, 0xFF};

    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%s=\"%.*f\"", cmd, (int)decimals, data);

    if (len < 0 || len >= (int)sizeof(buf)) {
        return ESP_FAIL;
    }

    int sent = bsp_uart_send(BSP_UART_SCREEN, (const uint8_t *)buf, len);
    if (sent != len)
    {
        ESP_LOGE(TAG, "命令发送失败，UART2 写入 %d/%d 字节: %s", sent, len, cmd);
        return ESP_FAIL;
    }

    sent = bsp_uart_send(BSP_UART_SCREEN, end_flag, sizeof(end_flag));
    if (sent != (int)sizeof(end_flag))
    {
        ESP_LOGE(TAG, "命令结束符发送失败，UART2 写入 %d/3 字节: %s", sent, cmd);
        return ESP_FAIL;
    }

    return ESP_OK;
}
