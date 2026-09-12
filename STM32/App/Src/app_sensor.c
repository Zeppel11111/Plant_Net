#include    <stdio.h>
#include    "FreeRTOS.h"
#include    "task.h"
#include    "cmsis_os.h"
#include    "app_config.h"
#include    "app_msg.h"
#include    "app_sensor.h"
#include    "TAS_GZ.h"
#include    "Debug.h"

/* =====================================================================
 * 采集任务：只负责"定时取数 + 发布"，不关心网络是否在线
 *   · 30s 绝对节拍（vTaskDelayUntil，不累积漂移）
 *   · 读 TAS_GZ → USART1 打印 VOFA 三通道 → 写队列给上行任务
 *   · 队列满则丢最旧：采样数据滞后期望无意义
 * ===================================================================== */
static const char* TAG = "app_sensor";

#define     APP_SENSOR_PERIOD_MS   30000u   /* 采集周期：想改间隔只改这里 */

void AppSensor_Task(void *argument)
{
    osMessageQueueId_t q = (osMessageQueueId_t)argument;
    TAS_GZ_data_t tas = {0};
    uint32_t seq = 0;
    TickType_t last_wake;

    /* 传感器初始化：失败则停住（空槽时 TAS_GZ_init 为空桩返回 -1） */
    if (TAS_GZ_init(BSP_UART2) != 0)
    {
        LOG_E(TAG, "TAS_GZ 初始化失败（查 485 接线/供电）");
        for (;;) { osDelay(1000); }
    }
    LOG_I(TAG, "采集任务启动，周期 %u ms", (unsigned)APP_SENSOR_PERIOD_MS);

    last_wake = xTaskGetTickCount();

    for (;;)
    {
        if (TAS_GZ_read(&tas) == 0)
        {
            /* VOFA+ 三通道（整数：newlib-nano 未链 _printf_float）
             * 用 LOG_printf：走日志互斥锁，与其它任务的 LOG 输出不插花 */
            LOG_printf("%d,%d,%d\n",
                       (int)tas.temp_x10, (int)tas.humi_x10, (int)tas.lux);

            sensor_msg_t m = {
                .temp_x10 = (int16_t)tas.temp_x10,
                .humi_x10 = (int16_t)tas.humi_x10,
                .lux      = (uint32_t)tas.lux,
                .seq      = ++seq,
            };

            if (osMessageQueueGetCount(q) >= APP_SENSOR_QUEUE_DEPTH)
            {
                sensor_msg_t drop;
                (void)osMessageQueueGet(q, &drop, NULL, 0);   /* 丢最旧 */
                LOG_D(TAG, "队列满，丢弃 seq=%u", (unsigned)drop.seq);
            }
            if (osMessageQueuePut(q, &m, 0, 0) != osOK)
            {
                LOG_E(TAG, "入队失败（队列异常）");
            }
        }
        else
        {
            LOG_D(TAG, "读取失败，本轮跳过");
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_SENSOR_PERIOD_MS));
    }
}
