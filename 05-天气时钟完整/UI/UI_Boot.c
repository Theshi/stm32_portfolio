/**
 * ============================================================================
 * UI_Boot.c —— Boot Screen 自检画面
 *
 *  boot_screen 控件树：
 *  ├── label_Zen      "ZenStones"  logo
 *  ├── label_load     "BOOTing ..."
 *  ├── bar_boot       进度条 0~100%
 *  ├── label_percent  "0%" → "25%" → ... → "100%"
 *  └── label_log      自检日志（逐条追加）
 * ============================================================================
 */

#include "UI_API.h"
#include <string.h>
#include <stdio.h>


/* ================================================================
 * 模块级静态变量（仅本文件内可见）
 * ================================================================ */
static lv_obj_t *label_log;           /* 自检日志 label      */
static char      boot_log[512];       /* 自检日志缓冲区       */
static lv_obj_t *bar_boot;            /* 进度条               */
static lv_obj_t *label_percent;       /* 进度百分比文字       */


/* ---- BootLog_Add —— 追加一条自检日志 ---- */
void BootLog_Add(const char *msg)
{
    strcat(boot_log, msg);
    strcat(boot_log, "\n");
    lv_label_set_text(label_log, boot_log);
}


/* ---- BootProgress_Set —— 更新进度条 + 百分比文字 ---- */
void BootProgress_Set(uint8_t percent)
{
    lv_bar_set_value(bar_boot, percent, LV_ANIM_ON);
    lv_label_set_text_fmt(label_percent, "%d%%", percent);
}


/* ---- Boot_UI_Create —— 创建 Boot Screen 并返回句柄 ---- */
lv_obj_t *Boot_UI_Create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);

    /* ---- ZenStones Logo ---- */
    lv_obj_t *label_Zen = lv_label_create(scr);
    lv_label_set_text(label_Zen, "ZenStones");
    lv_obj_set_style_text_font(label_Zen, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_pos(label_Zen, 50, 30);

    /* ---- BOOTing 提示 ---- */
    lv_obj_t *label_load = lv_label_create(scr);
    lv_label_set_text(label_load, "BOOTing ...");
    lv_obj_set_style_text_font(label_load, &my_font_SCH_16, LV_PART_MAIN);
    lv_obj_set_pos(label_load, 27, 97);

    /* ---- 进度条 ---- */
    bar_boot = lv_bar_create(scr);
    lv_obj_set_size(bar_boot, 200, 15);
    lv_obj_set_pos(bar_boot, 27, 117);
    lv_obj_set_style_bg_color(bar_boot, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar_boot,
                              lv_palette_main(LV_PALETTE_BLUE),
                              LV_STATE_DEFAULT);
    lv_bar_set_mode(bar_boot, LV_BAR_MODE_NORMAL);
    lv_bar_set_range(bar_boot, 0, 100);
    lv_obj_set_style_anim_time(bar_boot, 500, LV_STATE_DEFAULT);
    lv_bar_set_value(bar_boot, 0, LV_ANIM_OFF);

    /* ---- 进度百分比 ---- */
    label_percent = lv_label_create(scr);
    lv_label_set_text(label_percent, "0%");
    lv_obj_set_style_text_font(label_percent, &my_font_SCH_16, LV_PART_MAIN);
    lv_obj_set_pos(label_percent, 230, 117);

    /* ---- 自检日志区域 ---- */
    label_log = lv_label_create(scr);
    lv_obj_set_style_text_font(label_log, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(label_log, 27, 150);

    /* 初始化日志缓冲区 */
    memset(boot_log, 0, sizeof(boot_log));

    return scr;
}


/* ---- Boot_Finish —— 自检完成，切换到主界面 ---- */
void Boot_Finish(lv_obj_t *boot_screen, lv_obj_t *main_screen)
{
    /* 填充 Main Screen 内容 */
    LVGL_CreateMainScreen(main_screen);

    /* 带淡入动画切屏 */
    lv_scr_load_anim(main_screen,
                     LV_SCR_LOAD_ANIM_FADE_ON,
                     500,
                     0,
                     false);

    /* 删除 Boot Screen，释放内存 */
    lv_obj_del(boot_screen);
    printf("UI: 自检完成, 切换到主界面\n");
}
