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
#include "MyRTC.h"
#include "task_http.h"    /* WeatherData, g_weather */
#include <stdlib.h>       /* atoi */


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

/* 天气: wttr.in 紧凑格式 → 返回 "temp|humidity|weather_desc"，仅几十字节 */
static const HTTP_TaskConfig task_weather_cfg = {
    "wttr.in",
    80,
    "GET /Xi'an?format=%25t%7C%25h%7C%25C HTTP/1.1\r\n"
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
    char mon[4];
    int  day, year, hour, min, sec;
    int  month;

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

        /*
         * Date header 格式: "Sun, 12 Jul 2026 15:05:24 GMT"
         * %*[^,] 跳过星期几（如 "Sun"），然后跳过 ", "，再解析数字
         */
        if (sscanf(buf, "%*[^,], %d %3s %d %d:%d:%d",
                   &day, mon, &year, &hour, &min, &sec) == 6)
        {
            hour += 8;  /* GMT+8 = Beijing */
            if (hour >= 24) { hour -= 24; day += 1; }

            /* 月份缩写 → 数字 */
            if      (!strcmp(mon,"Jan")) month=1;
            else if (!strcmp(mon,"Feb")) month=2;
            else if (!strcmp(mon,"Mar")) month=3;
            else if (!strcmp(mon,"Apr")) month=4;
            else if (!strcmp(mon,"May")) month=5;
            else if (!strcmp(mon,"Jun")) month=6;
            else if (!strcmp(mon,"Jul")) month=7;
            else if (!strcmp(mon,"Aug")) month=8;
            else if (!strcmp(mon,"Sep")) month=9;
            else if (!strcmp(mon,"Oct")) month=10;
            else if (!strcmp(mon,"Nov")) month=11;
            else                          month=12;

            printf("\r\n=========== Network Time ===========\r\n");
            printf("  GMT    : %s\r\n", buf);
            printf("  Beijing: %04d-%02d-%02d %02d:%02d:%02d\r\n",
                   year, month, day, hour, min, sec);

            /* ★ 写入 RTC ★ */
            {
                RTC_TimeTypeDef t;
                /* 计算星期：蔡勒公式简化（0=Sunday ... 6=Saturday）
                 * 这里不依赖精确的星期，估算即可 */
                t.w_year  = (uint16_t)year;
                t.w_month = (uint8_t)month;
                t.w_date  = (uint8_t)day;
                t.w_hour  = (uint8_t)hour;
                t.w_min   = (uint8_t)min;
                t.w_sec   = (uint8_t)sec;
                MyRTC_SetTime(&t);
                printf("  RTC updated!\r\n");
            }
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
/*
 * 紧凑格式 URL: ?format=%t|%h|%C
 * wttr.in 返回纯文本（无 JSON），仅几十字节，如 "28|66|Smoky+haze"
 * 没有 HTTP 头部（或极少），直接就是 body 内容
 */
static void Parse_Weather_Response(void)
{
    const char *body;
    const char *p;
    char temp_buf[16]    = "N/A";
    char humid_buf[16]   = "N/A";
    char weather_buf[64] = "N/A";
    uint8_t i;
    int   wlen;

    /*
     * buffer 内容形如:
     *   \r\nSEND OK\r\n\r\n+IPD,225:HTTP/1.1 200 OK\r\n...\r\n\r\n+26 C|74%|Smoky hazeCLOSED\r\n
     * 有多个 \r\n\r\n, 最后一个才是 HTTP 头部/正文分隔符。
     * 部分 ESP8266 固件可能只用 \n, 所以 \n\n 作为 fallback。
     */
    {
        const char *scan = http_resp_buf;
        const char *found;
        body = http_resp_buf;

        /* 优先匹配 \r\n\r\n */
        while ((found = strstr(scan, "\r\n\r\n")) != NULL)
        {
            body = found + 4;
            scan = found + 1;   /* 前进至少 1 字节, 避免死循环 */
        }
        /* 如果没找到 \r\n\r\n, 试试 \n\n */
        if (body == http_resp_buf)
        {
            scan = http_resp_buf;
            while ((found = strstr(scan, "\n\n")) != NULL)
            {
                body = found + 2;
                scan = found + 1;
            }
        }
    }

    p = body;

    /* 跳过行首可能的空白 */
    while (*p == ' ' || *p == '\t') p++;

    /* ---- 温度 ---- */
    i = 0;
    while (*p && *p != '|' && *p != '\r' && *p != '\n' && i < 15)
        temp_buf[i++] = *p++;
    temp_buf[i] = '\0';
    if (*p == '|') p++;

    /* ---- 湿度 ---- */
    i = 0;
    while (*p && *p != '|' && *p != '\r' && *p != '\n' && i < 15)
        humid_buf[i++] = *p++;
    humid_buf[i] = '\0';
    if (*p == '|') p++;

    /* ---- 天气描述 ---- */
    i = 0;
    while (*p && *p != '\r' && *p != '\n' && i < 63)
    {
        weather_buf[i++] = (*p == '+') ? ' ' : *p;
        p++;
    }
    weather_buf[i] = '\0';

    /* ★ 去掉尾部可能粘着的 "CLOSED" / "CLOSED" 等 ESP8266 状态字 */
    wlen = (int)strlen(weather_buf);
    if (wlen >= 6 && !strcmp(weather_buf + wlen - 6, "CLOSED"))
        weather_buf[wlen - 6] = '\0';

    printf("\r\n=========== Weather Report ==========\r\n");
    printf("  City       : Xi'an\r\n");
    printf("  Temperature: %s C\r\n", temp_buf);
    printf("  Weather    : %s\r\n", weather_buf);
    printf("  Humidity   : %s%%\r\n", humid_buf);
    printf("====================================\r\n\r\n");

    /* ★ 写入全局天气数据，供 UI 读取 ★ */
    g_weather.temp_C   = (int8_t)atoi(temp_buf);
    g_weather.humidity = (int8_t)atoi(humid_buf);
    strncpy(g_weather.weather_desc, weather_buf, sizeof(g_weather.weather_desc) - 1);
    g_weather.weather_desc[sizeof(g_weather.weather_desc) - 1] = '\0';
    g_weather.valid = 1;
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
