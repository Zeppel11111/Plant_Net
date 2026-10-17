#ifndef BSP_H
#define BSP_H

#include "esp_err.h"
#include <stdint.h>

// 应用层只选择设备用途，具体 UART 端口和引脚由 BSP 配置。
typedef enum {
    BSP_UART_SENSOR,
    BSP_UART_SCREEN,
} bsp_uart_id_t;

// BSP 提供给上层的串口接口。
esp_err_t bsp_port_init(void);
int bsp_uart_send(bsp_uart_id_t id, const uint8_t *data, int length);
int bsp_uart_receive(bsp_uart_id_t id, uint8_t *data, uint32_t size, uint32_t timeout_ms);
esp_err_t bsp_uart_clear_rx(bsp_uart_id_t id);
esp_err_t bsp_uart_transaction_lock(bsp_uart_id_t id, uint32_t timeout_ms);
esp_err_t bsp_uart_transaction_unlock(bsp_uart_id_t id);

#endif /* BSP_H */
