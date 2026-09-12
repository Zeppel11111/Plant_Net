#include    <stdarg.h>
#include    <stdio.h>
#include    <string.h>
#include    "cmsis_os.h"
#include    "bsp_uart.h"
#include    "debug.h"

/* =====================================================================
 * 调试日志（线程安全）
 *   · LOG_init(): 认领 USART1（BSP 层）+ 创建日志互斥锁
 *     —— 在 osKernelStart() 之前调用，RTOS 允许内核启动前创建互斥量
 *   · LOG_printf()/LOG_out(): 组好整行后加锁一次性发出（行原子，不插花）
 *   · __io_putchar(): 保留给 newlib 兜底（printf/assert），不加锁
 * ===================================================================== */
#define DEBUG_PORT      BSP_UART1
#define LOG_LINE_MAX    160

static osMutexId_t s_log_mutex = NULL;

void LOG_init(void)
{
    bsp_uart_init(DEBUG_PORT);          /* 串口初始化唯一入口：BSP 层 */
    s_log_mutex = osMutexNew(NULL);     /* 内核启动前创建，允许 */
}

static void log_send(const char *s, size_t n)
{
    bsp_uart_send(DEBUG_PORT, (uint8_t *)s, (uint16_t)n, 0xFFFFFFFF,
                  BSP_UART_MODE_POLLING);
}

/* 线程安全的无前缀输出（给 VOFA 数据行、临时调试用） */
int LOG_printf(const char *fmt, ...)
{
    char buf[LOG_LINE_MAX];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) { return -1; }
    if (n >= (int)sizeof(buf)) { n = (int)sizeof(buf) - 1; }

    if (s_log_mutex != NULL) { (void)osMutexAcquire(s_log_mutex, osWaitForever); }
    log_send(buf, (size_t)n);
    if (s_log_mutex != NULL) { (void)osMutexRelease(s_log_mutex); }
    return n;
}

/* 线程安全的带级别/标签输出（LOG_E/W/I/D 宏的落点） */
void LOG_out(const char *lvl, const char *tag, const char *fmt, ...)
{
    char body[LOG_LINE_MAX];
    char line[LOG_LINE_MAX + 32];
    va_list ap;
    int n;

    va_start(ap, fmt);
    (void)vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    n = snprintf(line, sizeof(line), "[%s][%s] %s", lvl, tag, body);
    if (n < 0) { return; }
    if (n >= (int)sizeof(line)) { n = (int)sizeof(line) - 1; }

    if (s_log_mutex != NULL) { (void)osMutexAcquire(s_log_mutex, osWaitForever); }
    log_send(line, (size_t)n);
    if (s_log_mutex != NULL) { (void)osMutexRelease(s_log_mutex); }
}

/* newlib 兜底：把每个字符从 USART1 发出去（不加锁，仅异常路径用） */
int __io_putchar(int ch)
{
    uint8_t c = (uint8_t)ch;

    if (ch == '\n')
    {
        uint8_t cr = '\r';
        bsp_uart_send(DEBUG_PORT, &cr, 1, 0xFFFFFFFF, BSP_UART_MODE_POLLING);
    }
    bsp_uart_send(DEBUG_PORT, &c, 1, 0xFFFFFFFF, BSP_UART_MODE_POLLING);
    return ch;
}
