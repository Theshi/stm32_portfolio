/**
 * ============================================================================
 * task_startup.c —— StartUpTask
 *
 * 简单的周期性心跳任务，输出 "run..." 表示系统正常运行
 * ============================================================================
 */
#include "task_startup.h"
#include <stdio.h>

TaskHandle_t StartUpTask_Handle;


void StartUpTask(void *p)
{
    while (1)
    {
        vTaskDelay(1000);
        printf("run...\n");
    }
}
