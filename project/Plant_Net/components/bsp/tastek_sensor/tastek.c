#include "tastek.h"
#include "bsp.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TASTEK_ADDRESS          0x01
#define TASTEK_READ_FUNCTION    0x03
#define TASTEK_LIGHT_DATA_BYTES 2
#define TASTEK_AIR_DATA_BYTES   4
#define TASTEK_TIMEOUT_US       300000
#define TASTEK_BUS_LOCK_TIMEOUT_MS 1000

static const char *TAG = "塔石";
static const uint8_t read_light_cmd[8] = {0x01, 0x03, 0x00, 0x07, 0x00, 0x01, 0x35, 0xCB};
static const uint8_t read_air_cmd[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};

// Modbus RTU 的 CRC16；返回值低字节先发送。
static uint16_t tastek_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
    }
    return crc;
}

// 多次读取并拼接指定数量的原始字节，所有读取共用同一个截止时间。
static esp_err_t tastek_read_exact(uint8_t *data, size_t length, int64_t deadline_us,
                                   const char *stage)
{
    size_t received = 0;
    while (received < length)
    {
        int64_t remaining_us = deadline_us - esp_timer_get_time();
        if (remaining_us <= 0)
        {
            ESP_LOGW(TAG, "%s 超时：收到 %u/%u 字节", stage,
                     (unsigned)received, (unsigned)length);
            if (received > 0)
            {
                ESP_LOG_BUFFER_HEX(TAG, data, received);
            }
            return ESP_ERR_TIMEOUT;
        }

        uint32_t wait_ms = (uint32_t)((remaining_us + 999) / 1000);
        int count = bsp_uart_receive(BSP_UART_SENSOR, data + received,
                                     (uint32_t)(length - received), wait_ms);
        if (count < 0)
        {
            ESP_LOGE(TAG, "%s UART 读取失败：%d", stage, count);
            return ESP_FAIL;
        }
        if (count > 0)
        {
            ESP_LOGI(TAG, "%s 本次收到 %d 字节", stage, count);
            ESP_LOG_BUFFER_HEX(TAG, data + received, count);
        }
        received += (size_t)count;
    }
    return ESP_OK;
}

// 发送查询并收齐一帧；正常帧长度由第 3 字节的数据长度确定。
static esp_err_t tastek_read_frame_unlocked(const uint8_t *command, size_t command_length,
                                           uint8_t expected_data_bytes, uint8_t *frame, size_t frame_capacity)
{
    if (command == NULL || frame == NULL || frame_capacity < (size_t)expected_data_bytes + 5)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // 丢弃上一次失败查询留下的字节，再发送本次查询。
    esp_err_t err = bsp_uart_clear_rx(BSP_UART_SENSOR);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "查询前清空 UART1 接收缓冲区失败：%s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "发送查询：从站 0x%02X，寄存器 0x%02X，读取 %u 个寄存器",
             command[0], command[3], (unsigned)expected_data_bytes / 2);
    ESP_LOG_BUFFER_HEX(TAG, command, command_length);
    int sent = bsp_uart_send(BSP_UART_SENSOR, command, (int)command_length);
    if (sent != (int)command_length)
    {
        ESP_LOGE(TAG, "查询发送不完整：写入 %d/%u 字节", sent, (unsigned)command_length);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "UART1 已接收 %d 字节用于发送，等待从站应答", sent);

    int64_t deadline_us = esp_timer_get_time() + TASTEK_TIMEOUT_US;

    // 先读取地址、功能码和数据字节数（异常响应时第 3 字节是异常码）。
    err = tastek_read_exact(frame, 3, deadline_us, "应答帧头");
    if (err != ESP_OK)
    {
        return err;
    }
    if (frame[0] != TASTEK_ADDRESS)
    {
        ESP_LOGE(TAG, "应答地址不符：收到 0x%02X，期望 0x%02X",
                 frame[0], TASTEK_ADDRESS);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (frame[1] == (TASTEK_READ_FUNCTION | 0x80))
    {
        // 异常帧总长 5 字节，最后两个字节也是 CRC。
        err = tastek_read_exact(frame + 3, 2, deadline_us, "异常帧 CRC");
        if (err != ESP_OK)
        {
            return err;
        }
        ESP_LOGW(TAG, "收到 Modbus 异常应答：");
        ESP_LOG_BUFFER_HEX(TAG, frame, 5);
        uint16_t crc = tastek_crc16(frame, 3);
        if (frame[3] != (uint8_t)crc || frame[4] != (uint8_t)(crc >> 8))
        {
            ESP_LOGE(TAG, "异常帧 CRC 不符：收到 %02X %02X，计算得到 %02X %02X",
                     frame[3], frame[4], (uint8_t)crc, (uint8_t)(crc >> 8));
            return ESP_ERR_INVALID_CRC;
        }
        ESP_LOGE(TAG, "传感器返回异常码：0x%02X", frame[2]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (frame[1] != TASTEK_READ_FUNCTION || frame[2] != expected_data_bytes)
    {
        ESP_LOGE(TAG, "应答帧头不符：功能码 0x%02X，数据长度 %u；期望 0x%02X 和 %u",
                 frame[1], frame[2], TASTEK_READ_FUNCTION, expected_data_bytes);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // 正常帧长度 = 地址、功能码、字节数 3 字节 + 数据区 + CRC 2 字节。
    err = tastek_read_exact(frame + 3, frame[2] + 2, deadline_us, "应答数据和 CRC");
    if (err != ESP_OK)
    {
        return err;
    }
    ESP_LOGI(TAG, "收到完整应答帧：");
    ESP_LOG_BUFFER_HEX(TAG, frame, 3 + frame[2] + 2);
    uint16_t crc = tastek_crc16(frame, 3 + frame[2]);
    size_t crc_index = 3 + frame[2];
    if (frame[crc_index] != (uint8_t)crc || frame[crc_index + 1] != (uint8_t)(crc >> 8))
    {
        ESP_LOGE(TAG, "应答 CRC 不符：收到 %02X %02X，计算得到 %02X %02X",
                 frame[crc_index], frame[crc_index + 1], (uint8_t)crc, (uint8_t)(crc >> 8));
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}

// UART1 是共享的 RS485 总线；锁住完整的请求/响应，避免不同传感器帧交叉。
static esp_err_t tastek_read_frame(const uint8_t *command, size_t command_length,
                                   uint8_t expected_data_bytes, uint8_t *frame, size_t frame_capacity)
{
    esp_err_t err = bsp_uart_transaction_lock(BSP_UART_SENSOR, TASTEK_BUS_LOCK_TIMEOUT_MS);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "获取 485 总线锁失败：%s", esp_err_to_name(err));
        return err;
    }

    err = tastek_read_frame_unlocked(command, command_length, expected_data_bytes, frame, frame_capacity);
    esp_err_t unlock_err = bsp_uart_transaction_unlock(BSP_UART_SENSOR);
    if (unlock_err != ESP_OK)
    {
        ESP_LOGE(TAG, "释放 485 总线锁失败：%s", esp_err_to_name(unlock_err));
    }
    return err == ESP_OK ? unlock_err : err;
}

esp_err_t tastek_read_light(tastek_data_t *data)
{
    if (data == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t frame[3 + TASTEK_LIGHT_DATA_BYTES + 2];
    esp_err_t err = tastek_read_frame(read_light_cmd, sizeof(read_light_cmd),
                                     TASTEK_LIGHT_DATA_BYTES, frame, sizeof(frame));
    if (err != ESP_OK)
    {
        return err;
    }

    data->light_data = ((uint16_t)frame[3] << 8) | frame[4];
    return ESP_OK;
}

esp_err_t tastek_read_air_temp_humidity(tastek_data_t *data)
{
    if (data == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t frame[3 + TASTEK_AIR_DATA_BYTES + 2];
    esp_err_t err = tastek_read_frame(read_air_cmd, sizeof(read_air_cmd),
                                     TASTEK_AIR_DATA_BYTES, frame, sizeof(frame));
    if (err != ESP_OK)
    {
        return err;
    }

    // 手册中先返回湿度，再返回温度；原始整数都放大了 10 倍。
    uint16_t humidity_raw = ((uint16_t)frame[3] << 8) | frame[4];
    uint16_t temperature_raw = ((uint16_t)frame[5] << 8) | frame[6];
    int32_t temperature_tenths = temperature_raw;
    if (temperature_raw & 0x8000)
    {
        temperature_tenths -= 0x10000; // 按 16 位补码还原负温度
    }

    data->air_water_data = humidity_raw / 10.0f;
    data->air_temp_data = temperature_tenths / 10.0f;
    return ESP_OK;
}
