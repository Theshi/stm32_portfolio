/**
 * ============================================================================
 * main.c —— W25Q64 字库烧录测试（纯下载版，无 LVGL）
 *
 * 运行流程：
 *   1. main() 初始化 USART → printf 输出诊断信息
 *   2. 启动 FreeRTOS，只创建 2 个任务：
 *      - StartUpTask: 初始化 W25Q64 文件系统 + 字库寻址 + 串口命令交互
 *      - LoaderTask:  监控下载状态机，收尾时触发 CRC 校验 + 写目录
 *
 * 数据流向：
 *   PC (fireTools 发 .bin) → USART1 RXNE 中断
 *   → g_RxBuf[] 攒够 256 字节
 *   → W25Q64_StreamFeed() 跨页写入 Flash
 *   → 全部收完后 W25Q64_StreamEnd() 校验 CRC + 登记目录
 * ============================================================================
 */

#include "stm32f10x.h"
#include "SysTick.h"

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_usart.h"
#include "W25Q64.h"

/* ---- 外部字体初始化声明 ---- */
extern void my_font_SCH_16_init(void);

/* ========================================================================
 * 串口接收缓冲区 + 下载状态机
 * ======================================================================== */
#define SERIAL_BUF_SIZE  256

static uint8_t  g_RxBuf[SERIAL_BUF_SIZE];
static uint16_t g_RxIdx = 0;
static TickType_t g_LastRxTick = 0;           /* 最后一次收到字节的时刻 */

typedef enum {
    STATE_IDLE       = 0,
    STATE_RECEIVING  = 1,
    STATE_FINISHING  = 2
} StreamState_t;
static StreamState_t g_State = STATE_IDLE;

/* ========================================================================
 *  LoaderTask —— 监控下载状态，收尾时触发 StreamEnd
 * ======================================================================== */
#define LoaderTask_STACKSIZE  256
#define LoaderTask_PRIO       2
TaskHandle_t LoaderTask_Handle;

void LoaderTask(void *p)
{
    TickType_t now;
    while (1) {
        switch (g_State) {
        case STATE_IDLE:
            break;
        case STATE_RECEIVING:
            now = xTaskGetTickCount();
            /* 
             * 空闲超时排空尾数：
             *   串口中断攒数据到 g_RxBuf[]，但最后一段往往不足 256 字节，
             *   永远不会触发 g_RxIdx>=256 的 flush 条件。
             *   如果超过 100ms 没收到新字节，说明 PC 端已经发完了，
             *   此时把缓冲区残余数据喂进去。
             */
            if (g_RxIdx > 0 && (now - g_LastRxTick) > pdMS_TO_TICKS(100)) {
                W25Q64_StreamFeed(g_RxBuf, g_RxIdx);
                g_RxIdx = 0;
            }
            /* 数据全部收完且缓冲区已排空 → 进入收尾状态 */
            if (W25Q64_StreamIsDone() && g_RxIdx == 0) {
                g_State = STATE_FINISHING;
            }
            break;
        case STATE_FINISHING:
            printf("\n[Loader] 全部接收完毕，正在 CRC 校验 + 写目录...\n");
            if (W25Q64_StreamEnd() == 0)
                printf("[Loader] ✅ 字库写入成功！\n\n");
            else
                printf("[Loader] ❌ 写入失败！\n\n");
            g_State = STATE_IDLE;
            g_RxIdx = 0;
            memset(g_RxBuf, 0, SERIAL_BUF_SIZE);
            break;
        }
        vTaskDelay(5);
    }
}

/* ========================================================================
 * 串口中断 —— 逐字节存入缓冲区，攒够一帧喂入 StreamFeed
 * ======================================================================== */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        uint8_t byte = (uint8_t)USART_ReceiveData(USART1);

        if (g_State == STATE_RECEIVING) {
            g_LastRxTick = xTaskGetTickCount();        /* 记录最后收字节时间 */
            g_RxBuf[g_RxIdx++] = byte;

            /* 攒够 256 字节就喂入 Flash */
            if (g_RxIdx >= SERIAL_BUF_SIZE) {
                W25Q64_StreamFeed(g_RxBuf, SERIAL_BUF_SIZE);
                g_RxIdx = 0;
            }
        }
    }

    /* 过载错误处理（直接丢弃，防止死锁） */
    if (USART_GetITStatus(USART1, USART_IT_ORE) != RESET) {
        (void)USART_ReceiveData(USART1);
    }
}

/* ========================================================================
 * 触发串口下载
 * ======================================================================== */
void SerialFlashLoader_Start(uint16_t id, uint32_t size)
{
    if (g_State != STATE_IDLE) {
        printf("[Loader] 忙，请等待上一次传输完成\n");
        return;
    }

    if (W25Q64_StreamStart(id, size) != 0) {
        printf("[Loader] StreamStart 失败\n");
        return;
    }

    g_RxIdx = 0;
    memset(g_RxBuf, 0, SERIAL_BUF_SIZE);
    g_State = STATE_RECEIVING;

    printf("[Loader] 已就绪，请发送文件...\n");
}

/* ========================================================================
 * StartUpTask —— 初始化 Flash + 字库 + 串口命令交互
 * ======================================================================== */
#define StartUpTask_STACKSIZE 512
#define StartUpTask_PRIO      1
TaskHandle_t StartUpTask_Handle;

void StartUpTask(void *p)
{
    /* 初始化 W25Q64 文件系统 */
    W25Q64_FileSysInit();

    /* 从 W25Q64 目录中查找字库文件 (id=0)，记录其 Flash 起始地址 */
    my_font_SCH_16_init();

    printf("\n========================================\n");
    printf("  W25Q64 字库烧录测试\n");
    printf("  剩余空间: %d KB\n", (int)(W25Q64_GetFreeSpace() / 1024));
    printf("========================================\n");
    printf("  3秒后自动触发下载，请准备好 fireTools...\n");
    printf("========================================\n\n");

    /* 给 3 秒时间准备 fireTools */
    for (int i = 3; i > 0; i--) {
        printf("  %d...\r\n", i);
        vTaskDelay(1000);
    }

    /* ================================================
     * 触发字库下载！
     *
     * 参数说明：
     *   id=0           → 数据编号（字库文件在目录中的编号）
     *   size=2526346   → 字库 .bin 文件的总大小（字节）
     *
     * 你需要根据实际字库文件大小修改这个数字！
     * 去文件管理器右键 your_font.bin → 属性 → 看"大小"
     * ================================================ */
    SerialFlashLoader_Start(0, 2527232);

    while (1) {
        vTaskDelay(500);
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* 1. 初始化 USART */
    USART_Config();
    printf("\r\n======== W25Q64 字库烧录测试 ========\r\n");
    printf("[1/2] USART 初始化完毕\r\n");

    /* 2. 初始化 W25Q64 硬件 */
    W25Q64_Init();
    printf("[2/2] W25Q64 硬件初始化完毕, Flash ID = 0x%06X\r\n",
           (unsigned int)W25Q64_ReadID());
    printf("========================================\r\n\r\n");

    /* 3. 启动 FreeRTOS（只创建 2 个任务） */
    SysTick_Init();
    xTaskCreate(StartUpTask, "StartUpTask", StartUpTask_STACKSIZE, NULL,
                StartUpTask_PRIO, &StartUpTask_Handle);
    xTaskCreate(LoaderTask,  "LoaderTask",  LoaderTask_STACKSIZE,  NULL,
                LoaderTask_PRIO,  &LoaderTask_Handle);
    vTaskStartScheduler();

    /* 调度器启动失败才会到这里 */
    printf("[FATAL] 调度器启动失败！\r\n");
    while (1);
}