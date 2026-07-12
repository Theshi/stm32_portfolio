/**
 * ============================================================================
 * task_http.h —— HTTP_Task 声明 + 天气数据共享结构体
 *
 * 职责:
 *   1. FreeRTOS 任务包装: 接收通知 → WiFi连接 → 网络对时 → 天气获取
 *   2. HTTP 状态机调度
 *   3. WeatherData: UI 和 HTTP_Task 之间的天气数据共享结构
 * ============================================================================
 */
#ifndef __TASK_HTTP_H
#define __TASK_HTTP_H

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

#define HTTPTask_STACKSIZE  1024
#define HTTPTask_PRIO       3


/* ======================== 天气数据共享结构体 ======================== */
typedef struct {
    int8_t  temp_C;           /* 室外温度（整数）       */
    int8_t  humidity;         /* 室外湿度（整数 %）     */
    char    weather_desc[32]; /* 天气描述（如 "Sunny"） */
    uint8_t valid;            /* 0=未获取, 1=有效数据   */
} WeatherData;


/* ======================== HTTP_Task 内部命令 ======================== */
/*
 * Boot_Task 发送 NET_CMD_START (0x100) 启动网络流程
 * 该值定义在 task_boot.h 中
 */


/* ======================== 全局变量声明 ======================== */
extern TaskHandle_t HTTPTask_Handle;
extern WeatherData g_weather;    /* 由 http_client.c 写入，UI_Main.c 读取 */


void HTTP_Task(void *p);

#endif /* __TASK_HTTP_H */
