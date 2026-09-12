#ifndef     APP_SENSOR_H
#define     APP_SENSOR_H

/* 采集任务（App 层）：定时读塔石传感器 → 串口曲线 → 写队列
 * 入口参数：osMessageQueueId_t（sensor_msg_t 队列） */
void AppSensor_Task(void *argument);

#endif
