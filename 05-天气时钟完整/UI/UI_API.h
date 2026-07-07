/**
 * ============================================================================
 * UI_API.h —— UI 模块统一对外接口
 *
 * 使用方式：任何需要操作 LVGL UI 的 .c 文件只需 #include "UI_API.h"
 * ============================================================================
 */
#ifndef __UI_API_H
#define __UI_API_H

#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"


/* ========================================================================
 * 状态驱动设计 —— boot_state_t
 *
 * Boot_Task 每完成一个自检项，就通知 LVGL_Task 更新 UI
 * ======================================================================== */
typedef enum {
    BOOT_START        = 0x01,  // → 0%
    BOOT_W25Q64_INIT  = 0x02,  // → 10%  开始检测字库
    BOOT_W25Q64_DONE  = 0x03,  // → 25%  字库就绪
    BOOT_ESP_WAIT     = 0x04,  // → 30%  等待 ESP8266 上线
    BOOT_ESP_AT_OK    = 0x05,  // → 55%  AT 测试通过
    BOOT_RTC_OK       = 0x06,  // → 75%  RTC 初始化完成 (把空位补上)
    BOOT_CONNECT_WIFI = 0x07,  // → 85%  开始连 Wi-Fi
    BOOT_DONE         = 0xFF,  // → 100% 启动完成
} boot_state_t;

/* ---- 外部字库声明 ---- */
extern bool my_font_SCH_16_check_exists(void);
extern void my_font_SCH_16_init(void);
extern void my_font_SCH_16_verify(void);
extern const lv_font_t my_font_SCH_16;

/* ---- LVGL 图片源声明 ---- */
LV_IMG_DECLARE(GitHub_Logo);
LV_IMG_DECLARE(WIFI_Logo);
LV_IMG_DECLARE(TEMP_Logo);
LV_IMG_DECLARE(LVGL_Logo);


/* ========================================================================
 * 任务配置（main.c 创建任务时需要）
 * ======================================================================== */
#define LvglTask_STACKSIZE 1024
#define LvglTask_PRIO      1


/* ========================================================================
 * 跨任务共享变量
 * ======================================================================== */

/* LVGL_Task 句柄（Boot_Task 需要通过它发通知） */
extern TaskHandle_t LVGL_Task_Handle;


/* ========================================================================
 * Boot Screen API（定义在 UI_Boot.c）
 * ======================================================================== */
lv_obj_t *Boot_UI_Create(void);
void      BootLog_Add(const char *msg);
void      BootProgress_Set(uint8_t percent);
void      Boot_Finish(lv_obj_t *boot_screen, lv_obj_t *main_screen);


/* ========================================================================
 * Main Screen API（定义在 UI_Main.c）
 * ======================================================================== */
void LVGL_CreateMainScreen(lv_obj_t *screen);
void LVGL_RefreshMainScreen(lv_timer_t *timer);


/* ========================================================================
 * LVGL Task 入口（定义在 UI_Task.c）
 * ======================================================================== */
void LvglTask(void *p);


#endif /* __UI_API_H */
