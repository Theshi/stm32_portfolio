/**
 * ============================================================================
 * main.c —— ZenStone 架构：STM32 + FreeRTOS + LVGL + ESP8266
 *
 * 【架构原则】
 *   1. LVGL 单任务原则：所有 LVGL API 只在 LVGL_Task 中调用
 *   2. Boot_Task 只负责硬件自检逻辑，不操作 UI
 *   3. 任务间通过 Task Notification 通信（状态驱动）
 *
 * 【运行流程】
 *   系统启动 → Boot_Task(自检) ──Notify──→ LVGL_Task(UI更新/切屏)
 *   Boot Screen → 自检步骤逐步显示 → BOOT_DONE → Main Screen
 * ============================================================================
 */

#include "stm32f10x.h"                  // Device header
#include "SysTick.h"

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_usart.h"
#include "bsp_ili9341_lcd.h"
#include "W25Q64.h"

#include "lvgl.h"
#include "lv_port_disp_template.h"
#include "lv_port_indev_template.h"


/* ---- 外部字库初始化声明 ---- */
extern bool my_font_SCH_16_check_exists(void);
extern void my_font_SCH_16_init(void);
extern void my_font_SCH_16_verify(void);

/* ---- LVGL 图片源声明 ---- */
LV_IMG_DECLARE(GitHub_Logo);
LV_IMG_DECLARE(WIFI_Logo);
LV_IMG_DECLARE(TEMP_Logo);
LV_IMG_DECLARE(LVGL_Logo);

extern const lv_font_t my_font_SCH_16;  /* 外部字库，字模从 W25Q64 Flash 读取 */


/* ========================================================================
 * 状态驱动设计 —— boot_state_t
 *
 * Boot_Task 每完成一个自检项，就通知 LVGL_Task 更新 UI
 * ======================================================================== */
typedef enum {
    BOOT_START      = 0x01,   /* 自检开始 */
    BOOT_W25Q64_OK  = 0x02,   /* W25Q64 字库就绪 */
    BOOT_ESP_OK     = 0x03,   /* ESP8266 就绪（预留） */
    BOOT_RTC_OK     = 0x04,   /* RTC 就绪（预留）   */
    BOOT_DONE       = 0xFF    /* 全部自检完成 */
} boot_state_t;


/* ---- LVGL_Task 句柄前向声明（Boot_Task 需要通过它发通知） ---- */
extern TaskHandle_t LVGL_Task_Handle;


/* ========================================================================
 * Boot_Task —— 系统自检任务（纯逻辑，不操作 UI）
 *
 * 职责：
 *   - W25Q64 字库检查与烧录
 *   - ESP8266 自检（预留）
 *   - RTC 自检（预留）
 *   - 每完成一项，发送 Task Notification 给 LVGL_Task
 * ======================================================================== */
#define BootTask_STACKSIZE 512
#define BootTask_PRIO      2
TaskHandle_t BootTask_Handle;

void Boot_Task(void *p)
{
    /* ---- 通知 LVGL_Task：自检开始 ---- */
    xTaskNotify(LVGL_Task_Handle, BOOT_START, eSetValueWithOverwrite);

    /* ---- 自检项1: W25Q64 字库 ---- */
    if (!my_font_SCH_16_check_exists()) {
        /* 字库未烧录，执行写入 + 校验 */
        printf("Boot: 字库未找到, 开始烧写...\n");
        my_font_SCH_16_init();
        my_font_SCH_16_verify();
        printf("Boot: 字库写入完毕..\n");
    } else {
        printf("Boot: 字库已存在, 跳过烧写..\n");
    }
    xTaskNotify(LVGL_Task_Handle, BOOT_W25Q64_OK, eSetValueWithOverwrite);

    /* ---- 自检项2: ESP8266（预留） ---- */
    // check_esp8266();
    // xTaskNotify(LVGL_Task_Handle, BOOT_ESP_OK, eSetValueWithOverwrite);

    /* ---- 自检项3: RTC（预留） ---- */
    // check_rtc();
    // xTaskNotify(LVGL_Task_Handle, BOOT_RTC_OK, eSetValueWithOverwrite);

    /* ---- 全部自检完成 ---- */
    xTaskNotify(LVGL_Task_Handle, BOOT_DONE, eSetValueWithOverwrite);

    /* 删除自身，释放栈空间 */
    vTaskDelete(NULL);
}


/* ========================================================================
 * LVGL_Task —— UI 任务（所有 LVGL API 调用的唯一入口）
 * ======================================================================== */
#define LvglTask_STACKSIZE 1024
#define LvglTask_PRIO      1
TaskHandle_t LVGL_Task_Handle;


/* ================================================================
 * Boot Screen 相关静态变量（在 LvglTask 中初始化，通知回调中更新）
 * ================================================================ */
static lv_obj_t *label_log;           /* 自检日志 label      */
static char      boot_log[512];       /* 自检日志缓冲区       */
static lv_obj_t *bar_boot;            /* 进度条               */
static lv_obj_t *label_percent;       /* 进度百分比文字       */


/* ---- BootLog_Add —— 追加一条自检日志到 Boot Screen ---- */
static void BootLog_Add(const char *msg)
{
    strcat(boot_log, msg);
    strcat(boot_log, "\n");
    lv_label_set_text(label_log, boot_log);
}


/* ---- BootProgress_Set —— 更新进度条 + 百分比文字 ---- */
static void BootProgress_Set(uint8_t percent)
{
    lv_bar_set_value(bar_boot, percent, LV_ANIM_ON);
    lv_label_set_text_fmt(label_percent, "%d%%", percent);
}


/* ---- Boot_UI_Create —— 创建 Boot Screen（自检画面） ---- *
 *
 *  boot_screen
 *  ├── label_Zen      "ZenStones"  logo
 *  ├── label_load     "BOOTing ..."
 *  ├── bar_boot       进度条 0~100%
 *  ├── label_percent  "0%" / "25%" / ... / "100%"
 *  └── label_log      自检日志滚动
 */
static lv_obj_t *Boot_UI_Create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);

    /* ---- ZenStones Logo ---- */
    lv_obj_t *label_Zen = lv_label_create(scr);
    lv_label_set_text(label_Zen, "ZenStones");
    lv_obj_set_style_text_font(label_Zen, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_pos(label_Zen, 80, 30);

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


/* ---- 前向声明 ---- */
void LVGL_CreateMainScreen(lv_obj_t *screen);


/* ---- Boot_Finish —— 自检完成，切换到主界面 ---- */
static void Boot_Finish(lv_obj_t *boot_screen, lv_obj_t *main_screen)
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


/* ================================================================
 * LvglTask 主函数
 * ================================================================ */
void LvglTask(void *p)
{
    uint32_t notify_state;

    /* ---- 步骤1: 初始化 LVGL + 显示屏 + 触摸 ---- */
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    /* ---- 步骤2: 创建 Boot Screen 并显示 ---- */
    lv_obj_t *boot_screen = Boot_UI_Create();
    lv_scr_load(boot_screen);

    /* ---- 步骤3: 预创建 Main Screen（等 BOOT_DONE 后填充+切换） ---- */
    lv_obj_t *main_screen = lv_obj_create(NULL);

    /* ================================================================
     * 步骤4: 主循环 —— 刷新 LVGL + 接收自检状态通知
     *
     *   Boot_Task 完成每个自检步骤后发送通知，
     *   此处根据状态值更新 Boot Screen 的日志和进度条。
     * ================================================================ */
    while (1)
    {
        lv_timer_handler();

        /* 非阻塞接收通知 */
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &notify_state, 0) == pdTRUE)
        {
            switch (notify_state)
            {
                case BOOT_START:
                    BootLog_Add("[BOOT] 开始自检...");
                    BootProgress_Set(0);
                    break;

                case BOOT_W25Q64_OK:
                    BootLog_Add("[OK] W25Q64");
                    BootProgress_Set(25);
                    break;

                case BOOT_ESP_OK:
                    BootLog_Add("[OK] ESP8266");
                    BootProgress_Set(50);
                    break;

                case BOOT_RTC_OK:
                    BootLog_Add("[OK] RTC");
                    BootProgress_Set(75);
                    break;

                case BOOT_DONE:
                    BootLog_Add("[OK] 自检完成");
                    BootProgress_Set(100);
                    Boot_Finish(boot_screen, main_screen);
                    break;

                default:
                    break;
            }
        }

        vTaskDelay(5);
    }
}


/* ========================================================================
 * StartUpTask —— 心跳打印
 * ======================================================================== */
#define StartUpTask_STACKSIZE 256
#define StartUpTask_PRIO      1
TaskHandle_t StartUpTask_Handle;

void StartUpTask(void *p)
{
    while (1) {
        vTaskDelay(1000);
        printf("run...\n");
    }
}


/* ========================================================================
 * LVGL_CreateMainScreen —— 构建 Main Screen 的 UI 元素
 *
 *  main_screen
 *  ├── Time_Label       时间 "12:00"
 *  ├── Date_Label       日期 "2024-06-01"
 *  ├── separator        分割线
 *  ├── Temp_Label       "Room-Temp"
 *  ├── Outdoor_Label    "Outdoor"
 *  ├── img_github       GitHub Logo
 *  ├── img_wifi         WIFI  Logo
 *  ├── img_temp         TEMP  Logo
 *  └── img_lvgl         LVGL  Logo
 *
 * 【后续集成】时间/日期文字改为变量动态更新
 * ======================================================================== */
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

    /* ---- 室外温度 "Outdoor" （后续用天气API数据替换） ---- */
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


/* ========================================================================
 * 主函数
 * ======================================================================== */
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* 1. 初始化串口 */
    USART_Config();
    printf("窗口初始化完毕..\n");

    /* 2. 初始化 W25Q64 Flash + 文件系统 */
    W25Q64_FileSysInit();
    printf("  ── Flash ID = 0x%X, 剩余空间: %d KB\n",
           (unsigned int)W25Q64_ReadID(),
           (int)(W25Q64_GetFreeSpace() / 1024));

    /* 3. 初始化 SysTick（FreeRTOS 调度器依赖） */
    SysTick_Init();

    /* 4. 创建 LVGL_Task（先创建，确保 Boot_Task 发通知时 Handle 有效） */
    xTaskCreate(LvglTask, "LVGL_Task",
                LvglTask_STACKSIZE, NULL,
                LvglTask_PRIO, &LVGL_Task_Handle);

    /* 5. 创建 Boot_Task（优先级更高，会抢占 LVGL_Task 先执行自检） */
    xTaskCreate(Boot_Task, "Boot_Task",
                BootTask_STACKSIZE, NULL,
                BootTask_PRIO, &BootTask_Handle);

    /* 6. 创建心跳任务 */
    xTaskCreate(StartUpTask, "StartUpTask",
                StartUpTask_STACKSIZE, NULL,
                StartUpTask_PRIO, &StartUpTask_Handle);

    /* 7. 启动 FreeRTOS 调度器 */
    vTaskStartScheduler();

    /* 调度器启动失败才会到这里 */
    printf("[FATAL] 调度器启动失败！\r\n");
    while (1);
}