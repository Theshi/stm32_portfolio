/**
 * ============================================================================
 * task_boot.h —— Boot_Task 声明
 *
 * 职责:
 *   系统上电自检任务 — 检查 W25Q64 字库 → 测试 ESP8266 → 通知 LVGL
 * ============================================================================
 */
#ifndef __TASK_BOOT_H
#define __TASK_BOOT_H

#include "FreeRTOS.h"
#include "task.h"

#define BootTask_STACKSIZE  512
#define BootTask_PRIO       2

extern TaskHandle_t BootTask_Handle;

void Boot_Task(void *p);

#endif /* __TASK_BOOT_H */
