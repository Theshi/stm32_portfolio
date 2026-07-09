/**
 * ============================================================================
 * task_lvglUI.c —— LVGL 任务入口（所有 LVGL API 调用的唯一线程）
 *
 * 职责：
 *   1. 初始化 LVGL + 显示屏 + 触摸屏
 *   2. 创建 Boot Screen 并显示
 *   3. 接收 Boot_Task 的自检通知 → 更新 Boot Screen UI
 *   4. 自检完成后切换到 Main Screen
 *   5. lv_timer_handler() 刷新循环
 * ============================================================================
 */

#include "UI_API.h"
#include "lv_port_disp_template.h"
#include "lv_port_indev_template.h"


/* 全局句柄（Boot_Task 通过 xTaskNotify 发通知时需要） */
TaskHandle_t LVGL_Task_Handle;


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

    /* ---- 步骤3: 预创建 Main Screen 空壳（BOOT_DONE 后填充） ---- */
    lv_obj_t *main_screen = lv_obj_create(NULL);

    /* ================================================================
     * 步骤4: 主循环 —— 刷新 LVGL + 接收自检状态通知
     * ================================================================ */
    while (1)
    {
        lv_timer_handler();

        /* 非阻塞接收 Boot_Task 通知 */
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &notify_state, 0) == pdTRUE)
        {
            switch ((boot_state_t)notify_state)
            {
                case BOOT_START:
                    BootLog_Add("[BOOT] 开始自检...");
                    BootProgress_Set(0);
                    break;
                case BOOT_W25Q64_INIT:
                    BootLog_Add("[BOOT] 检测字库...");
                    BootProgress_Set(10);
                    break;

                case BOOT_W25Q64_DONE:
                    BootLog_Add("[OK] W25Q64");
                    BootProgress_Set(25);
                    break;
                case BOOT_ESP_WAIT:
                    BootLog_Add("[BOOT] 等待 ESP8266 上线...");
                    BootProgress_Set(30);
                    break;
                case BOOT_ESP_AT_OK:
                    BootLog_Add("[OK] ESP8266");
                    BootProgress_Set(55);
                    break;

                case BOOT_RTC_OK:
                    BootLog_Add("[OK] RTC");
                    BootProgress_Set(75);
                    break;
                case BOOT_CONNECT_WIFI:
                    BootLog_Add("[BOOT] 连接 Wi-Fi...");
                    BootProgress_Set(85);
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
