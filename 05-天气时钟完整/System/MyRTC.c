/**
 * ============================================================================
 * MyRTC.c — STM32 RTC 驱动
 *
 * 基于 STM32 硬件 RTC (LSE 32768Hz) + C 标准库 time.h
 * 读写统一使用 RTC_TimeTypeDef 结构体
 * ============================================================================
 */
#include "MyRTC.h"
#include <time.h>


/* 默认初始时间（首次上电时写入 RTC） */
static const RTC_TimeTypeDef g_default_time = {
    .w_year  = 2026,
    .w_month = 7,
    .w_date  = 7,
    .w_hour  = 12,
    .w_min   = 0,
    .w_sec   = 0,
	.w_week  = 2,
};


/* ============================================================
 * MyRTC_Init — 初始化 RTC 外设
 * ============================================================ */
void MyRTC_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    MyRTC_CheckAndConfig();
}


/* ============================================================
 * MyRTC_CheckAndConfig — 检查备份寄存器标志位
 *   首次上电 → 配置 LSE + 预分频 + 写入默认时间
 *   非首次   → 重新使能秒中断
 * ============================================================ */
void MyRTC_CheckAndConfig(void)
{
    if (BKP_ReadBackupRegister(BKP_DR1) != 0x5A5A)
    {
        /* ---- 首次配置 ---- */
        RCC_LSEConfig(RCC_LSE_ON);
        while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET);

        RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
        RCC_RTCCLKCmd(ENABLE);
        RTC_WaitForSynchro();
        RTC_WaitForLastTask();

        RTC_SetPrescaler(32768 - 1);
        RTC_WaitForLastTask();

        MyRTC_SetTime((RTC_TimeTypeDef *)&g_default_time);
        BKP_WriteBackupRegister(BKP_DR1, 0x5A5A);
    }
    else
    {
        /* ---- 非首次：重新使能秒中断 ---- */
        RTC_WaitForSynchro();
        RTC_ITConfig(RTC_IT_SEC, ENABLE);
        RTC_WaitForLastTask();
    }
}


/* ============================================================
 * MyRTC_SetTime — 将 RTC_TimeTypeDef 时间写入 RTC 硬件
 *   内部通过 mktime() 转为时间戳写入计数器
 * ============================================================ */
void MyRTC_SetTime(RTC_TimeTypeDef *pTime)
{
    time_t     time_cnt;
    struct tm  tm_date;

    tm_date.tm_year = pTime->w_year - 1900;
    tm_date.tm_mon  = pTime->w_month - 1;
    tm_date.tm_mday = pTime->w_date;
    tm_date.tm_hour = pTime->w_hour;
    tm_date.tm_min  = pTime->w_min;
    tm_date.tm_sec  = pTime->w_sec;
    tm_date.tm_isdst = 0;                          /* 不使用夏令时 */

    time_cnt = mktime(&tm_date) - 8 * 60 * 60;     /* UTC → 东八区 */

    RTC_SetCounter((uint32_t)time_cnt);
    RTC_WaitForLastTask();
}


/* ============================================================
 * MyRTC_GetTime — 从 RTC 硬件读取时间到 RTC_TimeTypeDef
 *   内部通过 localtime() 将时间戳转为日历格式
 * ============================================================ */
void MyRTC_GetTime(RTC_TimeTypeDef *pTime)
{
    time_t     time_cnt;
    struct tm *tm_date;

    time_cnt = RTC_GetCounter() + 8 * 60 * 60;     /* 东八区 → UTC */

    tm_date = localtime(&time_cnt);

    pTime->w_year   = tm_date->tm_year + 1900;
    pTime->w_month  = tm_date->tm_mon  + 1;
    pTime->w_date   = tm_date->tm_mday;
    pTime->w_hour   = tm_date->tm_hour;
    pTime->w_min    = tm_date->tm_min;
    pTime->w_sec    = tm_date->tm_sec;
}


/* ============================================================ */
uint32_t MyRTC_GetCounter(void)
{
    return RTC_GetCounter();
}


void MyRTC_SetCounter(uint32_t counterValue)
{
    RTC_SetCounter(counterValue);
    RTC_WaitForLastTask();
}


void MyRTC_SetAlarm(uint32_t alarmValue)
{
    RTC_SetAlarm(alarmValue);
    RTC_WaitForLastTask();
}


/* ============================================================
 * MyRTC_IsLeapYear — 判断闰年
 * ============================================================ */
uint8_t MyRTC_IsLeapYear(uint16_t year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}


/* ============================================================
 * MyRTC_GetWeekDay — Tomohiko Sakamoto 算法
 *   返回 0=星期日 ~ 6=星期六
 * ============================================================ */
uint8_t MyRTC_GetWeekDay(uint16_t year, uint8_t month, uint8_t day)
{
    static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

    year -= (month < 3);
    return (uint8_t)((year + year / 4 - year / 100 + year / 400
                      + t[month - 1] + day) % 7);
}
