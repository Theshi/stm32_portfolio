/**
 * ============================================================================
 * main.c —— LVGL UI 显示主程序
 *
 * 运行流程：
 *   1. main() 初始化 USART → W25Q64 Flash → SysTick → 创建 3 个任务
 *   2. FontTransferTask: 检查 W25Q64 中是否已有字库, 若没有则写入并校验,
 *                       通知 LvglTask 后删除自身
 *   3. LvglTask:        等待字库传输完成 → 初始化 LVGL/触摸屏/显示屏 →
 *                       构建主界面 UI → 每5ms 调用 lv_timer_handler() 驱动刷新
 *   4. StartUpTask:     每秒打印心跳 "run..."
 *
 * 【数据流】
 *   编译时: glyph_bitmap[] → MCU 内部 Flash
 *   运行时: FontTransferTask → W25Q64_FileWrite(0, ...) → W25Q64 外部 Flash
 *   显示时: LVGL → __user_font_get_bitmap_flash() → W25Q64_Read() → 渲染
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

/* ---- LVGL_Test 前向声明（定义在 LvglTask 之后） ---- */
void LVGL_Test(void);

/* ---- LVGL 图片源声明 ---- */
LV_IMG_DECLARE(GitHub_Logo);
LV_IMG_DECLARE(WIFI_Logo);
LV_IMG_DECLARE(TEMP_Logo);
LV_IMG_DECLARE(LVGL_Logo);

extern const lv_font_t my_font_SCH_16;  /* 外部字库，字模从 W25Q64 Flash 读取 */

/* ========================================================================
 * 字库传输任务 —— 将 glyph_bitmap[] 写入 W25Q64
 *
 * 【教学】
 *   这是整个"内部 Flash → 外部 Flash"搬运流程的入口。
 *   运行完 my_font_SCH_16_init() + my_font_SCH_16_verify() 之后，
 *   字模数据就安全地存放在 W25Q64 中，LVGL 字体回调通过
 *   __user_font_get_bitmap_flash() 读取。
 *   完成搬运后通知 LvglTask 接管 LVGL 初始化和 UI 构建。
 * ======================================================================== */
#define LvglTask_STACKSIZE 512 
#define LvglTask_PRIO      1
TaskHandle_t LvglTask_Handle;

void LvglTask(void *p)
{
    /* 等待 FontTransferTask 完成字库写入 */
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    /* 初始化 LVGL + 显示屏 + 触摸 */
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    /* 构建主界面 UI */
    LVGL_Test();
    printf("屏幕初始化完毕..\n");

    /* LVGL 刷新循环 */
    while (1) {
        vTaskDelay(5);
        lv_timer_handler();
    }
}

#define FontTransferTask_STACKSIZE 512
#define FontTransferTask_PRIO      2
TaskHandle_t FontTransferTask_Handle;

/* 用于 FontTransferTask → LvglTask 同步的通知值 */
#define FONT_TRANSFER_DONE 0x01

void FontTransferTask(void *p)
{
    /* ---- 步骤1: 检查字库是否已存在于 W25Q64 ---- */
    if (!my_font_SCH_16_check_exists()) {
        /* 字库未烧录，执行写入 + 校验 */
        my_font_SCH_16_init();
        my_font_SCH_16_verify();
        printf("字库写入完毕..\n");
    }

    /* ---- 步骤2: 通知 LvglTask 字库已就绪 ---- */
    xTaskNotifyGive(LvglTask_Handle);

    /* ---- 步骤3: 删除自身，释放栈空间 ---- */
    vTaskDelete(NULL);
}


/* ========================================================================
 * 启动任务 —— 心跳打印
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
 * LVGL_Test —— 主界面 UI 设计
 * ======================================================================== */
void LVGL_Test(void)
{
    /* 设置背景颜色为白色 */
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);

    /* 屏幕正中央显示"西安" */
    lv_obj_t *label_xian = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(label_xian, &my_font_SCH_16, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_xian, lv_color_hex(0x000000), LV_STATE_DEFAULT);
    lv_label_set_text(label_xian, "西安");
    lv_obj_center(label_xian);

    /* 创建 GitHub 图标 */
    lv_obj_t *img_GitHub = lv_img_create(lv_scr_act());
    lv_img_set_src(img_GitHub, &GitHub_Logo);
    lv_obj_set_pos(img_GitHub, 265, 213);
    lv_obj_update_layout(img_GitHub);

    /* 创建 WIFI 图标 */
    lv_obj_t *img_WIFI = lv_img_create(lv_scr_act());
    lv_img_set_src(img_WIFI, &WIFI_Logo);
    lv_obj_set_pos(img_WIFI, 142, 10);
    lv_obj_update_layout(img_WIFI);

    /* 创建 LVGL Logo */
    lv_obj_t *img_LVGL = lv_img_create(lv_scr_act());
    lv_img_set_src(img_LVGL, &LVGL_Logo);
    lv_obj_set_pos(img_LVGL, 10, 10);
    lv_obj_set_style_bg_opa(img_LVGL, LV_OPA_0, LV_STATE_DEFAULT);
    lv_obj_update_layout(img_LVGL);

    /* 创建 TEMP 温度图标 */
    lv_obj_t *img_TEMP = lv_img_create(lv_scr_act());
    lv_img_set_src(img_TEMP, &TEMP_Logo);
    lv_obj_set_pos(img_TEMP, 189, 190);
    lv_obj_update_layout(img_TEMP);

    /* 创建分割线 */
    lv_obj_t *line = lv_line_create(lv_scr_act());
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

    /* 4. 创建任务（FontTransferTask 优先级更高，会先运行） */
    xTaskCreate(FontTransferTask, "FontTransferTask",
                FontTransferTask_STACKSIZE, NULL,
                FontTransferTask_PRIO, &FontTransferTask_Handle);
    xTaskCreate(LvglTask, "LVGL_Tesk",
                LvglTask_STACKSIZE, NULL,
                LvglTask_PRIO, &LvglTask_Handle);
    xTaskCreate(StartUpTask, "StartUpTask",
                StartUpTask_STACKSIZE, NULL,
                StartUpTask_PRIO, &StartUpTask_Handle);

    /* 5. 启动 FreeRTOS 调度器 */
    vTaskStartScheduler();

    /* 调度器启动失败才会到这里 */
    printf("[FATAL] 调度器启动失败！\r\n");
    while (1);
}