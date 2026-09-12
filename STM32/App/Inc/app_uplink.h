#ifndef     APP_UPLINK_H
#define     APP_UPLINK_H

/* 上行任务（App 层）：等队列 → 组行 → ESP8266 建连/重连/发送
 * 入口参数：osMessageQueueId_t（sensor_msg_t 队列） */
void AppUplink_Task(void *argument);

#endif
