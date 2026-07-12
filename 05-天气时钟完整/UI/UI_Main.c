/**
 * ============================================================================
 * UI_Main.c —— Main Screen 主界面
 *
 *  main_screen 控件树：
 *  ├── WIFI_Label       "----------"  （HTTP_Task 连接后更新为 SSID）
 *  ├── Time_Label       "--:--"        （RTC）
 *  ├── Date_Label       "----/--/--"   （RTC）
 *  ├── separator        分割线
 *  ├── Temp_Label       "Room-Temp"    （DHT11 室内温度）
 *  ├── humi_label                       （DHT11 室内湿度）
 *  ├── "西安"                           （城市名）
 *  ├── OutdoorTemp                       （天气 API 室外温度）
 *  ├── OutdoorWeather                    （天气 API 天气描述）
 *  ├── img_github       GitHub Logo
 *  ├── img_wifi         WIFI  Logo
 *  ├── img_temp         TEMP  Logo
 *  └── img_lvgl         LVGL  Logo
 * ============================================================================
 */

#include "UI_API.h"
#include "MyRTC.h"
#include "bsp_dht11.h"
#include "task_boot.h"    /* g_wifi_boot */
#include "task_http.h"    /* g_weather   */

/* ---- 标签句柄（供 LVGL_RefreshMainScreen 更新） ---- */
static lv_obj_t *s_time_label         = NULL;
static lv_obj_t *s_date_label         = NULL;
static lv_obj_t *s_temp_label         = NULL;  /* 室内温度 —— DHT11   */
static lv_obj_t *s_humi_label         = NULL;  /* 室内湿度 —— DHT11   */
static lv_obj_t *s_wifi_label         = NULL;  /* WiFi 名称            */
static lv_obj_t *s_outdoor_temp_label = NULL;  /* 室外温度 —— 天气API  */
static lv_obj_t *s_outdoor_weather_lbl= NULL;  /* 天气描述 —— 天气API  */
static lv_obj_t *s_outdoor_humi_label = NULL;  /* 室外湿度 —— 天气API  */


/* ---- LVGL_CreateMainScreen —— 往传入的 screen 上构建主界面 ---- */
void LVGL_CreateMainScreen(lv_obj_t *screen)
{
    /* 设置背景颜色 */
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);

    /* ---- WiFi 状态 ---- */
    s_wifi_label = lv_label_create(screen);
    lv_label_set_text(s_wifi_label, "----------");
    lv_obj_set_style_text_font(s_wifi_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_pos(s_wifi_label, 180, 10);

    /* 主界面创建时立即检查 WiFi 状态（可能 HTTP_Task 已经连上了） */
    if (g_wifi_boot.wifi_sign)
        lv_label_set_text(s_wifi_label, g_wifi_boot.wifi_name);

    /* ---- 时间显示 ---- */
    s_time_label = lv_label_create(screen);
    lv_label_set_text(s_time_label, "--:--");
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_pos(s_time_label, 80, 50);

    /* ---- 日期显示 ---- */
    s_date_label = lv_label_create(screen);
    lv_label_set_text(s_date_label, "----/--/--");
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_pos(s_date_label, 100, 105);

    /* ---- 分割线 ---- */
    lv_obj_t *separator = lv_obj_create(screen);
    lv_obj_set_size(separator, 320, 2);
    lv_obj_set_pos(separator, 0, 136);
    lv_obj_set_style_bg_color(separator, lv_color_black(), LV_PART_MAIN);

    /* ---- 室内温度 / 湿度 ---- */
    lv_obj_t *Temp_Label = lv_label_create(screen);
    lv_label_set_text(Temp_Label, "Room-Temp");
    lv_obj_set_style_text_font(Temp_Label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_pos(Temp_Label, 30, 135);

    s_temp_label = lv_label_create(screen);
    lv_label_set_text(s_temp_label, "--.-°C");
    lv_obj_set_style_text_font(s_temp_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_pos(s_temp_label, 50, 165);

    s_humi_label = lv_label_create(screen);
    lv_label_set_text(s_humi_label, "--%");
    lv_obj_set_style_text_font(s_humi_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_pos(s_humi_label, 50, 190);

    /* ---- 室外天气 ---- */
    lv_obj_t *City_Label = lv_label_create(screen);
    lv_label_set_text(City_Label, "西安");
    lv_obj_set_style_text_font(City_Label, &my_font_SCH_16, LV_PART_MAIN);
    lv_obj_set_pos(City_Label, 250, 100);

    s_outdoor_temp_label = lv_label_create(screen);
    lv_label_set_text(s_outdoor_temp_label, "--°C");
    lv_obj_set_style_text_font(s_outdoor_temp_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_pos(s_outdoor_temp_label, 180, 140);

    s_outdoor_weather_lbl = lv_label_create(screen);
    lv_label_set_text(s_outdoor_weather_lbl, "---");
    lv_obj_set_style_text_font(s_outdoor_weather_lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_pos(s_outdoor_weather_lbl, 180, 170);

    s_outdoor_humi_label = lv_label_create(screen);
    lv_label_set_text(s_outdoor_humi_label, "--%");
    lv_obj_set_style_text_font(s_outdoor_humi_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_pos(s_outdoor_humi_label, 180, 210);


    /* ---- GitHub Logo ---- */
    lv_obj_t *img_github = lv_img_create(screen);
    lv_img_set_src(img_github, &GitHub_Logo);
    lv_obj_set_pos(img_github, 270, 220);

    /* ---- WIFI Logo ---- */
    lv_obj_t *img_wifi = lv_img_create(screen);
    lv_img_set_src(img_wifi, &WIFI_Logo);
    lv_obj_set_pos(img_wifi, 150, 10);

    /* ---- TEMP Logo ---- */
    lv_obj_t *img_temp = lv_img_create(screen);
    lv_img_set_src(img_temp, &TEMP_Logo);
    lv_obj_set_pos(img_temp, 250, 150);

    /* ---- LVGL Logo ---- */
    lv_obj_t *img_lvgl = lv_img_create(screen);
    lv_img_set_src(img_lvgl, &LVGL_Logo);
    lv_obj_set_pos(img_lvgl, 10, 10);

    /* ---- LVGL 定时器: 每 30 秒更新显示 ---- */
    lv_timer_create(LVGL_RefreshMainScreen, 30000, NULL);
}


/* ---- LVGL_RefreshMainScreen —— LVGL 定时器回调 ---- */
void LVGL_RefreshMainScreen(lv_timer_t *timer)
{
    RTC_TimeTypeDef now;
    DHT11_Data_Type  Cur_DHT_Date;

    MyRTC_GetTime(&now);
    DHT11_Read_TempAndHumidity(&Cur_DHT_Date);

    /* 更新时间 "HH:MM" */
    lv_label_set_text_fmt(s_time_label, "%02d:%02d", now.w_hour, now.w_min);

    /* 更新日期 "YYYY/MM/DD" */
    lv_label_set_text_fmt(s_date_label, "%04d/%02d/%02d",
                          now.w_year, now.w_month, now.w_date);

    /* 更新室内温湿度 (DHT11) */
    lv_label_set_text_fmt(s_temp_label, "%d.%d C",
                          Cur_DHT_Date.temp_int, Cur_DHT_Date.temp_deci);
    lv_label_set_text_fmt(s_humi_label, "%d.%d %%",
                          Cur_DHT_Date.humi_int, Cur_DHT_Date.humi_deci);

    /* 更新 WiFi 状态 */
    if (g_wifi_boot.wifi_sign)
    {
        lv_label_set_text(s_wifi_label, g_wifi_boot.wifi_name);
    }

    /* 更新室外天气（来自天气 API） */
    if (g_weather.valid)
    {
        lv_label_set_text_fmt(s_outdoor_temp_label, "%d C",
                              g_weather.temp_C);
        lv_label_set_text(s_outdoor_weather_lbl, g_weather.weather_desc);
        lv_label_set_text_fmt(s_outdoor_humi_label, "%d %%",
                              g_weather.humidity);
    }
}
