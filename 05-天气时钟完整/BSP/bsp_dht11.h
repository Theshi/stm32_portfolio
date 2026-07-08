#ifndef __DHT11_H
#define __DHT11_H

#include "stm32f10x.h"
//DHT11的数据读取结构体
typedef struct
{
    uint8_t humi_int;  //湿度整数部分
    uint8_t humi_deci;  //湿度小数部分
    uint8_t temp_int;  //温度整数部分
    uint8_t temp_deci;  //温度小数部分
    uint8_t check_sum;  //校验和
}DHT11_Data_Type;

/* 返回值定义 */
#define SUCCESS  1
#define ERROR    0

//封装DHT11的引脚
#define DHT11_GPIO_PORT  GPIOE
#define DHT11_GPIO_Pin   GPIO_Pin_6

#define DHT11_GPIO_CLK   RCC_APB2Periph_GPIOE


//封装DHT11的时序函数
#define DHT11_Output_Low()  GPIO_ResetBits(DHT11_GPIO_PORT,DHT11_GPIO_Pin)
#define DHT11_Output_High() GPIO_SetBits(DHT11_GPIO_PORT,DHT11_GPIO_Pin)

#define DHT11_Input()       GPIO_ReadInputDataBit(DHT11_GPIO_PORT,DHT11_GPIO_Pin)
//DHT11封装函数
uint8_t DHT11_Read_TempAndHumidity(DHT11_Data_Type *DHT11_Data);
void DHT11_Init(void);

#endif
