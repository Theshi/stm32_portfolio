/**
  ******************************************************************************
  * @file    main.c
  * @author  fire
  * @version V1.0
  * @date    2013-xx-xx
  * @brief   ESP8266 HTTP Weather & Time Clock via State Machine
  ******************************************************************************
  * @attention
  *
  * 实验平台:野火 F103-指南者 STM32 开发板
  * 论坛    :http://www.firebbs.cn
  * 淘宝    :https://fire-stm32.taobao.com
  *
  ******************************************************************************
  */


#include "stm32f10x.h"
#include "bsp_usart.h"
#include "bsp_esp8266.h"
#include "core_delay.h"
#include <string.h>

/* ================================ 任务类型 ================================ */
typedef enum
{
    HTTP_TASK_NONE    = 0,    // 空闲
    HTTP_TASK_TIME,           // 获取网络时间
    HTTP_TASK_WEATHER         // 获取天气
} HTTP_TaskTypeDef;


/* ============================ HTTP任务配置结构体 =========================== */
typedef struct
{
    const char *host;         // 目标服务器地址
    uint16_t    port;         // 端口号
    const char *request;      // 完整的HTTP请求报文 (\r\n\r\n结尾)
} HTTP_TaskConfig;


/* ============================== HTTP状态机枚举 ============================= */
typedef enum
{
    HTTP_STATE_IDLE,              // 空闲状态
    HTTP_SATEP_CIPSTART,          // 步骤1：发送TCP连接命令
    HTTP_STATE_WAIT_CONNECT,      // 等待TCP连接应答 (OK/ALREADY CONNECT)
    HTTP_STATE_CIPSEND,           // 步骤2：发送CIPSEND命令
    HTTP_STATE_WAIT_PROMPT,       // 等待发送提示符 ">"
    HTTP_STATE_SEND_HTTP,         // 步骤3：发送HTTP GET请求
    HTTP_STATE_WAIT_RESPONSE,     // 等待服务器响应数据，检测CLOSED结束
    HTTP_STATE_DONE,              // HTTP请求完成
    HTTP_STATE_ERROR              // 出错处理
} HTTP_StateTypeDef;


/* =========================== 两个HTTP任务配置实例 ========================== */

/*
 * 时间任务：GET httpbin.org/get
 *   从 HTTP 响应头 Date 字段获取GMT时间 (+8h = 北京时间)
 *   从 JSON body 的 "origin" 字段获取公网IP
 */
static const HTTP_TaskConfig task_time_cfg = {
    "httpbin.org",
    80,
    "GET /get HTTP/1.1\r\n"
    "Host: httpbin.org\r\n"
    "Connection: close\r\n"
    "\r\n"
};

/*
 * 天气任务：GET wttr.in/Xi'an?format=j1
 *   wttr.in 支持纯HTTP，无需API Key
 *   返回JSON: current_condition 包含 temp_C, humidity, weatherDesc
 *   城市可修改为任意城市名 (如 Beijing, Shanghai, Guangzhou...)
 */
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
HTTP_TaskTypeDef       current_task = HTTP_TASK_NONE;  // 当前任务类型
const HTTP_TaskConfig *pCurrentTask = NULL;            // 指向当前任务配置
HTTP_StateTypeDef      http_state    = HTTP_STATE_IDLE;
uint8_t                http_timeout  = 0;              // 超时计数器 每100ms+1

/* 响应数据缓存 (DONE后解析用) */
#define HTTP_RESP_BUF_SIZE  1024
static char     http_resp_buf[HTTP_RESP_BUF_SIZE];
static uint16_t http_resp_len = 0;


/* ========================== 响应解析函数(前置声明) ========================= */
static void Parse_Time_Response   (const char *response);
static void Parse_Weather_Response(const char *response);


/* ============================= 字符串提取辅助 ============================= */

/**
  * @brief  在haystack中查找key字符串，返回key后面第一个非空白/非引号字符
  *         例如: 输入 {"temp_C": "28"}, key="temp_C" → 返回指向 "28" 的指针
  * @param  haystack: 被搜索的字符串
  * @param  key      : 要查找的键名
  * @retval 指向值的指针，失败返回NULL
  */
static const char *Json_FindValue(const char *haystack, const char *key)
{
    const char *p = strstr(haystack, key);
    if (!p) return NULL;
    p += strlen(key);                  // 跳过键名
    p = strstr(p, ":");                // 找冒号
    if (!p) return NULL;
    p++;                               // 跳过冒号
    while (*p == ' ' || *p == '"') p++; // 跳过空白和引号
    return p;
}


/**
  * @brief  从value指针开始提取字符串，直到遇到引号或换行
  * @param  src   : 源字符串指针 (应指向值的第一个字符)
  * @param  dst   : 目标缓冲区
  * @param  maxlen: 目标缓冲区最大长度
  * @retval 无
  */
static void Str_CopyUntilQuote(const char *src, char *dst, uint8_t maxlen)
{
    uint8_t i = 0;
    while (src[i] != '\0' && src[i] != '"' && src[i] != '\r' &&
           src[i] != '\n' && i < maxlen - 1)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}


/**
  * @brief  特殊的复制：遇到 " 或 \ 或 \r 或 \n 停止
  *         用于在已转义的JSON字符串中提取值
  */
static void Str_CopyDelim(const char *src, char *dst, uint8_t maxlen)
{
    uint8_t i = 0;
    while (src[i] != '\0' && src[i] != '"' && src[i] != '\\' &&
           src[i] != '\r' && src[i] != '\n' && i < maxlen - 1)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}


/* ============================= 时间响应解析 ============================= */

/**
  * @brief  将GMT时间字符串转换为北京时间并打印
  *         输入: "Tue, 23 Jun 2026 15:18:20 GMT"
  *         北京时间 = GMT + 8小时
  */
static void Parse_Time_Response(const char *response)
{
    const char *p;
    char buf[64];
    char wday[4];
    int  day, year, hour, min, sec;
    char mon[4];

    /* ---- 提取 Date 头 ---- */
    p = strstr(response, "Date: ");
    if (p)
    {
        p += 6;  // skip "Date: "
        Str_CopyUntilQuote(p, buf, 64);

        /* ---- 解析GMT时间各字段 ---- */
        if (sscanf(buf, "%3s %2d %3s %4d %2d:%2d:%2d",
                   wday, &day, mon, &year, &hour, &min, &sec) == 6)
        {
            /* 北京时间 = GMT + 8 */
            hour += 8;
            if (hour >= 24) {
                hour -= 24;
                day += 1;   // 简化处理，不处理月末进位
            }

            printf("\r\n=========== Network Time ===========\r\n");
            printf("  GMT    : %s\r\n", buf);
            printf("  Beijing: %04d-%02d-%02d %02d:%02d:%02d (approx)\r\n",
                   year,
                   (strcmp(mon,"Jan")==0)?1:(strcmp(mon,"Feb")==0)?2:
                   (strcmp(mon,"Mar")==0)?3:(strcmp(mon,"Apr")==0)?4:
                   (strcmp(mon,"May")==0)?5:(strcmp(mon,"Jun")==0)?6:
                   (strcmp(mon,"Jul")==0)?7:(strcmp(mon,"Aug")==0)?8:
                   (strcmp(mon,"Sep")==0)?9:(strcmp(mon,"Oct")==0)?10:
                   (strcmp(mon,"Nov")==0)?11:12,
                   day, hour, min, sec);
            printf("  Note   : Day rollover not handled yet\r\n");
        }
        else
        {
            printf("[TIME] Raw Date: %s\r\n", buf);
            printf("[TIME] Beijing = GMT + 8 hours\r\n");
        }
    }
    else
    {
        printf("[TIME] No Date header found\r\n");
    }

    /* ---- 提取 origin (公网IP) ---- */
    p = Json_FindValue(response, "\"origin\"");
    if (p)
    {
        Str_CopyDelim(p, buf, 64);
        printf("  IP     : %s\r\n", buf);
    }
    printf("====================================\r\n\r\n");
}


/* ============================= 天气响应解析 ============================= */

/**
  * @brief  解析 wttr.in 返回的 j1 格式 JSON
  *         提取 current_condition[0] 中的 temp_C, humidity, weatherDesc
  */
static void Parse_Weather_Response(const char *response)
{
    const char *p;
    char temp_buf[16]     = "N/A";
    char weather_buf[64]  = "N/A";
    char humidity_buf[16] = "N/A";
    char feels_buf[16]    = "N/A";
    char wind_buf[32]     = "N/A";

    /* ---- 温度 ---- */
    p = Json_FindValue(response, "\"temp_C\"");
    if (p) Str_CopyDelim(p, temp_buf, 16);

    /* ---- 体感温度 ---- */
    p = Json_FindValue(response, "\"FeelsLikeC\"");
    if (p) Str_CopyDelim(p, feels_buf, 16);

    /* ---- 湿度 ---- */
    p = Json_FindValue(response, "\"humidity\"");
    if (p) Str_CopyDelim(p, humidity_buf, 16);

    /* ---- 天气描述 ---- */
    {
        const char *wd = strstr(response, "\"weatherDesc\"");
        if (wd)
        {
            p = strstr(wd, "\"value\"");
            if (p)
            {
                p = Json_FindValue(wd, "\"value\"");
                if (p) Str_CopyDelim(p, weather_buf, 64);
            }
        }
    }

    /* ---- 风速 ---- */
    p = Json_FindValue(response, "\"windspeedKmph\"");
    if (p) Str_CopyDelim(p, wind_buf, 32);

    /* ---- 打印天气报告 ---- */
    printf("\r\n=========== Weather Report ==========\r\n");
    printf("  City       : Xi'an\r\n");
    printf("  Temperature: %s C (feels %s C)\r\n", temp_buf, feels_buf);
    printf("  Weather    : %s\r\n", weather_buf);
    printf("  Humidity   : %s%%\r\n", humidity_buf);
    printf("  Wind Speed : %s km/h\r\n", wind_buf);
    printf("====================================\r\n\r\n");
}


/* ========================== HTTP非阻塞状态机 ============================= */

/**
  * @brief  HTTP请求非阻塞状态机，在main()的while循环中调用
  *         通过DEBUG串口(USART1)输入 "TIME" / "WEAT" 来触发
  *
  *         工作流程:
  *         CIPSTART → 等待CONNECT OK → CIPSEND → 等待> → 发送HTTP → 等待CLOSED → DONE
  *
  *         所有URL/端口/报文均通过 pCurrentTask 指针动态访问
  *         同一套状态机同时支持"获取时间"和"获取天气"两种任务
  * @param  无
  * @retval 无
  */
void HTTP_Request_Status_Machine(void)
{
    switch (http_state)
    {
    /* ---------------- 空闲状态 ---------------- */
    case HTTP_STATE_IDLE:
        break;

    /* ---------- 步骤1：发送TCP连接命令 ---------- */
    case HTTP_SATEP_CIPSTART:
    {
        char cCmdBuf[100];
        strEsp8266_Fram_Record.InfBit.FramLength = 0;
        strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;

        /* 动态拼接: AT+CIPSTART="TCP","<host>",<port> */
        sprintf(cCmdBuf, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n",
                pCurrentTask->host, pCurrentTask->port);
        Usart_SendString(macESP8266_USARTx, cCmdBuf);

        http_state = HTTP_STATE_WAIT_CONNECT;
        http_timeout = 0;

        /* 清空响应缓存 */
        http_resp_len = 0;
        memset(http_resp_buf, 0, HTTP_RESP_BUF_SIZE);
        break;
    }

    /* -------- 等待TCP连接应答(OK/ERROR) -------- */
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

            strEsp8266_Fram_Record.InfBit.FramLength = 0;
            strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
        }
        else
        {
            Delay_ms(100);
            http_timeout++;
            if (http_timeout > 60)   // 60*100ms = 6s timeout
            {
                printf("[HTTP] TCP Connect Timeout\r\n");
                http_state = HTTP_STATE_ERROR;
            }
        }
        break;

    /* ---------- 步骤2：发送CIPSEND ---------- */
    case HTTP_STATE_CIPSEND:
    {
        char cCmdBuf[30];
        strEsp8266_Fram_Record.InfBit.FramLength = 0;
        strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;

        /* 动态拼接: AT+CIPSEND=<报文长度> */
        sprintf(cCmdBuf, "AT+CIPSEND=%d\r\n",
                (int)strlen(pCurrentTask->request));
        Usart_SendString(macESP8266_USARTx, cCmdBuf);

        http_state = HTTP_STATE_WAIT_PROMPT;
        http_timeout = 0;
        break;
    }

    /* -------- 等待发送提示符 ">" -------- */
    case HTTP_STATE_WAIT_PROMPT:
        if (strEsp8266_Fram_Record.InfBit.FramFinishFlag)
        {
            strEsp8266_Fram_Record.Data_RX_BUF[
                strEsp8266_Fram_Record.InfBit.FramLength] = '\0';

            if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, ">"))
            {
                http_state = HTTP_STATE_SEND_HTTP;
            }
            else if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "ERROR"))
            {
                printf("[HTTP] CIPSEND Error\r\n");
                http_state = HTTP_STATE_ERROR;
            }
            printf("%s", strEsp8266_Fram_Record.Data_RX_BUF);

            strEsp8266_Fram_Record.InfBit.FramLength = 0;
            strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
        }
        else
        {
            Delay_ms(100);
            http_timeout++;
            if (http_timeout > 30)   // 30*100ms = 3s timeout
            {
                printf("[HTTP] Wait \">\" Timeout\r\n");
                http_state = HTTP_STATE_ERROR;
            }
        }
        break;

    /* ---------- 步骤3：发送HTTP GET请求 ---------- */
    case HTTP_STATE_SEND_HTTP:
        Usart_SendString(macESP8266_USARTx,
                         (char *)pCurrentTask->request);
        http_state = HTTP_STATE_WAIT_RESPONSE;
        http_timeout = 0;
        printf("[HTTP] Request Sent\r\n");
        break;

    /* ---- 等待服务器响应 + 检测CLOSED结束 ---- */
    case HTTP_STATE_WAIT_RESPONSE:
        if (strEsp8266_Fram_Record.InfBit.FramFinishFlag)
        {
            uint16_t len = strEsp8266_Fram_Record.InfBit.FramLength;
            strEsp8266_Fram_Record.Data_RX_BUF[len] = '\0';

            /* 转发到DEBUG串口 */
            printf("%s", strEsp8266_Fram_Record.Data_RX_BUF);

            /* 缓存响应数据供DONE阶段解析 */
            if (http_resp_len + len < HTTP_RESP_BUF_SIZE - 1)
            {
                memcpy(http_resp_buf + http_resp_len,
                       strEsp8266_Fram_Record.Data_RX_BUF, len);
                http_resp_len += len;
                http_resp_buf[http_resp_len] = '\0';
            }

            /* 检测连接关闭 */
            if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "CLOSED"))
            {
                printf("\r\n--- HTTP Done ---\r\n");
                http_state = HTTP_STATE_DONE;
            }

            strEsp8266_Fram_Record.InfBit.FramLength = 0;
            strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
            http_timeout = 0;
        }
        else
        {
            Delay_ms(100);
            http_timeout++;
            if (http_timeout > 100)  // 100*100ms = 10s timeout
            {
                printf("[HTTP] Response Timeout\r\n");
                http_state = HTTP_STATE_ERROR;
            }
        }
        break;

    /* -------------- 完成：按任务类型分发解析 -------------- */
    case HTTP_STATE_DONE:
        printf("[HTTP] Request SUCCESS\r\n");

        /* 根据任务类型调用对应的解析函数 */
        if (current_task == HTTP_TASK_TIME)
        {
            Parse_Time_Response(http_resp_buf);
        }
        else if (current_task == HTTP_TASK_WEATHER)
        {
            Parse_Weather_Response(http_resp_buf);
        }

        /* 复位 */
        current_task = HTTP_TASK_NONE;
        pCurrentTask = NULL;
        http_state   = HTTP_STATE_IDLE;
        break;

    /* -------------- 出错处理 -------------- */
    case HTTP_STATE_ERROR:
        printf("[HTTP] Request FAILED\r\n");
        current_task = HTTP_TASK_NONE;
        pCurrentTask = NULL;
        http_state   = HTTP_STATE_IDLE;
        break;

    default:
        break;
    }
}


/**
  * @brief  主函数
  * @param  无
  * @retval 无
  */
int main(void)
{
    /* 初始化USART 配置模式为 115200 8-N-1，中断接收 */
    USART_Config();
    CPU_TS_TmrInit();       // 初始化DWT时钟计数器，Delay_ms依赖此函数
    ESP8266_Init();

    printf("\r\n=== STM32 Weather & Time Clock ===\r\n");
    printf("AT commands -> manual control ESP8266\r\n");
    printf("\"TIME\" -> Get network time (httpbin.org)\r\n");
    printf("\"WEAT\" -> Get weather (wttr.in)\r\n\r\n");

    while (1)
    {
        /* ----- 处理调试串口(USART1)收到的数据 ----- */
        if (strUSART_Fram_Record.InfBit.FramFinishFlag == 1)
        {
            strUSART_Fram_Record.Data_RX_BUF[
                strUSART_Fram_Record.InfBit.FramLength] = '\0';

            /* ── 检测TIME触发命令 ── */
            if (strstr(strUSART_Fram_Record.Data_RX_BUF, "TIME"))
            {
                current_task  = HTTP_TASK_TIME;
                pCurrentTask  = &task_time_cfg;
                http_state    = HTTP_SATEP_CIPSTART;
                printf("[HTTP] === Time Task Start ===\r\n");
            }
            /* ── 检测WEAT触发命令 ── */
            else if (strstr(strUSART_Fram_Record.Data_RX_BUF, "WEAT"))
            {
                current_task  = HTTP_TASK_WEATHER;
                pCurrentTask  = &task_weather_cfg;
                http_state    = HTTP_SATEP_CIPSTART;
                printf("[HTTP] === Weather Task Start ===\r\n");
            }
            /* ── 保留HTTP作为兼容触发 (默认获取时间) ── */
            else if (strstr(strUSART_Fram_Record.Data_RX_BUF, "HTTP"))
            {
                current_task  = HTTP_TASK_TIME;
                pCurrentTask  = &task_time_cfg;
                http_state    = HTTP_SATEP_CIPSTART;
                printf("[HTTP] State Machine Start (Time)\r\n");
            }
            else
            {
                /* 正常转发：调试串口数据 → ESP8266 */
                Usart_SendString(macESP8266_USARTx,
                                 strUSART_Fram_Record.Data_RX_BUF);
            }

            strUSART_Fram_Record.InfBit.FramLength = 0;
            strUSART_Fram_Record.InfBit.FramFinishFlag = 0;
        }

        /* ----- HTTP状态机运行时，接管ESP8266返回数据 ----- */
        if (http_state != HTTP_STATE_IDLE)
        {
            HTTP_Request_Status_Machine();
        }
        else
        {
            /* 空闲时：正常透传 ESP8266数据 → 调试串口 */
            if (strEsp8266_Fram_Record.InfBit.FramFinishFlag)
            {
                strEsp8266_Fram_Record.Data_RX_BUF[
                    strEsp8266_Fram_Record.InfBit.FramLength] = '\0';
                Usart_SendString(DEBUG_USARTx,
                                 strEsp8266_Fram_Record.Data_RX_BUF);
                strEsp8266_Fram_Record.InfBit.FramLength = 0;
                strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
            }
        }
    }
}
/*********************************************END OF FILE**********************/
