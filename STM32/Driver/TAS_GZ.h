#ifndef     TAS_GZ_H
#define     TAS_GZ_H

#include    <stdint.h>
#include    "bsp_uart.h"
#include    "app_config.h"     /* 模块总开关：必须先于本文件包含 */

/* =====================================================================
 * 塔石「王字壳温湿度光照强度传感器」驱动（RS485 / Modbus-RTU）
 *
 * 器件卡插槽：CFG_ENABLE_TAS_GZ = 1 编译真驱动，= 0 时空桩。
 * 依赖：modbus_rtu（中间件：帧构造/校验）+ bsp_uart（串口收发）。
 * 接线：USART2 TX/RX → JSM485ESA 自动方向模块 → A/B → 传感器
 * 波特率：9600, 8, N, 1（传感器出厂默认，已在 CubeMX(.ioc) 配好 USART2）
 * 分层约束：驱动只调 BSP 接口，不触碰 HAL 句柄（无穿透）
 * ===================================================================== */

#define   TAS_GZ_ADDR          0x01   /* 出厂默认从机地址 */

/* 寄存器地址（手册 4.3 节） */
#define   TAS_GZ_REG_HUMI      0x0000 /* 湿度：值÷10 = %RH */
#define   TAS_GZ_REG_TEMP      0x0001 /* 温度：值÷10 = ℃（负数补码） */
#define   TAS_GZ_REG_LUX       0x0007 /* 光照：值 = LUX */

/* 器件句柄：记录挂在哪路串口 */
typedef struct{
    bsp_uartx_t uartx;
} TAS_GZ_t;

/* 一次采集的数据：物理量已除 10 的用 *10 整数表示（0.1 分辨率） */
typedef struct{
    int16_t  temp_x10;   /* 温度：-101 → -10.1 ℃ */
    uint16_t humi_x10;   /* 湿度：658 → 65.8 %RH */
    uint16_t lux;        /* 光照：1027 → 1027 LUX */
} TAS_GZ_data_t;

#if CFG_ENABLE_TAS_GZ

/* 初始化：认领串口（波特率 9600 由 CubeMX 配置）；成功返回 0 */
int  TAS_GZ_init(bsp_uartx_t uartx);

/* 读取温湿度 + 光照（两次 Modbus 查询）；成功返回 0 */
int  TAS_GZ_read(TAS_GZ_data_t *d);

#else
/* ===== 裁剪桩：开关=0 时模块不编译，调用点无需 #if，直接编成失败 ===== */
static inline int TAS_GZ_init(bsp_uartx_t uartx)             { (void)uartx; return -1; }
static inline int TAS_GZ_read(TAS_GZ_data_t *d)              { (void)d; return -1; }
#endif /* CFG_ENABLE_TAS_GZ */

#endif
