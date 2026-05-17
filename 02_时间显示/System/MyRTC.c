#include "stm32f10x.h"                  // Device header
#include <time.h>
#include "MyRTC.h"

uint16_t MyRTC_Time[] ={2025, 5, 27, 10, 23, 55};//全局时间数组
void RTC_SetTime();//时间设置函数声明，，，，后续这个函数需要与WIFI模块通讯链接，直接从WIFI获取时间设置到RTC中


void MyRTC_Init(void){
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR |RCC_APB1Periph_BKP,ENABLE);//pwr和bkp时钟使能
    PWR_BackupAccessCmd(ENABLE);//允许访问备份寄存器

    if(BKP_ReadBackupRegister(BKP_DR1)!=0x5A5A){
        RCC_LSEConfig(RCC_LSE_ON);//开启外部高速
        while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET);//等待外部高速稳定

        RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
        RCC_RTCCLKCmd(ENABLE);//使能RTC时钟
        RTC_WaitForSynchro();
        RTC_WaitForLastTask();//这两个函数必须在读取RTC数据之前写入

        RTC_SetPrescaler(32768-1);
        RTC_WaitForLastTask();

        RTC_SetTime();
        BKP_WriteBackupRegister(BKP_DR1, 0xA5A5);
    }
    else{
        RTC_WaitForSynchro();								//等待同步
        RTC_ITConfig(RTC_IT_SEC, ENABLE);                   // 重新使能秒中断
		RTC_WaitForLastTask();
    }
}

void MyRTC_SetTime(void)
{
	time_t time_cnt;		//定义秒计数器数据类型
	struct tm time_date;	//定义日期时间数据类型
	
	time_date.tm_year = MyRTC_Time[0] - 1900;		//将数组的时间赋值给日期时间结构体
	time_date.tm_mon = MyRTC_Time[1] - 1;
	time_date.tm_mday = MyRTC_Time[2];
	time_date.tm_hour = MyRTC_Time[3];
	time_date.tm_min = MyRTC_Time[4];
	time_date.tm_sec = MyRTC_Time[5];
	
	time_cnt = mktime(&time_date) - 8 * 60 * 60;	//调用mktime函数，将日期时间转换为秒计数器格式
													//- 8 * 60 * 60为东八区的时区调整
	
	RTC_SetCounter(time_cnt);						//将秒计数器写入到RTC的CNT中
	RTC_WaitForLastTask();							//等待上一次操作完成
}

/**
  * 函    数：RTC读取时间
  * 参    数：无
  * 返 回 值：无
  * 说    明：调用此函数后，RTC硬件电路里时间值将刷新到全局数组
  */
void MyRTC_ReadTime(void)
{
	time_t time_cnt;		//定义秒计数器数据类型
	struct tm time_date;	//定义日期时间数据类型
	
	time_cnt = RTC_GetCounter() + 8 * 60 * 60;		//读取RTC的CNT，获取当前的秒计数器
													//+ 8 * 60 * 60为东八区的时区调整
	
	time_date = *localtime(&time_cnt);				//使用localtime函数，将秒计数器转换为日期时间格式
	
	MyRTC_Time[0] = time_date.tm_year + 1900;		//将日期时间结构体赋值给数组的时间
	MyRTC_Time[1] = time_date.tm_mon + 1;
	MyRTC_Time[2] = time_date.tm_mday;
	MyRTC_Time[3] = time_date.tm_hour;
	MyRTC_Time[4] = time_date.tm_min;
	MyRTC_Time[5] = time_date.tm_sec;
}
