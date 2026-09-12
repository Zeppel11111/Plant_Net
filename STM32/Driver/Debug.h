#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

/* 日志级别：值越大越详细 */
typedef enum {
    DEBUG_LEVEL_OFF   = 0,   /* 关闭所有日志 */
    DEBUG_LEVEL_ERROR = 1,
    DEBUG_LEVEL_WARN  = 2,
    DEBUG_LEVEL_INFO  = 3,
    DEBUG_LEVEL_DEBUG = 4,
} debug_level_t;


/* 全局日志开关：改这一行即可控制整个工程的日志输出量 */
#define DEBUG_GLOBAL_LEVEL   DEBUG_LEVEL_DEBUG

#define DEBUG_ACTIVE(level)  ((level) <= DEBUG_GLOBAL_LEVEL)

/* 日志宏，用法同 ESP_LOG：LOG_I("TAG", "fmt", ...)
 * 全部经 LOG_out() 输出：整行加锁，多任务并发不插花（见 Debug.c） */
#define LOG_E(tag, fmt, ...) do { if (DEBUG_ACTIVE(DEBUG_LEVEL_ERROR)) LOG_out("E", tag, fmt "\r\n", ##__VA_ARGS__); } while(0)
#define LOG_W(tag, fmt, ...) do { if (DEBUG_ACTIVE(DEBUG_LEVEL_WARN))  LOG_out("W", tag, fmt "\r\n", ##__VA_ARGS__); } while(0)
#define LOG_I(tag, fmt, ...) do { if (DEBUG_ACTIVE(DEBUG_LEVEL_INFO))  LOG_out("I", tag, fmt "\r\n", ##__VA_ARGS__); } while(0)
#define LOG_D(tag, fmt, ...) do { if (DEBUG_ACTIVE(DEBUG_LEVEL_DEBUG)) LOG_out("D", tag, fmt "\r\n", ##__VA_ARGS__); } while(0)

/* 线程安全输出：LOG_init() 里创建互斥锁；LOG_printf 为无前缀版本（VOFA 数据行） */
void LOG_init(void);
void LOG_out(const char *lvl, const char *tag, const char *fmt, ...);
int  LOG_printf(const char *fmt, ...);

#endif /* DEBUG_H */
