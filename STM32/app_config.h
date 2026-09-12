#ifndef     APP_CONFIG_H
#define     APP_CONFIG_H

/* =====================================================================
 * app_config.h — 模块插槽背板（软件层面"插拔"设备与算法）
 *
 * 把本文件想成电脑主板的插槽背板：
 *   1 = 插槽上插着卡（对应模块编译进固件）
 *   0 = 空槽（模块整体不编译，头文件自动换成空桩函数）
 *
 * 插槽只有两种类型：
 *   【Driver 层】     器件卡：每颗芯片一个模块（MPU6050、OLED...）
 *   【Middleware 层】 算法卡：跨平台算法中间件（attitude...）
 *
 * 任务（App 层）是主板上的固定走线，不感知插槽状态：
 *   空槽时模块头文件提供空桩函数，任务代码无需任何 #if。
 *
 * 插拔操作 = 改本文件一个数字 → 重新编译 → 烧录。
 *
 * 使用规则：
 *   1. 各源文件先 #include "app_config.h"，再包含各模块头文件
 *   2. 依赖关系：attitude（算法卡）的输入数据来自 MPU6050（器件卡）
 * ===================================================================== */

/* ---------- Driver 插槽：器件卡 ---------- */
#define CFG_ENABLE_MPU6050      0   /* MPU6050 六轴传感器（未接线，驱动未启用） */
#define CFG_ENABLE_OLED         0   /* SSD1306 OLED 128x64（未接线，驱动未启用） */
#define CFG_ENABLE_TAS_GZ       1   /* 塔石温湿度光照传感器（USART2 → RS485 → Modbus-RTU） */
#define CFG_ENABLE_ESP8266      1   /* ESP8266 WiFi 模块（USART3 → AT 指令，115200） */

/* ---------- Middleware 插槽：算法卡 ---------- */
#define CFG_ENABLE_ATTITUDE     0   /* Mahony 姿态解算（依赖 MPU6050 数据） */
#define CFG_ENABLE_MODBUS       1   /* Modbus-RTU 协议栈（塔石传感器依赖） */

/* 字库卡：与 OLED 高度绑定（OLED 显示文字必须用字库），不单独裁剪，
 * 直接共享 OLED 的开关——OLED=1 字库进固件，OLED=0 字库一并剔除。
 * 定义在 Middleware 层但无独立开关：改 CFG_ENABLE_OLED 即同时生效。 */
#define CFG_ENABLE_FONT8X16     CFG_ENABLE_OLED

#endif /* APP_CONFIG_H */
