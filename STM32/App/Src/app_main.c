#include    "FreeRTOS.h"
#include    "task.h"
#include    "cmsis_os.h"
#include    "app_config.h"
#include    "app_msg.h"
#include    "app_main.h"
#include    "app_sensor.h"
#include    "app_uplink.h"
#include    "Debug.h"

/* =====================================================================
 * App 层总入口：集中创建队列与各业务任务
 *   —— 任务名/栈/优先级都在这里一眼看全；各模块自己的逻辑在各自文件
 * 调用者：freertos.c 的 defaultTask（启动器），调用后自删
 * ===================================================================== */
static const char* TAG = "app_main";

static osMessageQueueId_t s_sensor_q = NULL;

/* 栈 2048B：内含 printf/AT 拼接缓冲，实测足够；改这里即可调整 */
static const osThreadAttr_t sensor_attr = {
    .name       = "app_sensor",
    .stack_size = 2048,
    .priority   = (osPriority_t)osPriorityNormal,
};

static const osThreadAttr_t uplink_attr = {
    .name       = "app_uplink",
    .stack_size = 2048,
    /* 低于采集：AT 轮询是分片阻塞式，不能让它抢占采样节拍 */
    .priority   = (osPriority_t)osPriorityBelowNormal,
};

void App_Init(void)
{
    s_sensor_q = osMessageQueueNew(APP_SENSOR_QUEUE_DEPTH, sizeof(sensor_msg_t), NULL);
    if (s_sensor_q == NULL)
    {
        LOG_E(TAG, "采样队列创建失败，App 未启动");
        return;
    }

    if (osThreadNew(AppSensor_Task, s_sensor_q, &sensor_attr) == NULL)
    {
        LOG_E(TAG, "采集任务创建失败");
    }
    if (osThreadNew(AppUplink_Task, s_sensor_q, &uplink_attr) == NULL)
    {
        LOG_E(TAG, "上行任务创建失败");
    }

    LOG_I(TAG, "App 层已启动：采集(Normal) + 上行(BelowNormal)，队列深度 %d",
          APP_SENSOR_QUEUE_DEPTH);
}
