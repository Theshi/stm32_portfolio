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
#include <stdbool.h>

#define BootTask_STACKSIZE  512
#define BootTask_PRIO       2

extern TaskHandle_t BootTask_Handle;

typedef struct
{
    char  wifi_name[32];          /* SSID 名称 */
    volatile int8_t wifi_sign;    /* 0=未连上, 1=已连上（volatile 防跨任务优化） */
} WIFI_Boot;

extern WIFI_Boot g_wifi_boot;   /* 声明变量（在 task_boot.c 里定义） */

/* Boot_Task 发给 HTTP_Task 的网络启动命令 */
#define NET_CMD_START  0x100

void Boot_Task(void *p);


#endif /* __TASK_BOOT_H */
