/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>       /* strlen（栈溢出钩子用） */
#include "bsp_uart.h"     /* 栈溢出钩子直接发串口（不取日志锁，避免钩子内阻塞） */
#include "app_config.h"   /* 模块总开关：必须先于各模块头文件 */
#include "app_main.h"     /* App 层总入口：队列与业务任务都在这里创建 */
#include "Debug.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* 业务逻辑已收束到 App 层（App/Src/app_*.c），本文件只做 RTOS 启动。
   网络地址/端口见 config/net_config.h；采集周期见 app_sensor.c。 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  /* 栈 2048B：TAS_GZ_read 单帧 ~300B + printf ~250B 峰值已用 ~900B，
     实测 1024B 只剩 107B 余量；后续加 ESP8266 AT 缓冲/JSON 拼串还要 ~400B，
     留足余量。改这里要同步改 .ioc 的 Tasks01 栈参数 */
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  RTOS 启动器：拉起 App 层后自删。
  *         业务逻辑全部收束在 App/Src/app_*.c：
  *           app_sensor.c  采集（30s 节拍读 TAS_GZ → 队列）
  *           app_uplink.c  上行（ESP8266 建连/重连/发送）
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  (void)argument;

  App_Init();            /* 创建采样队列 + 采集任务 + 上行任务 */

  vTaskDelete(NULL);     /* 启动器使命完成：退出本任务（后续全由 App 层任务运行） */
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* 栈溢出钩子：configCHECK_FOR_STACK_OVERFLOW=2 时，内核检测到任务栈
 * 被踩穿会调到这里。打印任务名后停住，便于立刻发现（正常不应触发）
 * 注意：钩子里不取日志互斥锁（可能已被占用导致死等），直接发串口 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  static const char head[] = "\r\n[FATAL] 栈溢出, 任务=";
  static const char tail[] = "\r\n";
  const char *name = (pcTaskName != NULL) ? pcTaskName : "?";

  (void)xTask;
  taskDISABLE_INTERRUPTS();
  if (bsp_uart_init(BSP_UART1) != 0) { /* 串口未初始化时也不影响停机 */ }
  bsp_uart_send(BSP_UART1, (uint8_t *)head, (uint16_t)(sizeof(head) - 1),
                0xFFFFFFFF, BSP_UART_MODE_POLLING);
  bsp_uart_send(BSP_UART1, (uint8_t *)name, (uint16_t)strlen(name),
                0xFFFFFFFF, BSP_UART_MODE_POLLING);
  bsp_uart_send(BSP_UART1, (uint8_t *)tail, (uint16_t)(sizeof(tail) - 1),
                0xFFFFFFFF, BSP_UART_MODE_POLLING);
  for(;;);
}

/* USER CODE END Application */

