#ifndef     MODBUS_RTU_H
#define     MODBUS_RTU_H

#include    <stdint.h>
#include    "app_config.h"     /* 模块总开关：必须先于本文件包含 */

/* =====================================================================
 * Modbus-RTU 协议栈（中间件：纯算法，不依赖任何硬件）
 *
 * 插槽模型：CFG_ENABLE_MODBUS = 1 编译协议实现，= 0 时空槽，
 * 调用方（传感器驱动）靠裁剪桩拿到失败返回值。
 * 独立移植时默认启用（照 attitude.h 模板）。
 *
 * 本模块只做"帧的构造/校验/解析"，不碰串口——
 * 收发由调用方通过 BSP 串口完成，协议与硬件彻底解耦。
 * ===================================================================== */

#ifndef CFG_ENABLE_MODBUS
#define CFG_ENABLE_MODBUS       1   /* 独立移植时默认启用 */
#endif

/* ---- 常用功能码 ---- */
#define MODBUS_FUNC_READ_HOLDING   0x03   /* 读保持寄存器（塔石传感器用它） */
#define MODBUS_FUNC_READ_INPUT     0x04   /* 读输入寄存器 */
#define MODBUS_FUNC_WRITE_SINGLE   0x06   /* 写单个寄存器 */
#define MODBUS_FUNC_WRITE_MULTI    0x10   /* 写多个寄存器 */

/* 异常码（响应帧功能码最高位置 1 表示异常） */
#define MODBUS_EXCEPTION_MASK      0x80

/* 帧长上限：单条 RTU 帧最大 256 字节（地址1+功能码1+数据254） */
#define MODBUS_FRAME_MAX           256

#if CFG_ENABLE_MODBUS

/* CRC16(Modbus)：多项式 0xA001，返回校验值（低字节在前发送） */
uint16_t modbus_crc16(const uint8_t *data, uint16_t len);

/* 构造查询帧：addr 从机地址 | func 功能码 | reg 起始寄存器 | count 寄存器数量
 * 输出到 frame（长度固定 8 字节），返回帧长度；frame 为 NULL 返回 0 */
uint16_t modbus_build_query(uint8_t *frame, uint8_t addr, uint8_t func,
                            uint16_t reg, uint16_t count);

/* 校验响应帧并抽出数据段：
 *   frame/len 收到的完整响应帧（含 CRC）
 *   addr/func 期望的从机地址与功能码（func 不含异常位）
 *   data 输出寄存器数据段（大端序），max_data 为其容量
 * 返回 0 成功；负值见返回值注释 */
int modbus_parse_response(const uint8_t *frame, uint16_t len,
                          uint8_t addr, uint8_t func,
                          uint8_t *data, uint16_t max_data);

#else
/* ===== 裁剪桩：空槽时驱动收到失败返回值，自行降级 ===== */
static inline uint16_t modbus_crc16(const uint8_t *data, uint16_t len) { (void)data; (void)len; return 0; }
static inline uint16_t modbus_build_query(uint8_t *frame, uint8_t addr, uint8_t func, uint16_t reg, uint16_t count)
{ (void)frame; (void)addr; (void)func; (void)reg; (void)count; return 0; }
static inline int modbus_parse_response(const uint8_t *frame, uint16_t len, uint8_t addr, uint8_t func, uint8_t *data, uint16_t max_data)
{ (void)frame; (void)len; (void)addr; (void)func; (void)data; (void)max_data; return -1; }
#endif /* CFG_ENABLE_MODBUS */

#endif /* MODBUS_RTU_H */
