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
 *
 * 职责：
 *   - LVGL 初始化 + 显示屏 + 触摸屏
 *   - Boot Screen / Main Screen 的创建与切换
 *   - lv_timer_handler() 刷新循环
 *   - 接收 Boot_Task 通知，更新 UI 状态
 * ======================================================================== */
#define LvglTask_STACKSIZE 512
#define LvglTask_PRIO      1
TaskHandle_t LVGL_Task_Handle;

/* ---- 前向声明 ---- */
void LVGL_CreateMainScreen(lv_obj_t *screen);

void LvglTask(void *p)
{
    /* ---- 步骤1: 初始化 LVGL + 显示屏 + 触摸 ---- */
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    /* ================================================================
     * 步骤2: 创建 Boot Screen（自检画面）
     *
     *  boot_screen
     *  ├── label_logo    （预留）
     *  ├── label_status  （自检状态文字）
     *  └── progress_bar  （预留）
     * ================================================================ */
    lv_obj_t *boot_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(boot_screen, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    lv_obj_t *label_status = lv_label_create(boot_screen);
    lv_obj_set_style_text_color(label_status, lv_color_hex(0x000000), LV_STATE_DEFAULT);
    lv_label_set_text(label_status, "系统自检中...");
    lv_obj_center(label_status);

    /* 加载 Boot Screen */
    lv_scr_load(boot_screen);

    /* ================================================================
     * 步骤3: 预创建 Main Screen（主界面，等 BOOT_DONE 后切换）
     * ================================================================ */
    lv_obj_t *main_screen = lv_obj_create(NULL);
    // Main Screen 的内容在收到 BOOT_DONE 后由 LVGL_CreateMainScreen() 填充

    /* ================================================================
     * 步骤4: 主循环 —— 刷新 LVGL + 接收自检状态通知
     * ================================================================ */
    uint32_t notify_state;

    while (1)
    {
        lv_timer_handler();

        /* 非阻塞接收通知 */
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &notify_state, 0) == pdTRUE)
        {
            switch (notify_state)
            {
                case BOOT_START:
                    lv_label_set_text(label_status, "系统自检中...");
                    break;

                case BOOT_W25Q64_OK:
                    lv_label_set_text(label_status, "W25Q64 字库 OK");
                    printf("UI: W25Q64 字库就绪\n");
                    break;

                case BOOT_ESP_OK:
                    lv_label_set_text(label_status, "ESP8266 OK");
                    printf("UI: ESP8266 就绪\n");
                    break;

                case BOOT_RTC_OK:
                    lv_label_set_text(label_status, "RTC OK");
                    printf("UI: RTC 就绪\n");
                    break;

                case BOOT_DONE:
                    /* 填充 Main Screen 内容并切换 */
                    LVGL_CreateMainScreen(main_screen);
                    lv_scr_load_anim(main_screen,
                                     LV_SCR_LOAD_ANIM_FADE_ON,
                                     500,
                                     0,
                                     false);
                    printf("UI: 自检完成, 切换到主界面\n");
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
 * 在收到 BOOT_DONE 后调用，将所有 UI 放在 main_screen 上
 * ======================================================================== */
void LVGL_CreateMainScreen(lv_obj_t *screen)
{
    /* 设置背景颜色 */
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    /* 屏幕正中央显示"西安" */
    lv_obj_t *label_xian = lv_label_create(screen);
    lv_obj_set_style_text_font(label_xian, &my_font_SCH_16, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_xian, lv_color_hex(0x000000), LV_STATE_DEFAULT);
    lv_label_set_text(label_xian, "西安");
    lv_obj_center(label_xian);

    /* 创建 GitHub 图标 */
    lv_obj_t *img_GitHub = lv_img_create(screen);
    lv_img_set_src(img_GitHub, &GitHub_Logo);
    lv_obj_set_pos(img_GitHub, 265, 213);
    lv_obj_update_layout(img_GitHub);

    /* 创建 WIFI 图标 */
    lv_obj_t *img_WIFI = lv_img_create(screen);
    lv_img_set_src(img_WIFI, &WIFI_Logo);
    lv_obj_set_pos(img_WIFI, 142, 10);
    lv_obj_update_layout(img_WIFI);

    /* 创建 LVGL Logo */
    lv_obj_t *img_LVGL = lv_img_create(screen);
    lv_img_set_src(img_LVGL, &LVGL_Logo);
    lv_obj_set_pos(img_LVGL, 10, 10);
    lv_obj_set_style_bg_opa(img_LVGL, LV_OPA_0, LV_STATE_DEFAULT);
    lv_obj_update_layout(img_LVGL);

    /* 创建 TEMP 温度图标 */
    lv_obj_t *img_TEMP = lv_img_create(screen);
    lv_img_set_src(img_TEMP, &TEMP_Logo);
    lv_obj_set_pos(img_TEMP, 189, 190);
    lv_obj_update_layout(img_TEMP);

    /* 创建分割线 */
    lv_obj_t *line = lv_line_create(screen);
    static lv_point_t line_points[] = { {0, 136}, {320, 136} };
    lv_line_set_points(line, line_points, 2);
    lv_obj_set_style_line_color(line, lv_color_hex(0x000000), 0);
    lv_obj_set_style_line_width(line, 3, 0);
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