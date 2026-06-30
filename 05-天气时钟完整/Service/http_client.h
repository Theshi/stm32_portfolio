/**
 * ============================================================================
 * http_client.h —— HTTP 非阻塞状态机
 *
 * 职责:
 *   - 封装 AT 指令的 TCP 连接 + HTTP GET 请求状态机
 *   - 解析 HTTP 响应（Date 头/天气 JSON）
 *   - 完全不依赖 FreeRTOS 任务概念，纯逻辑层
 *
 * 使用方式:
 *   HTTP_Task 周期调用 HTTP_StateMachine_Run()
 *   HTTP_StartTask(HTTP_TASK_TIME) 触发一次请求
 * ============================================================================
 */
#ifndef __HTTP_CLIENT_H
#define __HTTP_CLIENT_H

#include "stm32f10x.h"
#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_esp8266.h"
#include "bsp_usart.h"
#include "http_utils.h"


/* ======================== 任务类型枚举 ======================== */
typedef enum {
    HTTP_TASK_NONE = 0,         /* 无任务 */
    HTTP_TASK_TIME,             /* 获取网络时间 (httpbin.org) */
    HTTP_TASK_WEATHER           /* 获取天气 (wttr.in) */
} HTTP_TaskTypeDef;


/* ======================== HTTP 任务配置 ======================== */
typedef struct {
    const char *host;
    uint16_t    port;
    const char *request;
} HTTP_TaskConfig;


/* ======================== HTTP 状态机枚举 ======================== */
typedef enum {
    HTTP_STATE_IDLE,            /* 空闲 */
    HTTP_SATEP_CIPSTART,        /* 发起 TCP 连接 */
    HTTP_STATE_WAIT_CONNECT,    /* 等待 CONNECT OK */
    HTTP_STATE_CIPSEND,         /* 准备发送数据长度 */
    HTTP_STATE_WAIT_PROMPT,     /* 等待 ">" 提示符 */
    HTTP_STATE_SEND_HTTP,       /* 发送 HTTP GET */
    HTTP_STATE_WAIT_RESPONSE,   /* 等待服务器响应 */
    HTTP_STATE_DONE,            /* 完成 */
    HTTP_STATE_ERROR            /* 出错 */
} HTTP_StateTypeDef;


/* ======================== 公开 API ======================== */

/* ---- 启动一个 HTTP 任务 -----------------------------------------
 * 在空闲状态下调用，状态机会自动经历 CIPSTART → ... → DONE
 * type: HTTP_TASK_TIME 或 HTTP_TASK_WEATHER
 * -------------------------------------------------------------- */
void HTTP_StartTask(HTTP_TaskTypeDef type);

/* ---- 运行 HTTP 状态机 -------------------------------------------
 * 需在 Task 循环中周期调用（每 5~10ms 一次）
 * 内部使用 vTaskDelay 等待异步响应
 * -------------------------------------------------------------- */
void HTTP_StateMachine_Run(void);

/* ---- 查询接口 -------------------------------------------------- */
uint8_t             HTTP_IsIdle(void);
HTTP_TaskTypeDef    HTTP_GetCurrentTask(void);
const char *        HTTP_GetResponseBuffer(void);


#endif /* __HTTP_CLIENT_H */
