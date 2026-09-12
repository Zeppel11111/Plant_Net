#ifndef     APP_MAIN_H
#define     APP_MAIN_H

/* App 层总入口：创建队列 + 各业务任务。
 * 由 freertos.c 的 defaultTask（启动器）调用一次，随后该任务自删。 */
void App_Init(void);

#endif
