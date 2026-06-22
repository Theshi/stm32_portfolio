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

/**
  * @brief  主函数
  * @param  无
  * @retval 无
  */
int main(void)
{	
  /*初始化USART 配置模式为 115200 8-N-1，中断接收*/
  USART_Config();
	ESP8266_Init();
  
	printf("欢迎使用野火STM32开发板\n\n");
	printf("这是一个ESP8266AT指令测试实验\n\n");
	printf("请使用串口调试助手发送\"AT+换行回车\"测试ESP8266是否准备好\n\n");
	printf("更多AT指令请参考模块资料\n\n");
	printf("以下是ESP8266上电初始化打印的信息\n\n");
  
  while(1)
	{	
		if(strUSART_Fram_Record .InfBit .FramFinishFlag == 1)  //如果接收到了串口调试助手的数据
		{
			strUSART_Fram_Record .Data_RX_BUF[strUSART_Fram_Record .InfBit .FramLength] = '\0';
			Usart_SendString(macESP8266_USARTx ,strUSART_Fram_Record .Data_RX_BUF);      //数据从串口调试助手转发到ESP8266
			strUSART_Fram_Record .InfBit .FramLength = 0;                                //接收数据长度置零
			strUSART_Fram_Record .InfBit .FramFinishFlag = 0;                            //接收标志置零
	  }
		if(strEsp8266_Fram_Record .InfBit .FramFinishFlag)                             //如果接收到了ESP8266的数据
		{                                                      
			 strEsp8266_Fram_Record .Data_RX_BUF[strEsp8266_Fram_Record .InfBit .FramLength] = '\0';
			 Usart_SendString(DEBUG_USARTx ,strEsp8266_Fram_Record .Data_RX_BUF);        //数据从ESP8266转发到串口调试助手
			 strEsp8266_Fram_Record .InfBit .FramLength = 0;                             //接收数据长度置零
			 strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;                           //接收标志置零
		}
  }	
}	
/*********************************************END OF FILE**********************/
/*HTTP状态机*/

typedef enum
{
  HTTP_STATE_IDLE ,//空闲状态
  HTTP_SATEP_CIPSTART,//正在链接TCP服务器
  HTTP_STATE_WAIT_CONNECT,//等待TCP服务器连接成功(OK/ALREADY CONNECTED)
  HTTP_STATE_CIPSEND, //正在发送数据
  HTTP_STATE_WAIT_PROMPT,//等待发送数据的提示符(>)
  HTTP_STATE_WAIT_SEND_OK,//等待发送数据成功的回复(OK/SEND OK)
  HTTP_STATE_SEND_HTTP,       // 发送 HTTP GET 请求
  HTTP_STATE_WAIT_RESPONSE,   // 等待服务器响应数据
  HTTP_STATE_DONE,            // 完成
  HTTP_STATE_ERROR            // 出错
} HTTP_StateTypeDef;

HTTP_StateTypeDef http_state = HTTP_STATE_IDLE;
uint8_t Http_Time_out =0;//HTTP状态机的超时计数器

/*在main()的主循环中调用状态机*/
void HTTP_Request_Status_Machine(void)
{
  switch(http_state)
  {
    case HTTP_STATE_IDLE:
      // 空闲状态，可以根据需要触发 HTTP 请求
      break;
      
    case HTTP_SATEP_CIPSTART:
      ESP8266_Cmd("AT+CIPSTART=\"TCP\",\"worldtimeapi.org\",80",
                        "OK", "ALREADY CONNECTED", 5000);// 发送 AT+CIPSTART 命令连接 TCP 服务器
      // Usart_SendString(macESP8266_USARTx, 
      //           "AT+CIPSTART=\"TCP\",\"worldtimeapi.org\",80\r\n");//这边两个发送的发送是不是重复了
      http_state= HTTP_STATE_WAIT_CONNECT;
      Http_Time_out = 0;                  
      break;
      
    case HTTP_STATE_WAIT_CONNECT:
      if(strEsp8266_Fram_Record.InfBit.FramFinishFlag)// 等待服务器连接成功的回复
      {
        
      }
      break;
      
    case HTTP_STATE_CIPSEND:
      // 发送 AT+CIPSEND 命令准备发送数据
      break;
      
    case HTTP_STATE_WAIT_PROMPT:
      // 等待发送数据的提示符 (>)
      break;
      
    case HTTP_STATE_WAIT_SEND_OK:
      // 等待发送数据成功的回复 (OK/SEND OK)
      break;
      
    case HTTP_STATE_SEND_HTTP:
      // 发送 HTTP GET 请求
      break;
      
    case HTTP_STATE_WAIT_RESPONSE:
      // 等待服务器响应数据
      break;
      
    case HTTP_STATE_DONE:
      // 请求完成，可以处理响应数据或重置状态机
      break;
      
    case HTTP_STATE_ERROR:
      // 处理错误情况，可以重置状态机或记录错误信息
      break;
      
    default:
      break;
  }
}