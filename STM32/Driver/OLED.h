#ifndef     OLED_H
#define     OLED_H

#include    <stdint.h>
#include    "bsp_iic.h"
#include    "font8x16.h"        /* 字库规格常量：OLED_FONT_W/H 取自中间件，杜绝魔法数漂移 */
#include    "app_config.h"     /* 模块总开关：必须先于本文件包含 */

/* =====================================================================
 * SSD1306 OLED 128x64（I2C 接口，0.96 寸）
 * 与 MPU6050 同款插槽模型：CFG_ENABLE_OLED=1 编译真驱动，
 * =0 时头文件自动换成空桩，调用点（任务）无需任何 #if。
 * 字库（font8x16，中间件）与 OLED 共享此开关，不单独裁剪。
 * ===================================================================== */

/* ---- 器件地址：SA0 引脚决定（模块背面可焊）---- */
#define   OLED_ADDR1          0x3C     /* SA0=0（接地，常见默认） */
#define   OLED_ADDR2          0x3D     /* SA0=1（接 VCC） */

/* ---- SSD1306 关键命令 ---- */
#define   OLED_CMD_OFF        0xAE     /* 显示关 */
#define   OLED_CMD_ON         0xAF     /* 显示开 */
#define   OLED_CMD_NOP        0xE3     /* 空操作：用于探测器件是否存在 */

/* ---- 屏参 ---- */
#define   OLED_WIDTH          128
#define   OLED_HEIGHT         64
#define   OLED_PAGE_NUM       8        /* 64 行 / 8 = 8 页 */
#define   OLED_FONT_W         FONT8X16_WIDTH    /* 8x16 字符宽：取自中间件字库规格 */
#define   OLED_FONT_H         FONT8X16_HEIGHT   /* 字符高 16 = 2 页 */
#define   OLED_MAX_COL        (OLED_WIDTH  / OLED_FONT_W)   /* 16 字符列 */
#define   OLED_MAX_ROW        (OLED_HEIGHT / OLED_FONT_H)   /* 4 字符行 */

/* ---- 器件句柄：挂在哪路 I2C + 探测到的 7 位地址 ---- */
typedef struct{
    bsp_iic_t iicx;
    uint8_t dev_addr;       /* 7 位器件地址（0x3C / 0x3D），初始化时探测得到 */
}OLED_t;

#if CFG_ENABLE_OLED

/* 探测地址 + 初始化序列 + 清屏；成功返回 0 */
int  OLED_init(bsp_iic_t iicx);

/* 显存操作：先改显存缓冲，再 OLED_refresh() 一次性刷上屏 */
void OLED_clear(void);                          /* 显存全清（未上屏） */
void OLED_refresh(void);                        /* 整屏刷新：显存 -> 屏 */

/* 字符/字符串：col 0..15，row 0..3（8x16 字符网格） */
void OLED_show_char  (uint8_t col, uint8_t row, char ch);
void OLED_show_string(uint8_t col, uint8_t row, const char *str);
void OLED_show_number(uint8_t col, uint8_t row, int32_t num);   /* 十进制整数 */

/* 图形：像素坐标 x 0..127，y 0..63 */
void OLED_draw_point(uint8_t x, uint8_t y);
void OLED_draw_line (uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);
void OLED_draw_rect (uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t fill);  /* fill=1 填充 */

#else
/* ===== 裁剪桩：开关=0 时模块不编译，调用点无需 #if，直接编成空操作 ===== */
static inline int  OLED_init(bsp_iic_t iicx)                        { (void)iicx; return -1; }
static inline void OLED_clear(void)                                 {}
static inline void OLED_refresh(void)                               {}
static inline void OLED_show_char(uint8_t col, uint8_t row, char ch){ (void)col; (void)row; (void)ch; }
static inline void OLED_show_string(uint8_t col, uint8_t row, const char *str){ (void)col; (void)row; (void)str; }
static inline void OLED_show_number(uint8_t col, uint8_t row, int32_t num){ (void)col; (void)row; (void)num; }
static inline void OLED_draw_point(uint8_t x, uint8_t y)            { (void)x; (void)y; }
static inline void OLED_draw_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1){ (void)x0; (void)y0; (void)x1; (void)y1; }
static inline void OLED_draw_rect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t fill){ (void)x0; (void)y0; (void)x1; (void)y1; (void)fill; }
#endif /* CFG_ENABLE_OLED */

#endif
