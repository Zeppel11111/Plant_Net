/**
  ******************************************************************************
  * @file    bsp.h
  * @brief   BSP 板级支持包统一入口
  *
  * 使用说明：
  *   - 业务层（App）只需 #include "bsp.h"，即可获得所有 BSP 模块 API。
  *   - 新增模块（bsp_uart / bsp_led / bsp_delay ...）时，在下方统一 include。
  *   - 本目录由用户维护，CubeMX 重新生成代码不会覆盖。
  ******************************************************************************
  */
#ifndef __BSP_H
#define __BSP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* BSP 版本号（开源发布时递增） */
#define BSP_VERSION_MAJOR   1U
#define BSP_VERSION_MINOR   0U
#define BSP_VERSION_PATCH   0U

/* 本 BSP 依赖的时钟假设（必须与 CubeMX Clock Configuration 保持一致） */
#define BSP_SYSCLK_FREQ     72000000U   /* HSE 8MHz -> PLL x9 -> 72MHz       */
#define BSP_HCLK_FREQ       72000000U   /* AHB  /1                           */
#define BSP_APB1_FREQ       36000000U   /* APB1 /2（USART2/3、I2C、TIM2~7 在此总线）*/
#define BSP_APB2_FREQ       72000000U   /* APB2 /1（USART1、SPI1、ADC 在此总线）   */

/* ---- 模块头文件（新增模块时在这里统一 include）---- */
/* #include "bsp_delay.h" */
/* #include "bsp_led.h"   */
/* #include "bsp_key.h"   */
/* #include "bsp_uart.h"  */
/* #include "bsp_i2c.h"   */

#ifdef __cplusplus
}
#endif

#endif /* __BSP_H */
