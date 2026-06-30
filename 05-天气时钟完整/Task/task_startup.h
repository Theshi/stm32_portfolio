/**
 * ============================================================================
 * task_startup.h —— StartUpTask 声明
 *
 * 职责:
 *   心跳打印任务，每 1 秒输出 "run..." 表示系统正常运行
 * ============================================================================
 */
#ifndef __TASK_STARTUP_H
#define __TASK_STARTUP_H

#include "FreeRTOS.h"
#include "task.h"

#define StartUpTask_STACKSIZE   256
#define StartUpTask_PRIO        1

extern TaskHandle_t StartUpTask_Handle;

void StartUpTask(void *p);

#endif /* __TASK_STARTUP_H */
