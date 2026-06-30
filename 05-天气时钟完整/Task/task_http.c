/**
 * ============================================================================
 * task_http.c —— HTTP_Task
 *
 * 【职责】
 *   1. 空闲时: USART1 <-> ESP8266 双向透传
 *   2. 收到 "TIME"/"WEAT"/"HTTP" 命令时: 启动 HTTP 状态机
 *   3. 状态机运行时: 不再透传, 由状态机接管 ESP8266 数据
 *
 * 【层级关系】
 *   task_http.c (任务调度) → Service/http_client.c (状态机逻辑)
 * ============================================================================
 */
#include "task_http.h"
#include "bsp_usart.h"
#include "bsp_esp8266.h"
#include "http_client.h"
#include <stdio.h>

TaskHandle_t HTTPTask_Handle;


void HTTP_Task(void *p)
{
    while (1)
    {
        /* ----- 处理 USART1 命令帧 ----- */
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
                /* 空闲时透传: USART1 -> ESP8266 */
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
            /* 空闲时透传: ESP8266 -> USART1 */
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

        vTaskDelay(5);  /* ~5ms 轮询周期 */
    }
}
