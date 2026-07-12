/**
 * ============================================================================
 * task_http.c —— HTTP_Task（网络任务）
 *
 * 【职责】
 *   1. 接收 FreeRTOS 通知 → 启动网络流程
 *   2. 网络流程: ①连接WiFi → ②获取网络时间 → ③获取天气
 *   3. HTTP 状态机调度
 *   4. 空闲时: ESP8266 ↔ USART1 双向透传（调试用）
 *
 * 【关键设计】
 *   WiFi 连接使用 vTaskDelay 轮询（非 Delay_ms 忙等），LVGL_Task 不会被饿死
 *   网络流程全程在 HTTP_Task 上下文中执行，独占 ESP8266 通信
 * ============================================================================
 */
#include "task_http.h"
#include "task_boot.h"        /* NET_CMD_START, g_wifi_boot */
#include "bsp_usart.h"
#include "bsp_esp8266.h"
#include "http_client.h"
#include <stdio.h>
#include <string.h>


TaskHandle_t HTTPTask_Handle;

/* 天气数据实例（http_client.c 的解析函数写入，UI_Main.c 读取） */
WeatherData g_weather = { 0 };


/* ================================================================
 * Net_JoinAP — 用 vTaskDelay 轮询连接 WiFi
 *
 * 每 1s 检查一次 ESP8266 响应缓冲区，最多等 30s
 * 与 ESP8266_JoinAP 的区别: 用 vTaskDelay 而非 Delay_ms 忙等
 * ================================================================ */
static bool Net_JoinAP(const char *ssid, const char *pwd)
{
    char cmd[120];
    int  i;

    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);

    strEsp8266_Fram_Record.InfBit.FramLength     = 0;
    strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
    macESP8266_Usart("%s\r\n", cmd);

    for (i = 0; i < 30; i++)                         /* 30s 超时 */
    {
        vTaskDelay(pdMS_TO_TICKS(1000));             /* 让出 CPU 1 秒 */

        if (strEsp8266_Fram_Record.InfBit.FramFinishFlag)
        {
            strEsp8266_Fram_Record.Data_RX_BUF[
                strEsp8266_Fram_Record.InfBit.FramLength] = '\0';
            printf("%s", strEsp8266_Fram_Record.Data_RX_BUF);

            if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "OK") ||
                strstr(strEsp8266_Fram_Record.Data_RX_BUF, "WIFI GOT IP"))
            {
                return true;
            }
            if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "FAIL"))
            {
                return false;
            }

            /* 还没出结果，清标志继续等 */
            strEsp8266_Fram_Record.InfBit.FramLength     = 0;
            strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
        }
    }
    printf("[NET] WiFi connect timeout (30s)\n");
    return false;
}


/* ================================================================
 * NetFlow_Run — 网络启动主流程
 *
 *   ① 连接 WiFi     → 写 g_wifi_boot
 *   ② 获取网络时间   → HTTP 状态机 → Parse → MyRTC_SetTime
 *   ③ 获取天气 JSON  → HTTP 状态机 → Parse → g_weather
 *
 * 全程使用 vTaskDelay，LVGL_Task 不会被饿死
 * ================================================================ */
static void NetFlow_Run(void)
{
    /* ---- 步骤0: 确保 ESP8266 处于 STA 模式 ---- */
    printf("[NET] Set STA mode...\n");
    ESP8266_Net_Mode_Choose(STA);

    /* ---- 步骤1: 连接 WiFi ---- */
    printf("[NET] Connecting WiFi...\n");
    if (Net_JoinAP("ZenStone", "12345678"))
    {
        g_wifi_boot.wifi_sign = 1;
        strcpy(g_wifi_boot.wifi_name, "ZenStone");
        printf("[NET] WiFi connected!\n");
    }
    else
    {
        g_wifi_boot.wifi_sign = 0;
        printf("[NET] WiFi FAILED\n");
        return;
    }

    /*
     * DHCP + DNS 需要时间就绪（手机上热点 DHCP 尤其慢）
     * 等 3 秒确保 ESP8266 拿到 IP 和 DNS
     */
    printf("[NET] Waiting DHCP/DNS settle...\n");
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* 打印 ESP8266 获取到的 IP（调试用） */
    printf("[NET] ESP8266 IP info:\n");
    ESP8266_Cmd("AT+CIFSR", "OK", NULL, 2000);
    printf("%s", strEsp8266_Fram_Record.Data_RX_BUF);

    /* ---- 步骤2: 获取网络时间 ---- */
    printf("[NET] Fetching network time...\n");
    HTTP_StartTask(HTTP_TASK_TIME);
    while (!HTTP_IsIdle())
    {
        HTTP_StateMachine_Run();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    printf("[NET] Time fetch done.\n");

    /* ---- 步骤3: 获取天气 ---- */
    printf("[NET] Fetching weather...\n");
    HTTP_StartTask(HTTP_TASK_WEATHER);
    while (!HTTP_IsIdle())
    {
        HTTP_StateMachine_Run();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    printf("[NET] Weather fetch done.\n");

    printf("[NET] === Network flow complete ===\n");
}


/* ================================================================
 * HTTP_Task — 主循环
 * ================================================================ */
void HTTP_Task(void *p)
{
    uint32_t cmd;

    while (1)
    {
        /* ----- 接收 FreeRTOS 通知 ----- */
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &cmd, 0) == pdTRUE)
        {
            if (cmd == NET_CMD_START)
            {
                printf("[NET] Received NET_CMD_START\n");
                NetFlow_Run();
            }
        }

        /* ----- 处理 USART1(PC) 串口命令帧（调试用） ----- */
        if (strUSART_Fram_Record.InfBit.FramFinishFlag)
        {
            strUSART_Fram_Record.Data_RX_BUF[
                strUSART_Fram_Record.InfBit.FramLength] = '\0';

            if (strstr(strUSART_Fram_Record.Data_RX_BUF, "TIME"))
            {
                HTTP_StartTask(HTTP_TASK_TIME);
            }
            else if (strstr(strUSART_Fram_Record.Data_RX_BUF, "WEAT"))
            {
                HTTP_StartTask(HTTP_TASK_WEATHER);
            }
            else if (strstr(strUSART_Fram_Record.Data_RX_BUF, "HTTP"))
            {
                HTTP_StartTask(HTTP_TASK_TIME);
            }
            else
            {
                if (HTTP_IsIdle())
                    Usart_SendString(macESP8266_USARTx,
                        strUSART_Fram_Record.Data_RX_BUF);
            }

            strUSART_Fram_Record.InfBit.FramLength     = 0;
            strUSART_Fram_Record.InfBit.FramFinishFlag = 0;
        }

        /* ----- HTTP 状态机 / ESP8266 透传 ----- */
        if (!HTTP_IsIdle())
        {
            HTTP_StateMachine_Run();
        }
        else
        {
            /* 空闲时透传: ESP8266 → USART1 */
            if (strEsp8266_Fram_Record.InfBit.FramFinishFlag)
            {
                strEsp8266_Fram_Record.Data_RX_BUF[
                    strEsp8266_Fram_Record.InfBit.FramLength] = '\0';
                Usart_SendString(DEBUG_USARTx,
                    strEsp8266_Fram_Record.Data_RX_BUF);
                strEsp8266_Fram_Record.InfBit.FramLength     = 0;
                strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
            }
        }

        vTaskDelay(5);
    }
}
