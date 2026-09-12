#ifndef     BSP_UART_H
#define     BSP_UART_H

#include    <stdint.h>

typedef enum
{
    BSP_UART1=0,        // 调试口（115200）
    BSP_UART2,          // RS485 传感器（9600）
    BSP_UART3,          // ESP8266（115200）
    BSP_UART_COUNT      // 做边界

} bsp_uartx_t;

typedef enum
{
    BSP_UART_MODE_POLLING=0,

} bsp_uart_mode_t;

int bsp_uart_init(bsp_uartx_t uartx);
int bsp_uart_send(bsp_uartx_t uartx, uint8_t *pData, uint16_t Size, uint32_t Timeout, bsp_uart_mode_t mode);
int bsp_uart_rec_line(bsp_uartx_t uartx, uint8_t *pData, uint16_t Size, uint32_t Timeout, bsp_uart_mode_t mode);

/* 按"帧"接收（Modbus-RTU 等二进制帧）：逐字节收，字节间隔超过 IdleGap 毫秒视为帧结束。
 * RTOS 友好：内部每 10ms 分片轮询，最坏单次阻塞 10ms。
 * 返回实际收到的字节数（0~Size）；总超时后：已收到数据返回字节数，一字节未收返回 -2；
 * -1 参数错 */
int bsp_uart_rec_frame(bsp_uartx_t uartx, uint8_t *pData, uint16_t Size,
                       uint32_t ByteTimeout, uint32_t IdleGap);

#endif