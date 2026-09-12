#ifndef     BSP_IIC_H
#define     BSP_IIC_H

#include    <stdint.h>

typedef enum
{
    BSP_IIC1=0,         /* I2C1：PB6(SCL)/PB7(SDA)，留给 OLED/MPU6050 等 */
    BSP_IIC_COUNT       /* 做边界。注：I2C2 的 PB10/PB11 已让位给 USART3(ESP8266) */

} bsp_iic_t;

int bsp_iic_init(bsp_iic_t iicx);

/* DevAddress 传 7 位器件地址（如 0x68），BSP 内部负责左移成 HAL 需要的 8 位形式 */
int bsp_iic_mem_write(bsp_iic_t iicx, uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout);
int bsp_iic_mem_read (bsp_iic_t iicx, uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout);
void bsp_iic_bus_diag(bsp_iic_t iicx);


#endif
