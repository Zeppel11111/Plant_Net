#include    <stdint.h>
#include    "bsp_uart.h"
#include    "usart.h"

static UART_HandleTypeDef *const bsp_uart_handle[BSP_UART_COUNT] = {
    &huart1,
    &huart2,
    &huart3
};

/* 初始化函数表：默认参数来自 CubeMX，BSP 只负责"认领" */
typedef void (*bsp_uart_init_fn_t)(void);
static const bsp_uart_init_fn_t bsp_uart_init_fn[BSP_UART_COUNT] = {
    MX_USART1_UART_Init,
    MX_USART2_UART_Init,
    MX_USART3_UART_Init,
};



int bsp_uart_init(bsp_uartx_t uartx)
{
    if(uartx >= BSP_UART_COUNT )
    {
        return -1;  //参数越界
    }

    bsp_uart_init_fn[uartx]();   // 自动对接：BSP_UART1 → MX_USART1_UART_Init
    return 0;

}



int bsp_uart_send(bsp_uartx_t uartx, uint8_t *pData, uint16_t Size, uint32_t Timeout, bsp_uart_mode_t mode)
{
    if(uartx >= BSP_UART_COUNT )
    {
        return -1;  //参数越界
    }

    switch(mode)
    {
        case BSP_UART_MODE_POLLING:
            if(HAL_UART_Transmit(bsp_uart_handle[uartx], pData, Size, Timeout) != HAL_OK)
            {
                return -2;  //发送失败
            }
            break;
        default:
                return -3;   // 不支持的模式
    }
    return 0;
}

int bsp_uart_rec_line(bsp_uartx_t uartx, uint8_t *pData,uint16_t Size, uint32_t Timeout, bsp_uart_mode_t mode)
{
    uint16_t i;

    if(uartx >= BSP_UART_COUNT )
    {
        return -1;  //参数越界
    }

    switch(mode)
    {
        case BSP_UART_MODE_POLLING:
            for(i = 0; i < Size - 1; i++)
            {
                if(HAL_UART_Receive(bsp_uart_handle[uartx], &pData[i], 1, Timeout) != HAL_OK)
                {
                    pData[i] = '\0';           // 超时收尾，防止脏数据
                    return -2;  //接收失败
                }
                if(pData[i] == '\n')
                {
                    if(i > 0 && pData[i-1] == '\r')  // 剥掉 \r
                    {
                        i--;
                    }
                    pData[i] = '\0';
                    return 0;
                }
            }
            pData[Size - 1] = '\0';            // 收满还没换行，强制收尾防越界
            return 0;
        default:
            return -3;   // 不支持的模式
    }
}

/* 按"帧"接收（Modbus-RTU 等二进制帧）：
 * 逐字节轮询接收，两次字节间隔超过 IdleGap 毫秒即认为一帧收完。
 * Modbus-RTU 规范：帧间间隔 >= 3.5 字符时间（9600 波特率下约 4ms）。
 *
 * RTOS 友好设计：每次 HAL_UART_Receive 只等 10ms（BSP_UART_POLL_SLICE），
 * 通过循环累计到 ByteTimeout。这样最坏单次阻塞 10ms，不会把任务
 * 卡死数百毫秒（否则传感器无响应时一次查询最多占 CPU 200ms）。
 *
 * 返回实际字节数；-1 参数错 / -2 超时未收到任何字节 */
#define BSP_UART_POLL_SLICE_MS   10

int bsp_uart_rec_frame(bsp_uartx_t uartx, uint8_t *pData, uint16_t Size,
                       uint32_t ByteTimeout, uint32_t IdleGap)
{
    uint16_t n = 0;
    uint32_t t_start, t_last;

    if (uartx >= BSP_UART_COUNT || pData == NULL || Size == 0)
    {
        return -1;
    }

    t_start = HAL_GetTick();
    t_last  = t_start;

    while (n < Size)
    {
        if (HAL_UART_Receive(bsp_uart_handle[uartx], &pData[n], 1,
                             BSP_UART_POLL_SLICE_MS) == HAL_OK)
        {
            n++;
            t_last = HAL_GetTick();   /* 收到字节：刷新"最后活跃时刻" */
        }
        else if (n > 0 && (HAL_GetTick() - t_last) >= IdleGap)
        {
            break;   /* 帧间隙超时：一帧收完 */
        }
        else if ((HAL_GetTick() - t_start) >= ByteTimeout)
        {
            return (n > 0) ? (int)n : -2;   /* 总超时：收到过就返回已收的，否则报无响应 */
        }
    }
    return (int)n;
}
