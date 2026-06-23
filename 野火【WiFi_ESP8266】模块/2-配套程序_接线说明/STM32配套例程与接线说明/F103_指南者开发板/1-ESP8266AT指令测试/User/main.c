/**
  ******************************************************************************
  * @file    main.c
  * @author  fire
  * @version V1.0
  * @date    2013-xx-xx
  * @brief   串口中断接收测试
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

/* HTTP状态机 */
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

HTTP_StateTypeDef http_state = HTTP_STATE_IDLE;
uint8_t Http_Time_out = 0;      // 状态机超时计数器，每100ms+1

/* HTTP GET 请求内容（以 \r\n\r\n 结尾） */
/* HTTP测试请求 — httpbin.org/get 返回你的IP等JSON数据 */
static const char http_request[] =
  "GET /get HTTP/1.1\r\n"
  "Host: httpbin.org\r\n"
  "Connection: close\r\n"
  "\r\n";

/**
  * @brief  HTTP请求非阻塞状态机，在main()的while循环中调用
  *         通过DEBUG串口(USART1)输入 "HTTP" 字符串来触发
  * @param  无
  * @retval 无
  */
void HTTP_Request_Status_Machine(void)
{
  switch(http_state)
  {
  /* ---------------- 空闲状态 ---------------- */
    case HTTP_STATE_IDLE:
      // 等待外部设置 http_state = HTTP_SATEP_CIPSTART 来触发
      break;

  /* ---------- 步骤1：发送TCP连接命令 ---------- */
    case HTTP_SATEP_CIPSTART:
      // 清空接收缓冲区，发送 AT+CIPSTART 指令（非阻塞方式）
      strEsp8266_Fram_Record .InfBit .FramLength = 0;
      strEsp8266_Fram_Record .InfBit .FramFinishFlag = 0;
      Usart_SendString(macESP8266_USARTx,
          "AT+CIPSTART=\"TCP\",\"httpbin.org\",80\r\n");
      http_state = HTTP_STATE_WAIT_CONNECT;
      Http_Time_out = 0;
      break;

  /* -------- 等待TCP连接应答(OK/ERROR) -------- */
    case HTTP_STATE_WAIT_CONNECT:
      if(strEsp8266_Fram_Record .InfBit .FramFinishFlag)
      {
        // 收到应答帧，补上字符串结尾
        strEsp8266_Fram_Record .Data_RX_BUF[
            strEsp8266_Fram_Record .InfBit .FramLength] = '\0';

        if(strstr(strEsp8266_Fram_Record .Data_RX_BUF, "OK") ||
           strstr(strEsp8266_Fram_Record .Data_RX_BUF, "ALREADY CONNECT"))
        {
          printf("[HTTP] TCP Connect OK\r\n");
          http_state = HTTP_STATE_CIPSEND;
        }
        else if(strstr(strEsp8266_Fram_Record .Data_RX_BUF, "ERROR"))
        {
          printf("[HTTP] TCP Connect FAIL\r\n");
          http_state = HTTP_STATE_ERROR;
        }
        // 同时打印到DEBUG串口供观察
        printf("%s", strEsp8266_Fram_Record .Data_RX_BUF);

        strEsp8266_Fram_Record .InfBit .FramLength = 0;
        strEsp8266_Fram_Record .InfBit .FramFinishFlag = 0;
      }
      else
      {
        // 未收到应答，延时100ms再检查
        Delay_ms(100);
        Http_Time_out++;
        if(Http_Time_out > 60)   // 60*100ms = 6s timeout
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
        // 清空接收缓冲区
        strEsp8266_Fram_Record .InfBit .FramLength = 0;
        strEsp8266_Fram_Record .InfBit .FramFinishFlag = 0;
        // 拼接 AT+CIPSEND=<HTTP报文长度>
        sprintf(cCmdBuf, "AT+CIPSEND=%d\r\n",
                (int)strlen(http_request)); // strlen=70
        Usart_SendString(macESP8266_USARTx, cCmdBuf);
        http_state = HTTP_STATE_WAIT_PROMPT;
        Http_Time_out = 0;
      }
      break;

  /* -------- 等待发送提示符 ">" -------- */
    case HTTP_STATE_WAIT_PROMPT:
      if(strEsp8266_Fram_Record .InfBit .FramFinishFlag)
      {
        strEsp8266_Fram_Record .Data_RX_BUF[
            strEsp8266_Fram_Record .InfBit .FramLength] = '\0';

        if(strstr(strEsp8266_Fram_Record .Data_RX_BUF, ">"))
        {
          // 收到 ">" 提示符，可以发送HTTP报文了
          http_state = HTTP_STATE_SEND_HTTP;
        }
        else if(strstr(strEsp8266_Fram_Record .Data_RX_BUF, "ERROR"))
        {
          printf("[HTTP] CIPSEND Error\r\n");
          http_state = HTTP_STATE_ERROR;
        }
        printf("%s", strEsp8266_Fram_Record .Data_RX_BUF);

        strEsp8266_Fram_Record .InfBit .FramLength = 0;
        strEsp8266_Fram_Record .InfBit .FramFinishFlag = 0;
      }
      else
      {
        Delay_ms(100);
        Http_Time_out++;
        if(Http_Time_out > 30)   // 30*100ms = 3s timeout
        {
          printf("[HTTP] Wait \">\" Timeout\r\n");
          http_state = HTTP_STATE_ERROR;
        }
      }
      break;

  /* ---------- 步骤3：发送HTTP GET请求 ---------- */
    case HTTP_STATE_SEND_HTTP:
      // 收到 ">" 后发送HTTP GET请求
      Usart_SendString(macESP8266_USARTx,
          (char *)http_request);
      http_state = HTTP_STATE_WAIT_RESPONSE;
      Http_Time_out = 0;
      printf("[HTTP] GET Request Sent\r\n");
      break;

  /* ---- 等待服务器响应 + 检测CLOSED结束 ---- */
    case HTTP_STATE_WAIT_RESPONSE:
      if(strEsp8266_Fram_Record .InfBit .FramFinishFlag)
      {
        strEsp8266_Fram_Record .Data_RX_BUF[
            strEsp8266_Fram_Record .InfBit .FramLength] = '\0';

        // 将ESP8266返回的响应数据转发到DEBUG串口观察
        printf("%s", strEsp8266_Fram_Record .Data_RX_BUF);

        // 服务器响应完成后主动关闭TCP连接，ESP8266会输出 CLOSED
        if(strstr(strEsp8266_Fram_Record .Data_RX_BUF,
                  "CLOSED"))
        {
          printf("\r\n--- HTTP Done ---\r\n");
          http_state = HTTP_STATE_DONE;
        }

        strEsp8266_Fram_Record .InfBit .FramLength = 0;
        strEsp8266_Fram_Record .InfBit .FramFinishFlag = 0;
        Http_Time_out = 0;   // 每次收到数据都重置超时
      }
      else
      {
        Delay_ms(100);
        Http_Time_out++;
        if(Http_Time_out > 100)  // 100*100ms = 10s timeout
        {
          printf("[HTTP] Response Timeout\r\n");
          http_state = HTTP_STATE_ERROR;
        }
      }
      break;

  /* -------------- 完成、出错 -------------- */
    case HTTP_STATE_DONE:
      printf("[HTTP] Request SUCCESS\r\n");
      http_state = HTTP_STATE_IDLE;
      break;

    case HTTP_STATE_ERROR:
      printf("[HTTP] Request FAILED\r\n");
      http_state = HTTP_STATE_IDLE;
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
  /*初始化USART 配置模式为 115200 8-N-1，中断接收*/
  USART_Config();
	CPU_TS_TmrInit();       // 初始化DWT时钟计数器，Delay_ms依赖此函数！
	ESP8266_Init();

	printf("\r\n--- ESP8266 AT Test ---\r\n");
	printf("Type AT commands manually to test ESP8266\r\n");
	printf("Type \"HTTP\" to trigger HTTP request state machine\r\n\r\n");

  while(1)
	{
		/* ----- 处理调试串口(USART1)收到的数据 ----- */
		if(strUSART_Fram_Record .InfBit .FramFinishFlag == 1)
		{
			strUSART_Fram_Record .Data_RX_BUF[
			    strUSART_Fram_Record .InfBit .FramLength] = '\0';

			/* 检测是否为HTTP触发命令 */
			if(strstr(strUSART_Fram_Record .Data_RX_BUF, "HTTP"))
			{
				http_state = HTTP_SATEP_CIPSTART;
				printf("[HTTP] State Machine Start\r\n");
			}
			else
			{
				/* 正常转发：调试串口数据 → ESP8266 */
				Usart_SendString(macESP8266_USARTx,
				    strUSART_Fram_Record .Data_RX_BUF);
			}

			strUSART_Fram_Record .InfBit .FramLength = 0;
			strUSART_Fram_Record .InfBit .FramFinishFlag = 0;
	  }

		/* ----- HTTP状态机运行时，接管ESP8266返回数据 ----- */
		if(http_state != HTTP_STATE_IDLE)
		{
			HTTP_Request_Status_Machine();
		}
		else
		{
			/* 空闲时：正常透传 ESP8266数据 → 调试串口 */
			if(strEsp8266_Fram_Record .InfBit .FramFinishFlag)
			{
				strEsp8266_Fram_Record .Data_RX_BUF[
				    strEsp8266_Fram_Record .InfBit .FramLength] = '\0';
				Usart_SendString(DEBUG_USARTx,
				    strEsp8266_Fram_Record .Data_RX_BUF);
				strEsp8266_Fram_Record .InfBit .FramLength = 0;
				strEsp8266_Fram_Record .InfBit .FramFinishFlag = 0;
			}
		}
  }
}
/*********************************************END OF FILE**********************/
