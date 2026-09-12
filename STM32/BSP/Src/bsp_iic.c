#include    <stdint.h>
#include    "bsp_iic.h"
#include    "Debug.h"
#include    "i2c.h"

static const char* TAG ="bsp_iic";

/* 把 HAL 的 ErrorCode 翻译成可读字符串，方便定位失败原因 */
static const char* i2c_err_str(uint32_t ec)
{
    if (ec & HAL_I2C_ERROR_AF)      return "NACK(无应答:地址错/接反/无上拉/未供电)";
    if (ec & HAL_I2C_ERROR_BERR)    return "BERR(总线错误)";
    if (ec & HAL_I2C_ERROR_ARLO)    return "ARLO(仲裁丢失)";
    if (ec & HAL_I2C_ERROR_TIMEOUT) return "TIMEOUT(超时:SCL被从机拉死)";
    if (ec & HAL_I2C_ERROR_OVR)     return "OVR(溢出)";
    return "未知";
}

//指针常量数组，对接底层句柄实体（只有 I2C1：I2C2 引脚已让位给 USART3）
static I2C_HandleTypeDef *const bsp_iic_handle[BSP_IIC_COUNT] = {
    &hi2c1
};

/* 初始化函数表：默认参数来自 CubeMX，BSP 只负责"认领" */
typedef void (*bsp_iic_init_fn_t)(void);
static const bsp_iic_init_fn_t bsp_iic_init_fn[BSP_IIC_COUNT] = {
    MX_I2C1_Init
};

int bsp_iic_init(bsp_iic_t iicx)
{
    if(iicx >=BSP_IIC_COUNT)
    {
        LOG_E(TAG,"参数越界");
        return -1;//参数越界
    }

    bsp_iic_init_fn[iicx]();

    LOG_D(TAG,"iic_%d 初始化成功",((int)iicx)+1);
    return 0;
}

/* 写寄存器：DevAddress 传 7 位地址，内部左移成 HAL 需要的 8 位形式 */
int bsp_iic_mem_write(bsp_iic_t iicx, uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
    if(iicx >=BSP_IIC_COUNT || pData == NULL)
    {
        LOG_E(TAG,"参数越界");
        return -1;//参数越界
    }

    uint16_t addr_left = ((uint16_t)DevAddress)<<1;   // 7 位地址 -> 8 位形式

    if(HAL_I2C_Mem_Write(bsp_iic_handle[iicx], addr_left, MemAddress, MemAddSize, pData, Size, Timeout)!= HAL_OK)
    {
        LOG_E(TAG,"iic_%d 写失败 %s",((int)iicx)+1, i2c_err_str(bsp_iic_handle[iicx]->ErrorCode));
        return -2;//写入失败
    }
    return 0;
}

/* 读寄存器：DevAddress 传 7 位地址，内部左移成 HAL 需要的 8 位形式 */
int bsp_iic_mem_read(bsp_iic_t iicx, uint16_t DevAddress, uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
    if(iicx >=BSP_IIC_COUNT || pData == NULL)
    {
        LOG_E(TAG,"参数越界");
        return -1;//参数越界
    }

    uint16_t addr_left = ((uint16_t)DevAddress)<<1;   // 7 位地址 -> 8 位形式

    if(HAL_I2C_Mem_Read(bsp_iic_handle[iicx], addr_left, MemAddress, MemAddSize, pData, Size, Timeout)!= HAL_OK)
    {
        LOG_E(TAG,"iic_%d 读失败 %s",((int)iicx)+1, i2c_err_str(bsp_iic_handle[iicx]->ErrorCode));
        return -2;//读取失败
    }
    return 0;
}

/* 总线诊断：打印 BUSY 标志与 SCL/SDA 引脚实际电平，用于定位"总线卡死" */
void bsp_iic_bus_diag(bsp_iic_t iicx)
{
    uint32_t busy;
    GPIO_TypeDef* port;
    uint16_t scl_pin;
    uint16_t sda_pin;
    GPIO_PinState scl;
    GPIO_PinState sda;

    if(iicx >= BSP_IIC_COUNT)
    {
        return;
    }

    busy = __HAL_I2C_GET_FLAG(bsp_iic_handle[iicx], I2C_FLAG_BUSY);

    /* 只有 I2C1（PB6 SCL / PB7 SDA）了：I2C2 引脚已让位给 USART3 */
    port    = GPIOB;
    scl_pin = GPIO_PIN_6;
    sda_pin = GPIO_PIN_7;

    scl = HAL_GPIO_ReadPin(port, scl_pin);
    sda = HAL_GPIO_ReadPin(port, sda_pin);

    LOG_I(TAG, "总线诊断 BUSY=%lu SCL=%d SDA=%d (1=高/有上拉, 0=低/被拉死)",
          (unsigned long)busy, (int)scl, (int)sda);
}
