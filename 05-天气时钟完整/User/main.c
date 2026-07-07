/**
 * ============================================================================
 * main.c —— ZenStone: STM32 + FreeRTOS + LVGL + ESP8266
 *
 * 本文件只做两件事:
 *   1. 板级初始化 (USART → DWT → Flash → SysTick → ESP8266)
 *   2. 创建所有 FreeRTOS 任务 → 启动调度器
 *
 * 各任务实现在独立文件中:
 *   UI_Task       → UI/UI_Task.c     (LVGL 渲染)
 *   Boot_Task     → Task/task_boot.c (上电自检)
 *   HTTP_Task     → Task/task_http.c (串口透传 + HTTP)
 *   StartUpTask   → Task/task_startup.c (心跳)
 *
 * 业务逻辑在 Service/ 层:
 *   http_client   → Service/http_client.c (HTTP 状态机)
 *   http_utils    → Service/http_utils.c  (JSON 查找)
 * ============================================================================
 */

#include "stm32f10x.h"
#include "SysTick.h"

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_usart.h"
#include "bsp_esp8266.h"
#include "core_delay.h"
#include "W25Q64.h"

#include "UI_API.h"
#include "task_boot.h"
#include "task_http.h"
#include "task_startup.h"


/* ========================================================================
 * 主函数
 * ======================================================================== */
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* ---- 1. 串口 USART1 (debug + printf) ---- */
    USART_Config();
    printf("USART OK.\n");

    /* ---- 2. DWT 延时初始化 (替代 SysTick，避免干扰 FreeRTOS 系统节拍) ----
 * ESP8266、W25Q64 等驱动中的 Delay_ms() 均基于 DWT 实现
 */
    CPU_TS_TmrInit();

    /* ---- 3. W25Q64 Flash ---- */
    W25Q64_FileSysInit();
    printf("  -- Flash ID = 0x%X, Free: %d KB\n",
           (unsigned int)W25Q64_ReadID(),
           (int)(W25Q64_GetFreeSpace() / 1024));

    /* ---- 4. SysTick ---- */
    SysTick_Init();

    /* ---- 5. ESP8266 硬件初始化 ---- */
    ESP8266_Init();
    printf("ESP8266 Init OK.\n");

    /* ---- 6. LVGL_Task (先创建, 确保 Boot_Task 句柄有效) ---- */
    xTaskCreate(LvglTask, "LVGL_Task",
                LvglTask_STACKSIZE, NULL,
                LvglTask_PRIO, &LVGL_Task_Handle);

    /* ---- 7. Boot_Task (自检) ---- */
    xTaskCreate(Boot_Task, "Boot_Task",
                BootTask_STACKSIZE, NULL,
                BootTask_PRIO, &BootTask_Handle);

    /* ---- 8. HTTP_Task (串口透传 + HTTP 状态机) ---- */
    xTaskCreate(HTTP_Task, "HTTP_Task",
                HTTPTask_STACKSIZE, NULL,
                HTTPTask_PRIO, &HTTPTask_Handle);

    /* ---- 9. StartUpTask (心跳) ---- */
    xTaskCreate(StartUpTask, "StartUpTask",
                StartUpTask_STACKSIZE, NULL,
                StartUpTask_PRIO, &StartUpTask_Handle);

    /* ---- 10. 启动调度器 ---- */
    vTaskStartScheduler();

    printf("[FATAL] Scheduler start failed!\r\n");
    while (1);
}
/*********************************************END OF FILE**********************/
