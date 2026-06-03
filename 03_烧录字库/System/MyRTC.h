#ifndef __MYRTC_H
#define __MYRTC_H

#include "stm32f10x.h"

/* 时间数据结构体 */
typedef struct
{
    uint16_t w_year;    /* 年 1970~2099 */
    uint8_t  w_month;   /* 月 1~12 */
    uint8_t  w_date;    /* 日 1~31 */
    uint8_t  w_hour;    /* 时 0~23 */
    uint8_t  w_minute;  /* 分 0~59 */
    uint8_t  w_second;  /* 秒 0~59 */
    uint8_t  w_week;    /* 星期 0:星期日 ~ 6:星期六 */
} RTC_TimeTypeDef;

/* 函数声明 */
void     MyRTC_Init(void);
void     MyRTC_CheckAndConfig(void);
void     MyRTC_SetTime(RTC_TimeTypeDef *pTime);
void     MyRTC_GetTime(RTC_TimeTypeDef *pTime);
uint32_t MyRTC_GetCounter(void);
void     MyRTC_SetCounter(uint32_t counterValue);
void     MyRTC_SetAlarm(uint32_t alarmValue);
uint8_t  MyRTC_IsLeapYear(uint16_t year);
uint8_t  MyRTC_GetWeekDay(uint16_t year, uint8_t month, uint8_t day);

#endif /* __MYRTC_H */