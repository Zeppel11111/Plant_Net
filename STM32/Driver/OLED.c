#include    <stdint.h>
#include    "OLED.h"
#include    "font8x16.h"
#include    "bsp_iic.h"
#include    "i2c.h"
#include    "Debug.h"

/* =====================================================================
 * 模块总开关：CFG_ENABLE_OLED = 0 时整个模块不编译，
 * 调用点（freertos.c）靠 OLED.h 里的裁剪桩编译通过。
 * ===================================================================== */
#if CFG_ENABLE_OLED

static const char* TAG = "OLED";

/* ---- 器件句柄：记录该器件挂在哪路 I2C 以及探测到的 7 位地址 ---- */
static OLED_t OLED = {
    .iicx = BSP_IIC_COUNT,   /* 哨兵值：调用 OLED_init 之前表示未指定总线 */
    .dev_addr = 0,
};

/* ---- 显存缓冲：8 页 x 128 列，全部操作先改这里，再整屏刷新 ---- */
static uint8_t OLED_fb[OLED_PAGE_NUM][OLED_WIDTH] = {0};

/* =====================================================================
 * SSD1306 通过 I2C 收发数据时，每个字节流前都要带 1 字节"控制字节"：
 *   0x00 —— 后续字节都是命令（D/C#=0）
 *   0x40 —— 后续字节都是显示数据（D/C#=1）
 * bsp_iic_mem_write 的 MemAddress 恰好就是这一字节，因此：
 *   写命令：mem_write(addr, MemAddress=0x00, cmd)
 *   写数据：mem_write(addr, MemAddress=0x40, data)
 * ===================================================================== */

/* 内部：发送一条命令 */
static int oled_write_cmd(uint8_t cmd)
{
    return bsp_iic_mem_write(OLED.iicx, OLED.dev_addr, 0x00, I2C_MEMADD_SIZE_8BIT, &cmd, 1, 1000);
}

/* 内部：按给定 7 位地址探测器件是否存在（写 NOP 命令看是否有 ACK） */
static int oled_probe(bsp_iic_t iicx, uint16_t dev_addr)
{
    uint8_t nop = OLED_CMD_NOP;

    if (bsp_iic_mem_write(iicx, dev_addr, 0x00, I2C_MEMADD_SIZE_8BIT, &nop, 1, 1000) != 0)
    {
        return -1;   /* 总线无应答：器件不在这个地址 */
    }
    return 0;
}

int OLED_init(bsp_iic_t iicx)
{
    /* SSD1306 上电初始化序列：逐条命令写入 */
    static const uint8_t init_cmds[] = {
        OLED_CMD_OFF,          /* 0xAE 先关显示 */
        0xD5, 0x80,            /* 时钟分频 */
        0xA8, 0x3F,            /* 多路比：64 行 */
        0xD3, 0x00,            /* 显示偏移 0 */
        0x40,                  /* 起始行 0 */
        0x8D, 0x14,            /* 电荷泵开（I2C 版必须开，否则无显示） */
        0x20, 0x02,            /* 页寻址模式 */
        0xA1,                  /* 段重映射：列 127 -> SEG0 */
        0xC8,                  /* COM 扫描方向：反扫 */
        0xDA, 0x12,            /* COM 引脚配置（128x64 用 0x12） */
        0x81, 0xCF,            /* 对比度 */
        0xD9, 0xF1,            /* 预充电周期 */
        0xDB, 0x40,            /* VCOMH 电平 */
        0xA4,                  /* 按 RAM 内容显示 */
        0xA6,                  /* 正常显示（非反色） */
        OLED_CMD_ON,           /* 0xAF 开显示 */
    };
    uint8_t i;

    OLED.iicx = iicx;

    if (bsp_iic_init(OLED.iicx) != 0)
    {
        LOG_E(TAG, "I2C 初始化失败");
        return -1;
    }

    /* 0. 总线诊断：打印 BUSY 与 SCL/SDA 电平，判断是否总线卡死/缺上拉 */
    bsp_iic_bus_diag(OLED.iicx);

    /* 1. 探测器件地址：先 SA0=0(0x3C)，无应答再试 SA0=1(0x3D) */
    if (oled_probe(OLED.iicx, OLED_ADDR1) == 0)
    {
        OLED.dev_addr = OLED_ADDR1;
        LOG_I(TAG, "检测到 OLED @ 0x3C");
    }
    else if (oled_probe(OLED.iicx, OLED_ADDR2) == 0)
    {
        OLED.dev_addr = OLED_ADDR2;
        LOG_I(TAG, "检测到 OLED @ 0x3D");
    }
    else
    {
        LOG_E(TAG, "未检测到 OLED，请检查 SDA/SCL 接线与上拉电阻");
        return -2;
    }

    /* 2. 初始化序列 */
    for (i = 0; i < sizeof(init_cmds); i++)
    {
        if (oled_write_cmd(init_cmds[i]) != 0)
        {
            LOG_E(TAG, "初始化命令发送失败");
            return -3;
        }
    }

    /* 3. 清屏并上屏 */
    OLED_clear();
    OLED_refresh();

    LOG_I(TAG, "OLED 初始化完成");
    return 0;
}

void OLED_clear(void)
{
    uint8_t page, col;

    for (page = 0; page < OLED_PAGE_NUM; page++)
    {
        for (col = 0; col < OLED_WIDTH; col++)
        {
            OLED_fb[page][col] = 0x00;
        }
    }
}

void OLED_refresh(void)
{
    uint8_t page, cmd[3];

    for (page = 0; page < OLED_PAGE_NUM; page++)
    {
        /* 设置页地址 0xB0+page，列地址低/高 0x00/0x10 */
        cmd[0] = 0xB0 + page;
        cmd[1] = 0x00;
        cmd[2] = 0x10;
        if (bsp_iic_mem_write(OLED.iicx, OLED.dev_addr, 0x00, I2C_MEMADD_SIZE_8BIT, cmd, 3, 1000) != 0)
        {
            return;
        }
        /* 整页 128 字节数据一次写入 */
        if (bsp_iic_mem_write(OLED.iicx, OLED.dev_addr, 0x40, I2C_MEMADD_SIZE_8BIT,
                              OLED_fb[page], OLED_WIDTH, 1000) != 0)
        {
            return;
        }
    }
}

void OLED_show_char(uint8_t col, uint8_t row, char ch)
{
    const uint8_t *glyph;
    uint8_t x, i;

    if (col >= OLED_MAX_COL || row >= OLED_MAX_ROW)
    {
        return;
    }
    glyph = font8x16_get(ch);   /* 中间件字库：越界自动落空格；空槽返回 NULL */
    if (glyph == NULL)
    {
        return;   /* 字库卡未插：跳过字符绘制，图形功能不受影响 */
    }

    /* 字符高 16 像素 = 2 页：上半 8 行进 row*2 页，下半 8 行进 row*2+1 页 */
    x = col * OLED_FONT_W;
    for (i = 0; i < 8; i++)
    {
        OLED_fb[row * 2][x + i]     = glyph[i];      /* 上半页 */
        OLED_fb[row * 2 + 1][x + i] = glyph[i + 8];  /* 下半页 */
    }
}

void OLED_show_string(uint8_t col, uint8_t row, const char *str)
{
    if (str == NULL)
    {
        return;
    }
    while (*str != '\0' && col < OLED_MAX_COL)
    {
        OLED_show_char(col, row, *str);
        str++;
        col++;
    }
}

void OLED_show_number(uint8_t col, uint8_t row, int32_t num)
{
    char buf[12];
    char *p = buf + sizeof(buf) - 1;
    uint32_t u;
    uint8_t neg = 0;

    /* 自己写十进制转换，不依赖 printf（newlib-nano 下 %f 等有坑） */
    if (num < 0)
    {
        neg = 1;
        u = (uint32_t)(-(num + 1)) + 1u;   /* 避免 INT32_MIN 取负溢出 */
    }
    else
    {
        u = (uint32_t)num;
    }

    *p = '\0';
    do
    {
        *--p = (char)('0' + (u % 10));
        u /= 10;
    } while (u != 0);

    if (neg)
    {
        *--p = '-';
    }
    OLED_show_string(col, row, p);
}

/* ---- 图形：全部画进显存缓冲，需 OLED_refresh() 上屏 ---- */

void OLED_draw_point(uint8_t x, uint8_t y)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT)
    {
        return;
    }
    OLED_fb[y >> 3][x] |= (uint8_t)(0x01 << (y & 0x07));
}

/* 内部：Bresenham 直线，xy 均用有符号中间量防越界计算 */
void OLED_draw_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    int16_t dx, dy, sx, sy, err, e2;

    dx = (x1 > x0) ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
    dy = (y1 > y0) ? (int16_t)(y1 - y0) : (int16_t)(y0 - y1);
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    err = dx - dy;

    for (;;)
    {
        OLED_draw_point(x0, y0);
        if (x0 == x1 && y0 == y1)
        {
            break;
        }
        e2 = (int16_t)(2 * err);
        if (e2 > -dy)
        {
            err -= dy;
            x0 = (uint8_t)((int16_t)x0 + sx);
        }
        if (e2 < dx)
        {
            err += dx;
            y0 = (uint8_t)((int16_t)y0 + sy);
        }
    }
}

void OLED_draw_rect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t fill)
{
    uint8_t x, y;

    if (fill)
    {
        for (y = y0; y <= y1 && y < OLED_HEIGHT; y++)
        {
            for (x = x0; x <= x1 && x < OLED_WIDTH; x++)
            {
                OLED_draw_point(x, y);
            }
        }
    }
    else
    {
        OLED_draw_line(x0, y0, x1, y0);
        OLED_draw_line(x0, y1, x1, y1);
        OLED_draw_line(x0, y0, x0, y1);
        OLED_draw_line(x1, y0, x1, y1);
    }
}

#endif /* CFG_ENABLE_OLED */
