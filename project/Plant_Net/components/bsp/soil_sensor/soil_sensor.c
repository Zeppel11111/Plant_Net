#include "soil_sensor.h"
#include "bsp.h"
#include "esp_log.h"
#include "esp_timer.h"

#define SOIL_READ_FUNCTION          0x03
#define SOIL_WRITE_MULTIPLE_FUNCTION 0x10
#define SOIL_REGISTER_START         0x0000
#define SOIL_REGISTER_COUNT         8
#define SOIL_ADDRESS_REGISTER       0x0080
#define SOIL_RESPONSE_DATA_BYTES    (SOIL_REGISTER_COUNT * 2)
#define SOIL_RESPONSE_FRAME_BYTES   (3 + SOIL_RESPONSE_DATA_BYTES + 2)
#define SOIL_ADDRESS_READ_FRAME_BYTES 7
#define SOIL_ADDRESS_WRITE_FRAME_BYTES 8
#define SOIL_TIMEOUT_US             300000
#define SOIL_BUS_LOCK_TIMEOUT_MS    1000

static const char *TAG = "soil_sensor";

// Modbus RTU CRC16，返回值低字节先发送。
static uint16_t soil_crc16(const uint8_t *bytes, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
    }
    return crc;
}

// 在统一截止时间前收齐指定字节，避免串口分段到达导致短帧误判。
static esp_err_t soil_read_exact(uint8_t *bytes, size_t length, int64_t deadline_us)
{
    size_t received = 0;
    while (received < length)
    {
        int64_t remaining_us = deadline_us - esp_timer_get_time();
        if (remaining_us <= 0)
        {
            return ESP_ERR_TIMEOUT;
        }

        uint32_t wait_ms = (uint32_t)((remaining_us + 999) / 1000);
        int count = bsp_uart_receive(BSP_UART_SENSOR, bytes + received,
                                     (uint32_t)(length - received), wait_ms);
        if (count < 0)
        {
            return ESP_FAIL;
        }
        received += (size_t)count;
    }
    return ESP_OK;
}

// 读取一个保持寄存器。仅供开发调试接口读取传感器配置寄存器。
esp_err_t soil_debug_read_holding_register(uint8_t slave_address,
                                           uint16_t register_address,
                                           uint16_t *value)
{
    if (slave_address == 0 || slave_address > 247 || value == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t command[8] = {
        slave_address,
        SOIL_READ_FUNCTION,
        (uint8_t)(register_address >> 8),
        (uint8_t)register_address,
        0x00,
        0x01,
        0,
        0,
    };
    uint16_t command_crc = soil_crc16(command, sizeof(command) - 2);
    command[6] = (uint8_t)command_crc;
    command[7] = (uint8_t)(command_crc >> 8);

    esp_err_t err = bsp_uart_transaction_lock(BSP_UART_SENSOR, SOIL_BUS_LOCK_TIMEOUT_MS);
    if (err != ESP_OK)
    {
        return err;
    }

    uint8_t frame[SOIL_ADDRESS_READ_FRAME_BYTES] = {0};
    err = bsp_uart_clear_rx(BSP_UART_SENSOR);
    if (err != ESP_OK)
    {
        goto read_unlock;
    }

    int sent = bsp_uart_send(BSP_UART_SENSOR, command, sizeof(command));
    if (sent != (int)sizeof(command))
    {
        ESP_LOGE(TAG, "从站 0x%02X 配置寄存器查询发送失败：%d 字节", slave_address, sent);
        err = ESP_FAIL;
        goto read_unlock;
    }

    int64_t deadline_us = esp_timer_get_time() + SOIL_TIMEOUT_US;
    err = soil_read_exact(frame, 3, deadline_us);
    if (err != ESP_OK)
    {
        goto read_unlock;
    }
    if (frame[0] != slave_address)
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto read_unlock;
    }

    if (frame[1] == (SOIL_READ_FUNCTION | 0x80))
    {
        err = soil_read_exact(frame + 3, 2, deadline_us);
        if (err != ESP_OK)
        {
            goto read_unlock;
        }
        uint16_t crc = soil_crc16(frame, 3);
        if (frame[3] != (uint8_t)crc || frame[4] != (uint8_t)(crc >> 8))
        {
            err = ESP_ERR_INVALID_CRC;
            goto read_unlock;
        }
        ESP_LOGE(TAG, "从站 0x%02X 读取配置寄存器异常：0x%02X", slave_address, frame[2]);
        err = ESP_ERR_INVALID_RESPONSE;
        goto read_unlock;
    }

    if (frame[1] != SOIL_READ_FUNCTION || frame[2] != 0x02)
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto read_unlock;
    }

    err = soil_read_exact(frame + 3, 4, deadline_us);
    if (err != ESP_OK)
    {
        goto read_unlock;
    }

    uint16_t crc = soil_crc16(frame, 5);
    if (frame[5] != (uint8_t)crc || frame[6] != (uint8_t)(crc >> 8))
    {
        err = ESP_ERR_INVALID_CRC;
        goto read_unlock;
    }

    *value = ((uint16_t)frame[3] << 8) | frame[4];
    err = ESP_OK;

read_unlock:
    {
        esp_err_t unlock_err = bsp_uart_transaction_unlock(BSP_UART_SENSOR);
        return err == ESP_OK ? unlock_err : err;
    }
}

// 将传感器地址寄存器 0x0080 写入新地址，并校验 Modbus 0x10 应答。
esp_err_t soil_debug_write_address_register(uint8_t current_address, uint8_t new_address)
{
    if (current_address == 0 || current_address > 247 ||
        new_address == 0 || new_address > 247 || current_address == new_address)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t command[11] = {
        current_address,
        SOIL_WRITE_MULTIPLE_FUNCTION,
        (uint8_t)(SOIL_ADDRESS_REGISTER >> 8),
        (uint8_t)SOIL_ADDRESS_REGISTER,
        0x00,
        0x01,
        0x02,
        0x00,
        new_address,
        0,
        0,
    };
    uint16_t command_crc = soil_crc16(command, sizeof(command) - 2);
    command[9] = (uint8_t)command_crc;
    command[10] = (uint8_t)(command_crc >> 8);

    esp_err_t err = bsp_uart_transaction_lock(BSP_UART_SENSOR, SOIL_BUS_LOCK_TIMEOUT_MS);
    if (err != ESP_OK)
    {
        return err;
    }

    uint8_t frame[SOIL_ADDRESS_WRITE_FRAME_BYTES] = {0};
    err = bsp_uart_clear_rx(BSP_UART_SENSOR);
    if (err != ESP_OK)
    {
        goto write_unlock;
    }

    int sent = bsp_uart_send(BSP_UART_SENSOR, command, sizeof(command));
    if (sent != (int)sizeof(command))
    {
        ESP_LOGE(TAG, "从站 0x%02X 地址修改发送失败：%d 字节", current_address, sent);
        err = ESP_FAIL;
        goto write_unlock;
    }

    int64_t deadline_us = esp_timer_get_time() + SOIL_TIMEOUT_US;
    err = soil_read_exact(frame, 2, deadline_us);
    if (err != ESP_OK)
    {
        goto write_unlock;
    }

    if (frame[0] != current_address && frame[0] != new_address)
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto write_unlock;
    }

    if (frame[1] == (SOIL_WRITE_MULTIPLE_FUNCTION | 0x80))
    {
        err = soil_read_exact(frame + 2, 3, deadline_us);
        if (err != ESP_OK)
        {
            goto write_unlock;
        }
        uint16_t crc = soil_crc16(frame, 3);
        if (frame[3] != (uint8_t)crc || frame[4] != (uint8_t)(crc >> 8))
        {
            err = ESP_ERR_INVALID_CRC;
            goto write_unlock;
        }
        ESP_LOGE(TAG, "从站 0x%02X 修改地址异常：0x%02X", current_address, frame[2]);
        err = ESP_ERR_INVALID_RESPONSE;
        goto write_unlock;
    }

    if (frame[1] != SOIL_WRITE_MULTIPLE_FUNCTION)
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto write_unlock;
    }

    err = soil_read_exact(frame + 2, SOIL_ADDRESS_WRITE_FRAME_BYTES - 2, deadline_us);
    if (err != ESP_OK)
    {
        goto write_unlock;
    }

    uint16_t crc = soil_crc16(frame, SOIL_ADDRESS_WRITE_FRAME_BYTES - 2);
    if (frame[6] != (uint8_t)crc || frame[7] != (uint8_t)(crc >> 8))
    {
        err = ESP_ERR_INVALID_CRC;
        goto write_unlock;
    }

    if (frame[2] != (uint8_t)(SOIL_ADDRESS_REGISTER >> 8) ||
        frame[3] != (uint8_t)SOIL_ADDRESS_REGISTER ||
        frame[4] != 0x00 || frame[5] != 0x01)
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto write_unlock;
    }
    err = ESP_OK;

write_unlock:
    {
        esp_err_t unlock_err = bsp_uart_transaction_unlock(BSP_UART_SENSOR);
        return err == ESP_OK ? unlock_err : err;
    }
}

// 读取 0x0000 起始的 8 个保持寄存器，并校验地址、长度、功能码和 CRC。
static esp_err_t soil_read_registers(uint8_t slave_address, uint16_t registers[SOIL_REGISTER_COUNT])
{
    uint8_t command[8] = {
        slave_address,
        SOIL_READ_FUNCTION,
        (uint8_t)(SOIL_REGISTER_START >> 8),
        (uint8_t)SOIL_REGISTER_START,
        (uint8_t)(SOIL_REGISTER_COUNT >> 8),
        (uint8_t)SOIL_REGISTER_COUNT,
        0,
        0,
    };
    uint16_t command_crc = soil_crc16(command, sizeof(command) - 2);
    command[6] = (uint8_t)command_crc;
    command[7] = (uint8_t)(command_crc >> 8);

    esp_err_t err = bsp_uart_transaction_lock(BSP_UART_SENSOR, SOIL_BUS_LOCK_TIMEOUT_MS);
    if (err != ESP_OK)
    {
        return err;
    }

    uint8_t frame[SOIL_RESPONSE_FRAME_BYTES] = {0};
    err = bsp_uart_clear_rx(BSP_UART_SENSOR);
    if (err != ESP_OK)
    {
        goto unlock;
    }

    int sent = bsp_uart_send(BSP_UART_SENSOR, command, sizeof(command));
    if (sent != (int)sizeof(command))
    {
        ESP_LOGE(TAG, "从站 0x%02X 查询发送失败：%d 字节", slave_address, sent);
        err = ESP_FAIL;
        goto unlock;
    }

    int64_t deadline_us = esp_timer_get_time() + SOIL_TIMEOUT_US;
    err = soil_read_exact(frame, 3, deadline_us);
    if (err != ESP_OK)
    {
        goto unlock;
    }
    if (frame[0] != slave_address)
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto unlock;
    }

    if (frame[1] == (SOIL_READ_FUNCTION | 0x80))
    {
        err = soil_read_exact(frame + 3, 2, deadline_us);
        if (err != ESP_OK)
        {
            goto unlock;
        }
        uint16_t crc = soil_crc16(frame, 3);
        if (frame[3] != (uint8_t)crc || frame[4] != (uint8_t)(crc >> 8))
        {
            err = ESP_ERR_INVALID_CRC;
            goto unlock;
        }
        ESP_LOGE(TAG, "从站 0x%02X 返回 Modbus 异常码 0x%02X", slave_address, frame[2]);
        err = ESP_ERR_INVALID_RESPONSE;
        goto unlock;
    }

    if (frame[1] != SOIL_READ_FUNCTION || frame[2] != SOIL_RESPONSE_DATA_BYTES)
    {
        err = ESP_ERR_INVALID_RESPONSE;
        goto unlock;
    }

    err = soil_read_exact(frame + 3, SOIL_RESPONSE_DATA_BYTES + 2, deadline_us);
    if (err != ESP_OK)
    {
        goto unlock;
    }

    uint16_t crc = soil_crc16(frame, 3 + SOIL_RESPONSE_DATA_BYTES);
    size_t crc_index = 3 + SOIL_RESPONSE_DATA_BYTES;
    if (frame[crc_index] != (uint8_t)crc || frame[crc_index + 1] != (uint8_t)(crc >> 8))
    {
        err = ESP_ERR_INVALID_CRC;
        goto unlock;
    }

    for (size_t i = 0; i < SOIL_REGISTER_COUNT; ++i)
    {
        size_t data_index = 3 + i * 2;
        registers[i] = ((uint16_t)frame[data_index] << 8) | frame[data_index + 1];
    }
    err = ESP_OK;

unlock:
    {
        esp_err_t unlock_err = bsp_uart_transaction_unlock(BSP_UART_SENSOR);
        return err == ESP_OK ? unlock_err : err;
    }
}

esp_err_t soil_read_all(uint8_t slave_address, soil_data_t *data)
{
    if (data == NULL || slave_address == 0 || slave_address > 247)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t registers[SOIL_REGISTER_COUNT];
    esp_err_t err = soil_read_registers(slave_address, registers);
    if (err != ESP_OK)
    {
        return err;
    }

    // 寄存器 0：温度（0.1℃，16 位补码）；1：湿度（0.1%）；
    // 2-6：EC、盐分、氮、磷、钾；7：pH（放大 10 倍）。
    int32_t temperature_raw = registers[0];
    if (registers[0] & 0x8000)
    {
        temperature_raw -= 0x10000;
    }

    soil_data_t parsed = {
        .temperature_c = temperature_raw / 10.0f,
        .moisture_percent = registers[1] / 10.0f,
        .ec_us_cm = registers[2],
        .salt_ppm = registers[3],
        .nitrogen_mg_kg = registers[4],
        .phosphorus_mg_kg = registers[5],
        .potassium_mg_kg = registers[6],
        .ph = registers[7] / 10.0f,
    };
    *data = parsed;
    return ESP_OK;
}
