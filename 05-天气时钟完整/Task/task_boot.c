/**
 * ============================================================================
 * task_boot.c —— Boot_Task
 *
 * 【职责】
 *   系统上电自检流程:
 *   1. W25Q64 字库校验/烧录
 *   2. ESP8266 AT 指令测试
 *   3. 每完成一步通过 xTaskNotify 通知 LVGL_Task 更新 UI
 *   4. 所有自检完成后自我删除 (vTaskDelete)
 * ============================================================================
 */
#include "task_boot.h"
#include "UI_API.h"
#include "W25Q64.h"
#include "bsp_esp8266.h"
#include <stdio.h>

TaskHandle_t BootTask_Handle;


void Boot_Task(void *p)
{
    xTaskNotify(LVGL_Task_Handle, BOOT_START, eSetValueWithOverwrite);

    /* ---- 自检1: W25Q64 字库 ---- */
    if (!my_font_SCH_16_check_exists())
    {
        printf("Boot: Font not found, burning...\n");
        my_font_SCH_16_init();
        my_font_SCH_16_verify();
        printf("Boot: Font burn done.\n");
    }
    else
    {
        printf("Boot: Font exists, skip.\n");
    }
    xTaskNotify(LVGL_Task_Handle, BOOT_W25Q64_OK, eSetValueWithOverwrite);

    /* ---- 自检2: ESP8266 ---- */
    printf("Boot: Testing ESP8266...\n");
    ESP8266_AT_Test();
    printf("Boot: ESP8266 AT OK.\n");
    xTaskNotify(LVGL_Task_Handle, BOOT_ESP_OK, eSetValueWithOverwrite);

    /* ---- 自检完成 ---- */
    xTaskNotify(LVGL_Task_Handle, BOOT_DONE, eSetValueWithOverwrite);

    vTaskDelete(NULL);
}
