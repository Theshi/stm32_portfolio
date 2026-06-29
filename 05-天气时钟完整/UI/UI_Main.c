/**
 * ============================================================================
 * UI_Main.c —— Main Screen 主界面
 *
 *  main_screen 控件树：
 *  ├── Time_Label       "12:00"        （后续接入 RTC）
 *  ├── Date_Label       "2024-06-01"   （后续接入 RTC）
 *  ├── separator        分割线
 *  ├── Temp_Label       "Room-Temp"    （后续接入传感器）
 *  ├── Outdoor_Label    "Outdoor"      （后续接入天气 API）
 *  ├── img_github       GitHub Logo
 *  ├── img_wifi         WIFI  Logo
 *  ├── img_temp         TEMP  Logo
 *  └── img_lvgl         LVGL  Logo
 * ============================================================================
 */

#include "UI_API.h"


/* ---- LVGL_CreateMainScreen —— 往传入的 screen 上构建主界面 ---- */
void LVGL_CreateMainScreen(lv_obj_t *screen)
{
    /* 设置背景颜色 */
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);

    /* ---- 时间显示 "12:00" （后续用 RTC 数据替换） ---- */
    lv_obj_t *Time_Label = lv_label_create(screen);
    lv_label_set_text(Time_Label, "12:00");
    lv_obj_set_style_text_font(Time_Label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_pos(Time_Label, 80, 50);

    /* ---- 日期显示 "2024-06-01" （后续用 RTC 数据替换） ---- */
    lv_obj_t *Date_Label = lv_label_create(screen);
    lv_label_set_text(Date_Label, "2024-06-01");
    lv_obj_set_style_text_font(Date_Label, &my_font_SCH_16, LV_PART_MAIN);
    lv_obj_set_pos(Date_Label, 100, 100);

    /* ---- 分割线 ---- */
    lv_obj_t *separator = lv_obj_create(screen);
    lv_obj_set_size(separator, 320, 2);
    lv_obj_set_pos(separator, 0, 136);
    lv_obj_set_style_bg_color(separator, lv_color_black(), LV_PART_MAIN);

    /* ---- 室内温度 "Room-Temp" （后续用传感器数据替换） ---- */
    lv_obj_t *Temp_Label = lv_label_create(screen);
    lv_label_set_text(Temp_Label, "Room-Temp");
    lv_obj_set_style_text_font(Temp_Label, &my_font_SCH_16, LV_PART_MAIN);
    lv_obj_set_pos(Temp_Label, 30, 140);

    /* ---- 室外温度 "Outdoor" （后续用天气 API 数据替换） ---- */
    lv_obj_t *Outdoor_Label = lv_label_create(screen);
    lv_label_set_text(Outdoor_Label, "Outdoor");
    lv_obj_set_style_text_font(Outdoor_Label, &my_font_SCH_16, LV_PART_MAIN);
    lv_obj_set_pos(Outdoor_Label, 180, 140);

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
}
