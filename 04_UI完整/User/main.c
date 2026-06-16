/**
 * ============================================================================
 * main.c —— ZenStone 架构：STM32 + FreeRTOS + LVGL + ESP8266
 *
 * 职责：
 *   - 硬件初始化（USART / W25Q64 / SysTick）
 *   - 创建并启动 FreeRTOS 任务
 *   - Boot_Task：系统自检逻辑（不操作 UI）
 *   - StartUpTask：心跳打印
 *
 * 【UI 模块】所有 LVGL 相关代码已拆分到 UI/ 目录：
 *   UI/UI_API.h   —— 统一接口声明
 *   UI/UI_Task.c  —— LVGL_Task 入口
 *   UI/UI_Boot.c  —— Boot Screen 自检画面
 *   UI/UI_Main.c  —— Main Screen 主界面
 * ============================================================================
 */

#include "stm32f10x.h"
#include "SysTick.h"

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_usart.h"
#include "bsp_ili9341_lcd.h"
#include "W25Q64.h"

#include "UI_API.h"         /* UI 模块统一接口：boot_state_t / LVGL_Task_Handle / 字库声明 */


/* ========================================================================
 * Boot_Task —— 系统自检任务（纯逻辑，不操作 UI）
 * ======================================================================== */
#define BootTask_STACKSIZE 512
#define BootTask_PRIO      2
TaskHandle_t BootTask_Handle;

void Boot_Task(void *p)
{
    /* 通知 LVGL_Task：自检开始 */
    xTaskNotify(LVGL_Task_Handle, BOOT_START, eSetValueWithOverwrite);

    /* ---- 自检项1: W25Q64 字库 ---- */
    if (!my_font_SCH_16_check_exists()) {
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

    /* 全部自检完成 */
    xTaskNotify(LVGL_Task_Handle, BOOT_DONE, eSetValueWithOverwrite);

    vTaskDelete(NULL);
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
 * 主函数
 * ======================================================================== */
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* 1. 初始化串口 */
    USART_Config();
    printf("窗口初始化完毕..\n");

    /* 2. 初始化 W25Q64 Flash */
    W25Q64_FileSysInit();
    printf("  ── Flash ID = 0x%X, 剩余空间: %d KB\n",
           (unsigned int)W25Q64_ReadID(),
           (int)(W25Q64_GetFreeSpace() / 1024));

    /* 3. 初始化 SysTick */
    SysTick_Init();

    /* 4. 先创建 LVGL_Task（确保 Boot_Task 发通知时 Handle 有效） */
    xTaskCreate(LvglTask, "LVGL_Task",
                LvglTask_STACKSIZE, NULL,
                LvglTask_PRIO, &LVGL_Task_Handle);

    /* 5. 创建 Boot_Task（优先级更高，抢占 LVGL_Task 先执行自检） */
    xTaskCreate(Boot_Task, "Boot_Task",
                BootTask_STACKSIZE, NULL,
                BootTask_PRIO, &BootTask_Handle);

    /* 6. 创建心跳任务 */
    xTaskCreate(StartUpTask, "StartUpTask",
                StartUpTask_STACKSIZE, NULL,
                StartUpTask_PRIO, &StartUpTask_Handle);

    /* 7. 启动调度器 */
    vTaskStartScheduler();

    printf("[FATAL] 调度器启动失败！\r\n");
    while (1);
}
