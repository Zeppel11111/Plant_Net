#include    <stdint.h>
#include    "TAS_GZ.h"
#include    "bsp_uart.h"
#include    "modbus_rtu.h"
#include    "Debug.h"

/* =====================================================================
 * 模块总开关：CFG_ENABLE_TAS_GZ = 0 时整个模块不编译，
 * 调用点（freertos.c）靠 TAS_GZ.h 里的裁剪桩编译通过。
 * 分层约束：本文件只允许调用 BSP 接口（bsp_uart_*），
 * 不直接触碰 HAL 句柄——USART2 波特率 9600 在 CubeMX（.ioc）配置。
 * ===================================================================== */
#if CFG_ENABLE_TAS_GZ

static const char* TAG = "TAS_GZ";

/* ---- 器件句柄 ---- */
static TAS_GZ_t TAS_GZ = {
    .uartx = BSP_UART_COUNT,   /* 哨兵值：调用 TAS_GZ_init 之前表示未指定串口 */
};

/* 内部：发送查询帧并等待响应，响应帧存 rsp，返回响应长度（>0 成功） */
static int tas_gz_transact(uint8_t *query, uint16_t qlen,
                           uint8_t *rsp, uint16_t rsp_max)
{
    int n;

    if (bsp_uart_send(TAS_GZ.uartx, query, qlen, 1000, BSP_UART_MODE_POLLING) != 0)
    {
        LOG_E(TAG, "查询帧发送失败");
        return -1;
    }

    /* 9600 波特率下 1 字节 ≈ 1.04ms，3.5 字符帧间隙 ≈ 3.7ms，取 5ms 保险 */
    n = bsp_uart_rec_frame(TAS_GZ.uartx, rsp, rsp_max, 200, 5);
    if (n <= 0)
    {
        LOG_E(TAG, "无响应（检查 A/B 接线、电源、从机地址）");
        return -2;
    }
    return n;
}

int TAS_GZ_init(bsp_uartx_t uartx)
{
    TAS_GZ.uartx = uartx;

    /* 波特率 9600 已由 CubeMX(.ioc) 配置到 USART2，BSP 只负责认领 */
    if (bsp_uart_init(TAS_GZ.uartx) != 0)
    {
        LOG_E(TAG, "串口初始化失败");
        return -1;
    }
    LOG_I(TAG, "USART2 已初始化（9600 8N1）");

    /* 探活：读一次温湿度，验证链路 */
    {
        TAS_GZ_data_t d;
        if (TAS_GZ_read(&d) == 0)
        {
            LOG_I(TAG, "传感器在线：%d.%d℃ %d.%d%%RH %d LUX",
                  d.temp_x10 / 10, (d.temp_x10 < 0 ? -d.temp_x10 : d.temp_x10) % 10,
                  d.humi_x10 / 10, d.humi_x10 % 10, d.lux);
            return 0;
        }
        LOG_E(TAG, "传感器未应答，检查接线/供电/地址");
        return -3;
    }
}

int TAS_GZ_read(TAS_GZ_data_t *d)
{
    uint8_t query[8], rsp[MODBUS_FRAME_MAX];
    uint8_t payload[4];
    int n;
    int16_t temp;
    uint16_t humi, lux;

    if (d == NULL)
    {
        return -1;
    }
    if (TAS_GZ.uartx >= BSP_UART_COUNT)
    {
        return -1;   /* 还没调用 TAS_GZ_init */
    }

    /* ---- 查询 1：温湿度（寄存器 0x0000 起 2 个，一次拿两个）----
     * 帧：01 03 00 00 00 02 CRC
     * 响应：01 03 04 [湿度H][湿度L][温度H][温度L] CRC */
    n = modbus_build_query(query, TAS_GZ_ADDR, MODBUS_FUNC_READ_HOLDING,
                           TAS_GZ_REG_HUMI, 2);
    n = tas_gz_transact(query, (uint16_t)n, rsp, sizeof(rsp));
    if (n < 0)
    {
        return -2;
    }
    if (modbus_parse_response(rsp, (uint16_t)n, TAS_GZ_ADDR,
                              MODBUS_FUNC_READ_HOLDING, payload, sizeof(payload)) != 0)
    {
        LOG_E(TAG, "温湿度响应校验失败");
        return -3;
    }
    humi = (uint16_t)(((uint16_t)payload[0] << 8) | payload[1]);   /* 大端 */
    temp = (int16_t)(((uint16_t)payload[2] << 8) | payload[3]);    /* 负数补码 */

    /* ---- 查询 2：光照（寄存器 0x0007 起 1 个）----
     * 响应：01 03 02 [光照H][光照L] CRC */
    n = modbus_build_query(query, TAS_GZ_ADDR, MODBUS_FUNC_READ_HOLDING,
                           TAS_GZ_REG_LUX, 1);
    n = tas_gz_transact(query, (uint16_t)n, rsp, sizeof(rsp));
    if (n < 0)
    {
        return -4;
    }
    if (modbus_parse_response(rsp, (uint16_t)n, TAS_GZ_ADDR,
                              MODBUS_FUNC_READ_HOLDING, payload, 2) != 0)
    {
        LOG_E(TAG, "光照响应校验失败");
        return -5;
    }
    lux = (uint16_t)(((uint16_t)payload[0] << 8) | payload[1]);

    d->temp_x10 = temp;
    d->humi_x10 = humi;
    d->lux      = lux;
    return 0;
}

#endif /* CFG_ENABLE_TAS_GZ */
