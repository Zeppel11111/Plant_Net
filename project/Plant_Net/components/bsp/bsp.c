#include "bsp.h"
#include "bsp_config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "BSP_UART";
static SemaphoreHandle_t s_uart_transaction_mutex[2];

static uart_port_t bsp_uart_port(bsp_uart_id_t id)
{
    switch (id)
    {
    case BSP_UART_SENSOR:
        return BSP_SENSOR_UART_PORT;
    case BSP_UART_SCREEN:
        return BSP_SCREEN_UART_PORT;
    default:
        return UART_NUM_MAX;
    }
}

static SemaphoreHandle_t bsp_uart_transaction_mutex_for_id(bsp_uart_id_t id)
{
    switch (id)
    {
    case BSP_UART_SENSOR:
        return s_uart_transaction_mutex[0];
    case BSP_UART_SCREEN:
        return s_uart_transaction_mutex[1];
    default:
        return NULL;
    }
}

/**bsp统一初始化IO口配置，配置信息修改在 "bsp_config.h" 中
 *
 */
esp_err_t bsp_port_init(void)
{

    esp_err_t err;

    for (size_t i = 0; i < sizeof(s_uart_transaction_mutex) / sizeof(s_uart_transaction_mutex[0]); ++i)
    {
        if (s_uart_transaction_mutex[i] == NULL)
        {
            s_uart_transaction_mutex[i] = xSemaphoreCreateMutex();
            if (s_uart_transaction_mutex[i] == NULL)
            {
                return ESP_ERR_NO_MEM;
            }
        }
    }

    ////////////////////////// 串口1的初始化/////////////////////////////////////
    // 先把 485 方向脚置为接收态，避免启动时占用总线。
    err = gpio_set_level(BSP_SENSOR_RS485_DIR_GPIO, BSP_SENSOR_RS485_RX_LEVEL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "485 方向脚预置接收态失败：%s", esp_err_to_name(err));
        return err;
    }
    const gpio_config_t rs485_dir_config = {
        .pin_bit_mask = 1ULL << BSP_SENSOR_RS485_DIR_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&rs485_dir_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "485 方向脚 GPIO%d 初始化失败：%s",
                 BSP_SENSOR_RS485_DIR_GPIO, esp_err_to_name(err));
        return err;
    }

    const uart_port_t uart_num = BSP_SENSOR_UART_PORT;

    uart_config_t uart1_config = {
        .baud_rate = BSP_SENSOR_UART_BAUD_RATE,
        .data_bits = BSP_SENSOR_UART_DATA_BITS,
        .parity = BSP_SENSOR_UART_PARITY,
        .stop_bits = BSP_SENSOR_UART_STOP_BITS,
        .flow_ctrl = BSP_SENSOR_UART_FLOW_CTRL};

    err = uart_param_config(uart_num, &uart1_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "UART1 参数配置失败：%s", esp_err_to_name(err));
        return err;
    }

    // Set UART pins(TX: , RX:, RTS: , CTS:)
    err = uart_set_pin(uart_num, BSP_SENSOR_UART_TX_GPIO, BSP_SENSOR_UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "UART1 引脚映射失败：%s", esp_err_to_name(err));
        return err;
    }

    // 安装 UART 驱动，当前测试不使用事件队列
    err = uart_driver_install(uart_num, BSP_SENSOR_UART_RX_BUFFER_SIZE,
                              BSP_SENSOR_UART_TX_BUFFER_SIZE, 0, NULL, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "UART1 驱动安装失败：%s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "传感器 UART%d 已初始化：TX=GPIO%d，RX=GPIO%d，485 方向=GPIO%d，波特率=%d，8N1",
             uart_num, BSP_SENSOR_UART_TX_GPIO, BSP_SENSOR_UART_RX_GPIO, BSP_SENSOR_RS485_DIR_GPIO,
             BSP_SENSOR_UART_BAUD_RATE);

    ///////////////////////////////////串口2初始化///////////////////////////////
    const uart_port_t uart2_num = BSP_SCREEN_UART_PORT;

    uart_config_t uart2_config = {
        .baud_rate = BSP_SCREEN_UART_BAUD_RATE,
        .data_bits = BSP_SCREEN_UART_DATA_BITS,
        .parity = BSP_SCREEN_UART_PARITY,
        .stop_bits = BSP_SCREEN_UART_STOP_BITS,
        .flow_ctrl = BSP_SCREEN_UART_FLOW_CTRL};

    err = uart_param_config(uart2_num, &uart2_config);
    if (err != ESP_OK)
    {
        return err;
    }

    // Set UART pins(TX: , RX:, RTS: , CTS:)
    err = uart_set_pin(uart2_num, BSP_SCREEN_UART_TX_GPIO, BSP_SCREEN_UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        return err;
    }

    // 安装 UART 驱动，当前测试不使用事件队列
    err = uart_driver_install(uart2_num, BSP_SCREEN_UART_RX_BUFFER_SIZE,
                              BSP_SCREEN_UART_TX_BUFFER_SIZE, 0, NULL, 0);
    if (err != ESP_OK)
    {
        return err;
    }
    return ESP_OK;
}

int bsp_uart_send(bsp_uart_id_t id, const uint8_t *data, int length)
{
    uart_port_t uart_num = bsp_uart_port(id);
    if (uart_num == UART_NUM_MAX || length < 0 || (length > 0 && data == NULL))
    {
        return -1;
    }
    if (length == 0)
    {
        return 0;
    }
    if (id != BSP_UART_SENSOR)
    {
        return uart_write_bytes(uart_num, data, (size_t)length);
    }

    // 只有传感器 UART 接了手动方向控制的 485 芯片。
    esp_err_t err = gpio_set_level(BSP_SENSOR_RS485_DIR_GPIO, BSP_SENSOR_RS485_TX_LEVEL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "485 切换发送态失败：%s", esp_err_to_name(err));
        return -1;
    }

    int sent = uart_write_bytes(uart_num, data, (size_t)length);
    // uart_write_bytes 返回时，最后一位可能还在 UART 移位寄存器里。
    // 必须等实际发送结束，才能关闭 485 驱动器并切换回接收态。
    esp_err_t tx_err = sent >= 0
                           ? uart_wait_tx_done(uart_num, pdMS_TO_TICKS(BSP_SENSOR_UART_TX_DONE_TIMEOUT_MS))
                           : ESP_FAIL;
    esp_err_t rx_err = gpio_set_level(BSP_SENSOR_RS485_DIR_GPIO, BSP_SENSOR_RS485_RX_LEVEL);
    if (tx_err != ESP_OK)
    {
        ESP_LOGE(TAG, "UART1 等待发送完成失败：%s", esp_err_to_name(tx_err));
    }
    if (rx_err != ESP_OK)
    {
        ESP_LOGE(TAG, "485 切换接收态失败：%s", esp_err_to_name(rx_err));
    }
    return (tx_err == ESP_OK && rx_err == ESP_OK) ? sent : -1;
}

int bsp_uart_receive(bsp_uart_id_t id, uint8_t *data, uint32_t size, uint32_t timeout_ms)
{
    uart_port_t uart_num = bsp_uart_port(id);
    if (uart_num == UART_NUM_MAX || data == NULL || size == 0)
    {
        return -1;
    }

    return uart_read_bytes(uart_num, data, size, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t bsp_uart_clear_rx(bsp_uart_id_t id)
{
    uart_port_t uart_num = bsp_uart_port(id);
    if (uart_num == UART_NUM_MAX)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (id == BSP_UART_SENSOR)
    {
        size_t buffered_bytes = 0;
        esp_err_t count_err = uart_get_buffered_data_len(uart_num, &buffered_bytes);
        if (count_err == ESP_OK && buffered_bytes > 0)
        {
            ESP_LOGW(TAG, "新查询前 UART1 接收缓冲区还有 %u 字节，将丢弃",
                     (unsigned)buffered_bytes);
        }
    }
    return uart_flush_input(uart_num);
}

esp_err_t bsp_uart_transaction_lock(bsp_uart_id_t id, uint32_t timeout_ms)
{
    SemaphoreHandle_t mutex = bsp_uart_transaction_mutex_for_id(id);
    if (mutex == NULL)
    {
        return (id == BSP_UART_SENSOR || id == BSP_UART_SCREEN) ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
    }

    return xSemaphoreTake(mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t bsp_uart_transaction_unlock(bsp_uart_id_t id)
{
    SemaphoreHandle_t mutex = bsp_uart_transaction_mutex_for_id(id);
    if (mutex == NULL)
    {
        return (id == BSP_UART_SENSOR || id == BSP_UART_SCREEN) ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
    }

    return xSemaphoreGive(mutex) == pdTRUE ? ESP_OK : ESP_FAIL;
}
