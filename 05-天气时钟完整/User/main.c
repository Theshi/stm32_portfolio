/**
 * ============================================================================
 * main.c —— ZenStone: STM32 + FreeRTOS + LVGL + ESP8266
 *
 * 任务列表:
 *   LVGL_Task    - UI 渲染 (LVGL 单线程模型)
 *   Boot_Task    - 自检 (W25Q64 + ESP8266) -> 通知 LVGL 更新进度
 *   HTTP_Task    - ESP8266 AT透传 + HTTP时间/天气状态机
 *   StartUpTask  - 心跳打印
 * ============================================================================
 */

#include "stm32f10x.h"
#include "SysTick.h"
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_usart.h"
#include "bsp_esp8266.h"
#include "bsp_ili9341_lcd.h"
#include "W25Q64.h"
#include "core_delay.h"

#include "UI_API.h"


/* ================================ 任务类型 ================================ */
typedef enum {
    HTTP_TASK_NONE = 0,
    HTTP_TASK_TIME,
    HTTP_TASK_WEATHER
} HTTP_TaskTypeDef;


/* ============================ HTTP任务配置结构体 ============================ */
typedef struct {
    const char *host;
    uint16_t    port;
    const char *request;
} HTTP_TaskConfig;


/* ============================== HTTP状态机枚举 ============================= */
typedef enum {
    HTTP_STATE_IDLE,
    HTTP_SATEP_CIPSTART,
    HTTP_STATE_WAIT_CONNECT,
    HTTP_STATE_CIPSEND,
    HTTP_STATE_WAIT_PROMPT,
    HTTP_STATE_SEND_HTTP,
    HTTP_STATE_WAIT_RESPONSE,
    HTTP_STATE_DONE,
    HTTP_STATE_ERROR
} HTTP_StateTypeDef;


/* =========================== HTTP任务配置实例 ============================== */

/* 时间: httpbin.org/get -> Date头 -> GMT+8=北京时间 */
static const HTTP_TaskConfig task_time_cfg = {
    "httpbin.org",
    80,
    "GET /get HTTP/1.1\r\n"
    "Host: httpbin.org\r\n"
    "Connection: close\r\n"
    "\r\n"
};

/* 天气: wttr.in (免费HTTP, 无需API Key) */
static const HTTP_TaskConfig task_weather_cfg = {
    "wttr.in",
    80,
    "GET /Xi'an?format=j1 HTTP/1.1\r\n"
    "Host: wttr.in\r\n"
    "User-Agent: curl\r\n"
    "Connection: close\r\n"
    "\r\n"
};


/* ============================== 全局状态变量 ============================== */
static HTTP_TaskTypeDef       current_task = HTTP_TASK_NONE;
static const HTTP_TaskConfig *pCurrentTask = NULL;
static HTTP_StateTypeDef      http_state    = HTTP_STATE_IDLE;
static uint8_t                http_timeout  = 0;

/* 响应缓存 */
#define HTTP_RESP_BUF_SIZE  1024
static char     http_resp_buf[HTTP_RESP_BUF_SIZE];
static uint16_t http_resp_len = 0;


/* ========================== 响应解析(前置声明) ========================== */
static void Parse_Time_Response   (void);
static void Parse_Weather_Response(void);


/* ========================= 字符串提取辅助 ========================= */

static const char *Json_FindValue(const char *haystack, const char *key)
{
    const char *p = strstr(haystack, key);
    if (!p) return NULL;
    p += strlen(key);
    p = strstr(p, ":");
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '"') p++;
    return p;
}

static void Str_CopyDelim(const char *src, char *dst, uint8_t maxlen)
{
    uint8_t i = 0;
    while (src[i] && src[i] != '"' && src[i] != '\\' &&
           src[i] != '\r' && src[i] != '\n' && i < maxlen - 1)
        { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}


/* ============================= 时间响应解析 ============================= */

static void Parse_Time_Response(void)
{
    const char *p;
    char buf[64];
    char wday[4], mon[4];
    int  day, year, hour, min, sec;

    p = strstr(http_resp_buf, "Date: ");
    if (p)
    {
        p += 6;
        {
            uint8_t i = 0;
            while (p[i] && p[i] != '\r' && p[i] != '\n' && i < 63)
                { buf[i] = p[i]; i++; }
            buf[i] = '\0';
        }

        if (sscanf(buf, "%3s %2d %3s %4d %2d:%2d:%2d",
                   wday, &day, mon, &year, &hour, &min, &sec) == 6)
        {
            hour += 8;  /* GMT+8 = Beijing */
            if (hour >= 24) { hour -= 24; day += 1; }

            printf("\r\n=========== Network Time ===========\r\n");
            printf("  GMT    : %s\r\n", buf);
            printf("  Beijing: %04d-%02d-%02d %02d:%02d:%02d\r\n",
                   year,
                   (!strcmp(mon,"Jan"))?1:(!strcmp(mon,"Feb"))?2:
                   (!strcmp(mon,"Mar"))?3:(!strcmp(mon,"Apr"))?4:
                   (!strcmp(mon,"May"))?5:(!strcmp(mon,"Jun"))?6:
                   (!strcmp(mon,"Jul"))?7:(!strcmp(mon,"Aug"))?8:
                   (!strcmp(mon,"Sep"))?9:(!strcmp(mon,"Oct"))?10:
                   (!strcmp(mon,"Nov"))?11:12,
                   day, hour, min, sec);
        }
        else
        {
            printf("[TIME] Raw Date: %s\r\n", buf);
        }
    }
    else
    {
        printf("[TIME] No Date header found\r\n");
    }

    /* 公网IP */
    p = Json_FindValue(http_resp_buf, "\"origin\"");
    if (p) { Str_CopyDelim(p, buf, 64); printf("  IP     : %s\r\n", buf); }
    printf("====================================\r\n\r\n");
}


/* ============================= 天气响应解析 ============================= */

static void Parse_Weather_Response(void)
{
    const char *p;
    char temp_buf[16]    = "N/A";
    char weather_buf[64] = "N/A";
    char humid_buf[16]   = "N/A";
    char feels_buf[16]   = "N/A";
    char wind_buf[32]    = "N/A";

    p = Json_FindValue(http_resp_buf, "\"temp_C\"");
    if (p) Str_CopyDelim(p, temp_buf, 16);

    p = Json_FindValue(http_resp_buf, "\"FeelsLikeC\"");
    if (p) Str_CopyDelim(p, feels_buf, 16);

    p = Json_FindValue(http_resp_buf, "\"humidity\"");
    if (p) Str_CopyDelim(p, humid_buf, 16);

    {
        const char *wd = strstr(http_resp_buf, "\"weatherDesc\"");
        if (wd) {
            p = Json_FindValue(wd, "\"value\"");
            if (p) Str_CopyDelim(p, weather_buf, 64);
        }
    }

    p = Json_FindValue(http_resp_buf, "\"windspeedKmph\"");
    if (p) Str_CopyDelim(p, wind_buf, 32);

    printf("\r\n=========== Weather Report ==========\r\n");
    printf("  City       : Xi'an\r\n");
    printf("  Temperature: %s C (feels %s C)\r\n", temp_buf, feels_buf);
    printf("  Weather    : %s\r\n", weather_buf);
    printf("  Humidity   : %s%%\r\n", humid_buf);
    printf("  Wind Speed : %s km/h\r\n", wind_buf);
    printf("====================================\r\n\r\n");
}


/* ====================== HTTP非阻塞状态机 ========================= */

static void HTTP_StateMachine(void)
{
    switch (http_state)
    {
    case HTTP_STATE_IDLE:
        break;

    /* ---- 步骤1: TCP连接 ---- */
    case HTTP_SATEP_CIPSTART:
    {
        char buf[100];
        strEsp8266_Fram_Record.InfBit.FramLength     = 0;
        strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;

        sprintf(buf, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n",
                pCurrentTask->host, pCurrentTask->port);
        Usart_SendString(macESP8266_USARTx, buf);

        http_state   = HTTP_STATE_WAIT_CONNECT;
        http_timeout = 0;
        http_resp_len = 0;
        memset(http_resp_buf, 0, HTTP_RESP_BUF_SIZE);
        break;
    }

    case HTTP_STATE_WAIT_CONNECT:
        if (strEsp8266_Fram_Record.InfBit.FramFinishFlag)
        {
            strEsp8266_Fram_Record.Data_RX_BUF[
                strEsp8266_Fram_Record.InfBit.FramLength] = '\0';

            if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "OK") ||
                strstr(strEsp8266_Fram_Record.Data_RX_BUF, "ALREADY CONNECT"))
            {
                printf("[HTTP] TCP Connect OK\r\n");
                http_state = HTTP_STATE_CIPSEND;
            }
            else if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "ERROR"))
            {
                printf("[HTTP] TCP Connect FAIL\r\n");
                http_state = HTTP_STATE_ERROR;
            }
            printf("%s", strEsp8266_Fram_Record.Data_RX_BUF);

            strEsp8266_Fram_Record.InfBit.FramLength     = 0;
            strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            http_timeout++;
            if (http_timeout > 60)
            {
                printf("[HTTP] TCP Connect Timeout\r\n");
                http_state = HTTP_STATE_ERROR;
            }
        }
        break;

    /* ---- 步骤2: CIPSEND ---- */
    case HTTP_STATE_CIPSEND:
    {
        char buf[30];
        strEsp8266_Fram_Record.InfBit.FramLength     = 0;
        strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;

        sprintf(buf, "AT+CIPSEND=%d\r\n",
                (int)strlen(pCurrentTask->request));
        Usart_SendString(macESP8266_USARTx, buf);

        http_state   = HTTP_STATE_WAIT_PROMPT;
        http_timeout = 0;
        break;
    }

    case HTTP_STATE_WAIT_PROMPT:
        if (strEsp8266_Fram_Record.InfBit.FramFinishFlag)
        {
            strEsp8266_Fram_Record.Data_RX_BUF[
                strEsp8266_Fram_Record.InfBit.FramLength] = '\0';

            if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, ">"))
                http_state = HTTP_STATE_SEND_HTTP;
            else if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "ERROR"))
            {
                printf("[HTTP] CIPSEND Error\r\n");
                http_state = HTTP_STATE_ERROR;
            }
            printf("%s", strEsp8266_Fram_Record.Data_RX_BUF);

            strEsp8266_Fram_Record.InfBit.FramLength     = 0;
            strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            http_timeout++;
            if (http_timeout > 30)
            {
                printf("[HTTP] Wait \">\" Timeout\r\n");
                http_state = HTTP_STATE_ERROR;
            }
        }
        break;

    /* ---- 步骤3: 发送HTTP GET ---- */
    case HTTP_STATE_SEND_HTTP:
        Usart_SendString(macESP8266_USARTx,
                         (char *)pCurrentTask->request);
        http_state   = HTTP_STATE_WAIT_RESPONSE;
        http_timeout = 0;
        printf("[HTTP] Request Sent\r\n");
        break;

    /* ---- 等待服务器响应 ---- */
    case HTTP_STATE_WAIT_RESPONSE:
        if (strEsp8266_Fram_Record.InfBit.FramFinishFlag)
        {
            uint16_t len = strEsp8266_Fram_Record.InfBit.FramLength;
            strEsp8266_Fram_Record.Data_RX_BUF[len] = '\0';

            printf("%s", strEsp8266_Fram_Record.Data_RX_BUF);

            if (http_resp_len + len < HTTP_RESP_BUF_SIZE - 1)
            {
                memcpy(http_resp_buf + http_resp_len,
                       strEsp8266_Fram_Record.Data_RX_BUF, len);
                http_resp_len += len;
                http_resp_buf[http_resp_len] = '\0';
            }

            if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "CLOSED"))
            {
                printf("\r\n--- HTTP Done ---\r\n");
                http_state = HTTP_STATE_DONE;
            }

            strEsp8266_Fram_Record.InfBit.FramLength     = 0;
            strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
            http_timeout = 0;
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            http_timeout++;
            if (http_timeout > 100)
            {
                printf("[HTTP] Response Timeout\r\n");
                http_state = HTTP_STATE_ERROR;
            }
        }
        break;

    /* ---- 完成 ---- */
    case HTTP_STATE_DONE:
        printf("[HTTP] Request SUCCESS\r\n");
        if (current_task == HTTP_TASK_TIME)
            Parse_Time_Response();
        else if (current_task == HTTP_TASK_WEATHER)
            Parse_Weather_Response();

        current_task  = HTTP_TASK_NONE;
        pCurrentTask  = NULL;
        http_state    = HTTP_STATE_IDLE;
        break;

    /* ---- 出错 ---- */
    case HTTP_STATE_ERROR:
        printf("[HTTP] Request FAILED\r\n");
        current_task  = HTTP_TASK_NONE;
        pCurrentTask  = NULL;
        http_state    = HTTP_STATE_IDLE;
        break;

    default:
        break;
    }
}


/* ========================================================================
 * HTTP_Task -- 串口透传 + HTTP状态机
 *
 * 空闲时: USART1 <-> ESP8266 双向透传
 * 收到 "TIME"/"WEAT" 时: 启动HTTP状态机, 接管ESP8266数据
 * 状态机中使用 vTaskDelay 代替 Delay_ms, 让出CPU给 LVGL_Task
 * ======================================================================== */
#define HTTPTask_STACKSIZE 1024
#define HTTPTask_PRIO      3
TaskHandle_t HTTPTask_Handle;

void HTTP_Task(void *p)
{
    while (1)
    {
        /* ----- 处理USART1命令帧 ----- */
        if (strUSART_Fram_Record.InfBit.FramFinishFlag)
        {
            strUSART_Fram_Record.Data_RX_BUF[
                strUSART_Fram_Record.InfBit.FramLength] = '\0';

            if (strstr(strUSART_Fram_Record.Data_RX_BUF, "TIME"))
            {
                current_task = HTTP_TASK_TIME;
                pCurrentTask = &task_time_cfg;
                http_state   = HTTP_SATEP_CIPSTART;
                printf("[HTTP] === Time Task Start ===\r\n");
            }
            else if (strstr(strUSART_Fram_Record.Data_RX_BUF, "WEAT"))
            {
                current_task = HTTP_TASK_WEATHER;
                pCurrentTask = &task_weather_cfg;
                http_state   = HTTP_SATEP_CIPSTART;
                printf("[HTTP] === Weather Task Start ===\r\n");
            }
            else if (strstr(strUSART_Fram_Record.Data_RX_BUF, "HTTP"))
            {
                current_task = HTTP_TASK_TIME;
                pCurrentTask = &task_time_cfg;
                http_state   = HTTP_SATEP_CIPSTART;
                printf("[HTTP] State Machine Start (Time)\r\n");
            }
            else
            {
                /* 空闲时透传: USART1 -> ESP8266 */
                if (http_state == HTTP_STATE_IDLE)
                    Usart_SendString(macESP8266_USARTx,
                        strUSART_Fram_Record.Data_RX_BUF);
            }

            strUSART_Fram_Record.InfBit.FramLength     = 0;
            strUSART_Fram_Record.InfBit.FramFinishFlag = 0;
        }

        /* ----- HTTP状态机 / ESP8266透传 ----- */
        if (http_state != HTTP_STATE_IDLE)
        {
            HTTP_StateMachine();
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


/* ========================================================================
 * Boot_Task -- 系统自检任务
 * ======================================================================== */
#define BootTask_STACKSIZE 512
#define BootTask_PRIO      2
TaskHandle_t BootTask_Handle;

void Boot_Task(void *p)
{
    xTaskNotify(LVGL_Task_Handle, BOOT_START, eSetValueWithOverwrite);

    /* ---- 自检1: W25Q64 字库 ---- */
    if (!my_font_SCH_16_check_exists())
    {
        printf("Boot: Font not found, burning...\n");
        my_font_SCH_16_init();
        my_font_SCH_16_verify();
        printf("Boot: Font burn done.\n");
    }
    else
    {
        printf("Boot: Font exists, skip.\n");
    }
    xTaskNotify(LVGL_Task_Handle, BOOT_W25Q64_OK, eSetValueWithOverwrite);

    /* ---- 自检2: ESP8266 ---- */
    printf("Boot: Testing ESP8266...\n");
    ESP8266_AT_Test();
    printf("Boot: ESP8266 AT OK.\n");
    xTaskNotify(LVGL_Task_Handle, BOOT_ESP_OK, eSetValueWithOverwrite);

    /* ---- 自检完成 ---- */
    xTaskNotify(LVGL_Task_Handle, BOOT_DONE, eSetValueWithOverwrite);

    vTaskDelete(NULL);
}


/* ========================================================================
 * StartUpTask -- 心跳打印
 * ======================================================================== */
#define StartUpTask_STACKSIZE 256
#define StartUpTask_PRIO      1
TaskHandle_t StartUpTask_Handle;

void StartUpTask(void *p)
{
    while (1)
    {
        vTaskDelay(1000);
        printf("run...\n");
    }
}


/* ========================================================================
 * 主函数
 * ======================================================================== */
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* 1. 串口 USART1 (debug + printf) */
    USART_Config();
    printf("USART OK.\n");

    /* 2. DWT 延时初始化 (ESP8266 驱动依赖) */
    CPU_TS_TmrInit();

    /* 3. W25Q64 Flash */
    W25Q64_FileSysInit();
    printf("  -- Flash ID = 0x%X, Free: %d KB\n",
           (unsigned int)W25Q64_ReadID(),
           (int)(W25Q64_GetFreeSpace() / 1024));

    /* 4. SysTick */
    SysTick_Init();

    /* 5. ESP8266 硬件初始化 */
    ESP8266_Init();
    printf("ESP8266 Init OK.\n");

    /* 6. LVGL_Task (先创建, 确保Boot_Task句柄有效) */
    xTaskCreate(LvglTask, "LVGL_Task",
                LvglTask_STACKSIZE, NULL,
                LvglTask_PRIO, &LVGL_Task_Handle);

    /* 7. Boot_Task */
    xTaskCreate(Boot_Task, "Boot_Task",
                BootTask_STACKSIZE, NULL,
                BootTask_PRIO, &BootTask_Handle);

    /* 8. HTTP_Task (串口透传 + HTTP状态机) */
    xTaskCreate(HTTP_Task, "HTTP_Task",
                HTTPTask_STACKSIZE, NULL,
                HTTPTask_PRIO, &HTTPTask_Handle);

    /* 9. 心跳 */
    xTaskCreate(StartUpTask, "StartUpTask",
                StartUpTask_STACKSIZE, NULL,
                StartUpTask_PRIO, &StartUpTask_Handle);

    /* 10. 启动调度器 */
    vTaskStartScheduler();

    printf("[FATAL] Scheduler start failed!\r\n");
    while (1);
}
/*********************************************END OF FILE**********************/
