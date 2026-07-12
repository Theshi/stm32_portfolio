/**
 * ============================================================================
 * task_boot.c —— Boot_Task
 *
 * 【职责】
 *   系统上电自检流程:
 *   1. W25Q64 字库校验/烧录
 *   2. ESP8266 AT 指令测试
 *   3. RTC 初始化
 *   4. 通知 HTTP_Task 开始网络流程（WiFi连接→网络对时→天气获取）
 *   5. 每完成一步通过 xTaskNotify 通知 LVGL_Task 更新 UI
 *   6. 所有自检完成后自我删除 (vTaskDelete)
 * ============================================================================
 */
#include "task_boot.h"
#include "task_http.h"
#include "UI_API.h"
#include "W25Q64.h"
#include "bsp_esp8266.h"
#include "MyRTC.h"
#include <stdio.h>


TaskHandle_t BootTask_Handle;

/* WiFi 连接状态变量（由 HTTP_Task 写入，UI_Main.c 读取） */
WIFI_Boot g_wifi_boot = {
    .wifi_name = "",
    .wifi_sign = 0
};


void Boot_Task(void *p)
{
    xTaskNotify(LVGL_Task_Handle, BOOT_START, eSetValueWithOverwrite);

    /* ---- 自检1: W25Q64 字库 ---- */
    xTaskNotify(LVGL_Task_Handle, BOOT_W25Q64_INIT, eSetValueWithOverwrite);
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
    xTaskNotify(LVGL_Task_Handle, BOOT_W25Q64_DONE, eSetValueWithOverwrite);

    vTaskDelay(pdMS_TO_TICKS(50));

    /* ---- 自检2: ESP8266 AT 指令测试 ---- */
    xTaskNotify(LVGL_Task_Handle, BOOT_ESP_WAIT, eSetValueWithOverwrite);
    printf("Boot: Testing ESP8266...\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP8266_AT_Test();
    printf("Boot: ESP8266 AT OK.\n");
    xTaskNotify(LVGL_Task_Handle, BOOT_ESP_AT_OK, eSetValueWithOverwrite);

    vTaskDelay(pdMS_TO_TICKS(50));

    /* ---- 自检3: RTC 初始化 ---- */
    printf("Boot: Testing RTC...\n");
    MyRTC_Init();
    vTaskDelay(pdMS_TO_TICKS(500));
    xTaskNotify(LVGL_Task_Handle, BOOT_RTC_OK, eSetValueWithOverwrite);


    /* ================================================================
     * ★ 通知 HTTP_Task 启动网络流程
     *   HTTP_Task 会在后台依次完成:
     *     ① 连接 WiFi → ② 获取网络时间 → ③ 获取天气
     *   Boot_Task 不等待、不阻塞，直接进入 BOOT_DONE
     * ================================================================ */
    vTaskDelay(pdMS_TO_TICKS(500));
    xTaskNotify(LVGL_Task_Handle, BOOT_CONNECT_WIFI, eSetValueWithOverwrite);
    printf("Boot: Notify HTTP_Task to start network flow...\n");
    xTaskNotify(HTTPTask_Handle, NET_CMD_START, eSetValueWithOverwrite);


    /* ---- 自检完成 ---- */
    printf("Boot: 自检结束\n");
    xTaskNotify(LVGL_Task_Handle, BOOT_DONE, eSetValueWithOverwrite);
    vTaskDelay(pdMS_TO_TICKS(500));
    vTaskDelete(NULL);
}
