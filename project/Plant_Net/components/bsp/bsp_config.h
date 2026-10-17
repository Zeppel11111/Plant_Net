#ifndef BSP_CONFIG_H
#define BSP_CONFIG_H

/* ESP-IDF UART 端口和 GPIO 类型、枚举定义。 */
#include "driver/gpio.h"
#include "driver/uart.h"

////////////////////////////////////485采集器区域///////////////////////////////////

/* UART1 连接 RS485 转 TTL 转接板，GPIO 编号以 ESP32 一侧为准。 */
#define BSP_SENSOR_UART_PORT              UART_NUM_1
#define BSP_SENSOR_UART_TX_GPIO            GPIO_NUM_17
#define BSP_SENSOR_UART_RX_GPIO            GPIO_NUM_16

/* 手动收发控制：IO18 同时连接 485 芯片的 DE 与 /RE。高电平发送，低电平接收。 */
#define BSP_SENSOR_RS485_DIR_GPIO           GPIO_NUM_18
#define BSP_SENSOR_RS485_TX_LEVEL           1
#define BSP_SENSOR_RS485_RX_LEVEL           0
/* 等待 UART 最后一位发完的最长时间，单位为毫秒。 */
#define BSP_SENSOR_UART_TX_DONE_TIMEOUT_MS 1000

/* 塔石传感器默认串口格式：9600 波特率、8 位数据、无校验、1 位停止位。 */
#define BSP_SENSOR_UART_BAUD_RATE          9600
#define BSP_SENSOR_UART_DATA_BITS          UART_DATA_8_BITS
#define BSP_SENSOR_UART_PARITY             UART_PARITY_DISABLE
#define BSP_SENSOR_UART_STOP_BITS          UART_STOP_BITS_1
#define BSP_SENSOR_UART_FLOW_CTRL          UART_HW_FLOWCTRL_DISABLE

/* UART 驱动环形缓冲区大小，单位为字节。TX 为 0 表示不使用发送环形缓冲区。 */
#define BSP_SENSOR_UART_RX_BUFFER_SIZE     256
#define BSP_SENSOR_UART_TX_BUFFER_SIZE     0


////////////////////////////////////屏幕串口区域///////////////////////////////////

#define BSP_SCREEN_UART_PORT               UART_NUM_2
#define BSP_SCREEN_UART_TX_GPIO            GPIO_NUM_8
#define BSP_SCREEN_UART_RX_GPIO            GPIO_NUM_9

/* 陶晶驰屏幕默认串口格式：9600 波特率、8 位数据、无校验、1 位停止位。 */
#define BSP_SCREEN_UART_BAUD_RATE          9600
#define BSP_SCREEN_UART_DATA_BITS          UART_DATA_8_BITS
#define BSP_SCREEN_UART_PARITY             UART_PARITY_DISABLE
#define BSP_SCREEN_UART_STOP_BITS          UART_STOP_BITS_1
#define BSP_SCREEN_UART_FLOW_CTRL          UART_HW_FLOWCTRL_DISABLE

/* UART 驱动环形缓冲区大小，单位为字节。TX 为 0 表示不使用发送环形缓冲区。 */
#define BSP_SCREEN_UART_RX_BUFFER_SIZE     256
#define BSP_SCREEN_UART_TX_BUFFER_SIZE     0


#endif /* BSP_CONFIG_H */
