/**
 * ============================================================================
 * http_client.c —— HTTP 非阻塞状态机
 *
 * 【设计说明】
 *   这是一个纯逻辑层的 HTTP 客户端，不包含任何 FreeRTOS 任务创建/删除操作。
 *   所有耗时等待都通过 vTaskDelay 让出 CPU，不会阻塞其他任务。
 *
 *   状态机流程:
 *     IDLE → CIPSTART → WAIT_CONNECT → CIPSEND → WAIT_PROMPT
 *         → SEND_HTTP → WAIT_RESPONSE → DONE/ERROR → IDLE
 * ============================================================================
 */
#include "http_client.h"


/* ==================== 任务配置实例 ==================== */

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


/* ==================== 内部状态变量 ==================== */

static HTTP_TaskTypeDef       current_task = HTTP_TASK_NONE;
static const HTTP_TaskConfig *pCurrentTask = NULL;
static HTTP_StateTypeDef      http_state   = HTTP_STATE_IDLE;
static uint8_t                http_timeout = 0;

/* 响应缓存 */
#define HTTP_RESP_BUF_SIZE  1024
static char     http_resp_buf[HTTP_RESP_BUF_SIZE];
static uint16_t http_resp_len = 0;


/* ==================== 内部函数前置声明 ==================== */

static void Parse_Time_Response   (void);
static void Parse_Weather_Response(void);


/* ==================== 时间响应解析 ==================== */

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


/* ==================== 天气响应解析 ==================== */

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


/* ==================== HTTP 非阻塞状态机核心 ==================== */

static void HTTP_StateMachine(void)
{
    switch (http_state)
    {
    case HTTP_STATE_IDLE:
        break;

    /* ---- 步骤1: TCP 连接 ---- */
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

    /* ---- 步骤3: 发送 HTTP GET ---- */
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


/* ==================== 公开 API 实现 ==================== */

void HTTP_StartTask(HTTP_TaskTypeDef type)
{
    current_task = type;
    switch (type) {
    case HTTP_TASK_TIME:
        pCurrentTask = &task_time_cfg;
        printf("[HTTP] === Time Task Start ===\r\n");
        break;
    case HTTP_TASK_WEATHER:
        pCurrentTask = &task_weather_cfg;
        printf("[HTTP] === Weather Task Start ===\r\n");
        break;
    default:
        return;
    }
    http_state = HTTP_SATEP_CIPSTART;
}


void HTTP_StateMachine_Run(void)
{
    HTTP_StateMachine();
}


uint8_t HTTP_IsIdle(void)
{
    return (http_state == HTTP_STATE_IDLE) ? 1 : 0;
}


HTTP_TaskTypeDef HTTP_GetCurrentTask(void)
{
    return current_task;
}


const char *HTTP_GetResponseBuffer(void)
{
    return http_resp_buf;
}
