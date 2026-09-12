#ifndef     FONT8X16_H
#define     FONT8X16_H

#include    <stdint.h>
#include    <stddef.h>          /* NULL 定义（空桩返回 NULL 需要） */
#include    "app_config.h"     /* 模块总开关：必须先于本文件包含 */

/* =====================================================================
 * 8x16 ASCII 点阵字库（中间件：纯数据，不依赖任何硬件）
 *
 * 开关策略：本模块与 OLED 高度绑定，不设独立开关——
 * 工程内 CFG_ENABLE_FONT8X16 由 app_config.h 定义为 CFG_ENABLE_OLED
 * 的别名（OLED 开字库进固件，OLED 关字库一并剔除）。
 * 独立移植到其他工程时若无此宏，下面的 fallback 默认启用（=1）。
 * 空槽语义：开关=0 时 font8x16_get() 返回 NULL，调用方自行降级。
 * ===================================================================== */

#ifndef CFG_ENABLE_FONT8X16
#define CFG_ENABLE_FONT8X16     1   /* 独立移植时默认启用；工程内由 app_config.h 定义为 CFG_ENABLE_OLED 别名 */
#endif

/* ---- 字库规格 ---- */
#define FONT8X16_WIDTH          8    /* 每字符宽 8 像素 */
#define FONT8X16_HEIGHT         16   /* 每字符高 16 像素 */
#define FONT8X16_FIRST          0x20 /* 起始字符：空格 */
#define FONT8X16_LAST           0x7E /* 结束字符：~ */
#define FONT8X16_COUNT          (FONT8X16_LAST - FONT8X16_FIRST + 1)   /* 95 */

#if CFG_ENABLE_FONT8X16

/* 取字符字形：返回指向 16 字节点阵的指针（每字节一行 8 像素，高位在左）。
 * 越界字符自动落回空格字形，永不返回越界指针。 */
const uint8_t *font8x16_get(char ch);

#else
/* ===== 裁剪桩：空槽时调用方收到 NULL，自行降级（如 OLED 跳过字符绘制） ===== */
static inline const uint8_t *font8x16_get(char ch) { (void)ch; return NULL; }
#endif /* CFG_ENABLE_FONT8X16 */

#endif /* FONT8X16_H */
