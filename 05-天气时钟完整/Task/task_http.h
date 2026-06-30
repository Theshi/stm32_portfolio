/**
 * ============================================================================
 * task_http.h —— HTTP_Task 声明
 *
 * 职责:
 *   FreeRTOS 任务包装，管理 USART1<->ESP8266 双向透传
 *   和 HTTP 状态机调度
 * ============================================================================
 */
#ifndef __TASK_HTTP_H
#define __TASK_HTTP_H

#include "FreeRTOS.h"
#include "task.h"

#define HTTPTask_STACKSIZE  1024
#define HTTPTask_PRIO       3

extern TaskHandle_t HTTPTask_Handle;

void HTTP_Task(void *p);

#endif /* __TASK_HTTP_H */
