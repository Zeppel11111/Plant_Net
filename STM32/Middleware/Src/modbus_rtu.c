#include    <stdint.h>
#include    <string.h>
#include    "modbus_rtu.h"
#include    "app_config.h"     /* 工程内由 app_config.h 统一控开关；独立移植时删除本行 */

/* =====================================================================
 * 模块总开关：CFG_ENABLE_MODBUS = 0 时整个模块不编译，
 * 调用方（传感器驱动）靠 modbus_rtu.h 里的裁剪桩拿到失败返回值。
 * ===================================================================== */
#if CFG_ENABLE_MODBUS

/* ---- CRC16(Modbus) ----
 * 多项式 0xA001（= 0x8005 反序），初值 0xFFFF。
 * 发送时低字节在前：查询帧最后两个字节 = CRC 低字节、高字节。
 */
uint16_t modbus_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t i;
    uint8_t j;

    for (i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/* ---- 构造查询帧（RTU 主站→从站）----
 * 帧格式：[地址1B][功能码1B][起始寄存器2B][寄存器数2B][CRC16 2B] = 8 字节
 */
uint16_t modbus_build_query(uint8_t *frame, uint8_t addr, uint8_t func,
                            uint16_t reg, uint16_t count)
{
    uint16_t crc;

    if (frame == NULL)
    {
        return 0;
    }

    frame[0] = addr;
    frame[1] = func;
    frame[2] = (uint8_t)(reg >> 8);    /* 寄存器地址：高字节在前 */
    frame[3] = (uint8_t)(reg & 0xFF);
    frame[4] = (uint8_t)(count >> 8);  /* 寄存器数量：高字节在前 */
    frame[5] = (uint8_t)(count & 0xFF);

    crc = modbus_crc16(frame, 6);
    frame[6] = (uint8_t)(crc & 0xFF);  /* CRC 低字节 */
    frame[7] = (uint8_t)(crc >> 8);    /* CRC 高字节 */
    return 8;
}

/* ---- 校验响应帧并抽取数据段 ----
 * 返回 0 成功；-1 参数错 / -2 地址不符 / -3 异常响应 / -4 功能码不符
 *     -5 长度不符 / -6 CRC 错误
 */
int modbus_parse_response(const uint8_t *frame, uint16_t len,
                          uint8_t addr, uint8_t func,
                          uint8_t *data, uint16_t max_data)
{
    uint8_t byte_cnt;
    uint16_t crc_recv, crc_calc;

    if (frame == NULL || data == NULL || len < 5)
    {
        return -1;
    }

    /* 1. 从机地址匹配 */
    if (frame[0] != addr)
    {
        return -2;
    }

    /* 2. 异常响应：功能码最高位 = 1 */
    if (frame[1] & MODBUS_EXCEPTION_MASK)
    {
        return -3;
    }
    /* 3. 功能码匹配（正常响应应原样回显） */
    if (frame[1] != func)
    {
        return -4;
    }

    /* 4. 长度校验：addr + func + byte_cnt + 数据 + CRC(2) */
    byte_cnt = frame[2];
    if ((uint16_t)(3 + byte_cnt + 2) != len)
    {
        return -5;
    }
    if (byte_cnt > max_data)
    {
        return -5;   /* 数据段超出调用方缓冲 */
    }

    /* 5. CRC 校验（不含最后两字节 CRC 本身） */
    crc_recv = (uint16_t)(frame[len - 2] | ((uint16_t)frame[len - 1] << 8));
    crc_calc = modbus_crc16(frame, len - 2);
    if (crc_recv != crc_calc)
    {
        return -6;
    }

    /* 6. 拷贝数据段（寄存器值大端：高字节在前） */
    memcpy(data, &frame[3], byte_cnt);
    return 0;
}

#endif /* CFG_ENABLE_MODBUS */
