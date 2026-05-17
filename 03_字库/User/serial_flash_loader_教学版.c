/**
 * ============================================================================
 * serial_flash_loader_教学版.c —— "串口收存" 完整教学程序（纯文档，不编译）
 * ============================================================================
 *
 * 【本文件的作用】
 *   这是一个独立的"教学参考程序"，不修改你工程中的任何现有文件。
 *   你学习完里面的逻辑后，自己动手把关键代码整合到 W25Q64.c 和 main.c 中。
 *
 *   ⚠️ 本文件不能编译，也**不需要**编译！它是纯文档，仅供阅读学习。
 *      C 语言编译器会跳过 #if 0 ... #endif 之间的所有内容。
 *
 * 【整体思路 —— "快递签收"类比】
 *
 *   你已有的 W25Q64 文件系统像一个"大仓库"，有登记本（目录区）和货架（数据区）。
 *   问题：仓库只接受"一次性整批入库"（W25Q64_FileWrite 需要完整数据在 RAM）。
 *   但字库文件 200KB+，STM32 的 RAM 只有几十 KB，装不下整个字库。
 *
 *   解决方案：给仓库开一扇"小门"（流式写入接口），让快递员（串口）把货物
 *   一包一包（256字节/页）递进来，每次递一包就码放一包到货架上。
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │  PC (fireTools)                STM32                        │
 *   │  ┌──────────┐                 ┌─────────────────────────┐   │
 *   │  │ .bin文件  │──串口 TX──→    │ 串口 RXNE 中断          │   │
 *   │  │ 212KB    │   一包256B     │  ↓                      │   │
 *   │  └──────────┘                 │ 收到256B→W25Q64写一页  │   │
 *   │                               │ 收到256B→W25Q64写一页  │   │
 *   │                               │ ...重复直到全部写完     │   │
 *   │                               └─────────────────────────┘   │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * 【你需要学的内容】
 *   1. USART 接收中断（RXNE 中断）—— 怎么"接住"串口发来的每个字节
 *   2. 缓冲区（Buffer）——           怎么暂存收到的一包数据
 *   3. 流式写入接口（Stream）——     怎么把分片数据写入 W25Q64
 *   4. 状态机（State Machine）——    怎么管理"接收中→写入中→完成"的过程
 *
 * 【使用方式】
 *   Step 1: 阅读本文件的每一段注释，理解逻辑
 *   Step 2: 在纸上画出"数据流向图"（PC→串口→缓冲→W25Q64）
 *   Step 3: 自己照着写一遍（不要复制粘贴）
 *   Step 4: 把你的代码整合到工程中调试
 *
 * 【fireTools 怎么用】
 *   1. 打开 fireTools → 选择"串口调试助手"
 *   2. 选对 COM 口，波特率 115200，数据位 8，停止位 1，无校验
 *   3. 勾选"十六进制发送"（重要！否则会按文本发送，数据损坏）
 *   4. 点击"文件"→ 加载你的 .bin 字库文件
 *   5. 点击"发送"
 *
 * 【文件大小速查表】
 *   GB2312 一级汉字 3755 个
 *     16x16 点阵：3755 × 32 = 120,160 字节 ≈ 117 KB
 *     24x24 点阵：3755 × 72 = 270,360 字节 ≈ 264 KB
 *   GB2312 全部汉字 6763 个
 *     16x16 点阵：6763 × 32 = 216,416 字节 ≈ 211 KB
 *     24x24 点阵：6763 × 72 = 486,936 字节 ≈ 476 KB
 *
 * 【操作步骤】
 *   1. 把本文件中标注的代码整合到你的工程文件里
 *   2. 在 PC 端用 LvglFontTool 生成 .bin 字库
 *   3. 编译下载程序到 STM32
 *   4. 打开串口助手，看到 STM32 输出 "[Loader] 已准备就绪..."
 *   5. 打开 fireTools → 选择 COM 口 → 115200 / 8N1 → 勾选"十六进制发送"
 *   6. 加载 .bin 文件 → 点"发送"
 *   7. 观察 STM32 输出的进度日志
 *   8. 完成后输出 "[Loader] 文件写入成功！"
 *   9. 断电重启 → W25Q64_FileSysInit → W25Q64_FileRead 验证数据
 * ============================================================================
 */

#if 0  /* ══════════════ 编译开关：以下全部不编译，纯教学文档 ══════════════ */
/*  ↑ 这行 #if 0 让编译器跳过整个文件，所以没有任何编译错误。
 *    你只看不编译，学会了再自己敲到工程里。                          */


/*===========================================================================
 * ╔════════════════════════════════════════════════════════════════╗
 * ║  第一部分：需要加到 Driver/W25Q64.h 的函数声明                ║
 * ╚════════════════════════════════════════════════════════════════╝
 *
 * 在你的 Driver/W25Q64.h 文件末尾（#endif 之前）加入以下声明：
 *
 *   // ---------- 流式写入接口 ----------
 *   int32_t  W25Q64_StreamStart(uint16_t id, uint32_t size);
 *   int32_t  W25Q64_StreamFeed(const uint8_t *data, uint16_t len);
 *   int32_t  W25Q64_StreamEnd(void);
 *   uint16_t W25Q64_CRC16_FromFlash(uint32_t addr, uint32_t size);
 *   uint16_t W25Q64_CRC16_Update(uint16_t crc, const uint8_t *data, uint32_t len);
 * ===========================================================================*/


/*===========================================================================
 * ╔════════════════════════════════════════════════════════════════╗
 * ║  第二部分：要加到 Drier/W25Q64.c 的 5 个 static 全局变量     ║
 * ╚════════════════════════════════════════════════════════════════╝
 *
 * 在 Drier/W25Q64.c 文件顶部，找到其他 static 变量的位置
 * （比如 g_DirCache 和 g_DirDirty 那附近），加上这 5 个：
 * ===========================================================================*/

static uint32_t g_FlashAddr        = 0;           // 当前写入的 Flash 起始地址
static uint32_t g_FileID           = 0;           // 正在写入的文件编号
static uint32_t g_FileSize         = 0;           // 文件总大小（字节）
static uint32_t g_BytesWritten     = 0;           // 已写入的字节数
static uint32_t g_LastErasedSector = 0xFFFFFFFF;  // 上一次擦除的扇区号


/*===========================================================================
 * ╔════════════════════════════════════════════════════════════════╗
 * ║  第三部分：要加到 Drier/W25Q64.c 的 3+2 个函数               ║
 * ╚════════════════════════════════════════════════════════════════╝
 *
 * 把下面 5 个函数加到 Drier/W25Q64.c 中。
 * 建议放在 W25Q64_FileWrite 函数的后面。
 *
 * 【重要前提】
 *   这些函数会引用同一个 .c 文件里已有的 static 变量和函数：
 *     g_DirCache[], g_DirDirty, g_NextFreeAddr ——  你已有的 static 变量
 *     W25Q64_FindEntryByID()                     ——  你已有的 static 函数
 *     W25Q64_CalcNextFreeAddr()                  ——  你已有的 static 函数
 *     W25Q64_FindFreeEntry()                     ——  你已有的 static 函数
 *     W25Q64_DirFlush()                          ——  你已有的 static 函数
 *     W25Q64_SectorErase(), W25Q64_PageWrite(),
 *     W25Q64_Read()                              ——  你已有的底层函数
 *     W25Q64_PAGE_SIZE, W25Q64_SECTOR_SIZE       ——  你已有的宏
 *     W25Q64_DATA_START_ADDR, W25Q64_DATA_TOTAL_SIZE
 *
 *   只要放在同一个 .c 文件内，就能直接使用它们。
 * ===========================================================================*/


/* --------------------------------------------------------------------------
 * 函数 1/5: W25Q64_StreamStart —— 打开"小门"，准备接收分片数据
 *
 * 【教学 —— 这个函数干了什么？】 (★ 空间计算 ★)
 *
 *   1. 检查 id 是否已存在，若存在标记删除旧条目
 *   2. 用"空间计算"算法（W25Q64_CalcNextFreeAddr），
 *      遍历所有已用目录项，找到 end_addr 最大的那个 + 1
 *      → 这个位置就是新数据该放的起始地址
 *   3. 检查剩余空间是否够
 *   4. 把起始地址、文件编号、文件大小等信息记下来，供 StreamFeed 使用
 *
 *   【通俗解释 —— "空间计算"】
 *     仓库里已经放了几个箱子（文件），新箱子该放哪里？
 *     答：扫描所有箱子，找到最右边那个箱子的边界，
 *         紧挨着它放新箱子。
 *     这就是"追加写入"策略。
 *
 * 【调用时机】
 *   main.c 收到 PC 发来的"开始写入"指令后调用。
 *
 * @param  id    文件编号（比如 0=16点阵字库, 1=24点阵字库, 2=天气图标）
 * @param  size  文件总大小（字节数，PC 端需要先告诉 STM32 文件多大）
 * @return 0=成功, -1=空间不足, -3=参数无效
 */
int32_t W25Q64_StreamStart(uint16_t id, uint32_t size)
{
    int16_t entry_idx;

    if (size == 0) return -3;

    /* ① 如果 id 已存在，标记删除旧条目（不回收空间，新数据往后追加） */
    entry_idx = W25Q64_FindEntryByID(id);
    if (entry_idx >= 0) {
        g_DirCache[entry_idx].used = 0xA5;  /* 标记"已删除" */
        g_DirDirty = 1;
    }

    /* ② ★ 空间计算：找到新数据该放的起始地址 */
    g_FlashAddr = W25Q64_CalcNextFreeAddr();

    /* ③ 容量检查 */
    if (g_FlashAddr + size > W25Q64_DATA_START_ADDR + W25Q64_DATA_TOTAL_SIZE) {
        printf("[Stream] 空间不足！需要 %lu 字节\n", (unsigned long)size);
        return -1;
    }

    /* ④ 记录文件信息，供 StreamFeed 使用 */
    g_FileID           = id;
    g_FileSize         = size;
    g_BytesWritten     = 0;
    g_LastErasedSector = 0xFFFFFFFF;  /* 标记"还没擦过任何扇区" */

    printf("[Stream] 开始接收文件 id=%u, 大小=%lu 字节, 起始地址=0x%06X\n",
           id, (unsigned long)size, (unsigned int)g_FlashAddr);
    printf("[Stream] 准备接收数据...\n");

    return 0;
}


/* --------------------------------------------------------------------------
 * 函数 2/5: W25Q64_StreamFeed —— 喂一"片"数据到 Flash (★ 最核心！)
 *
 * 【教学 —— 这是整个算法的核心！】
 *
 *   每次调用写入一小片数据（最多 256 字节 = 一页），
 *   复用了 W25Q64_WriteVariableData 里的跨页写入逻辑，
 *   但把一次性写入改成了分次调用。
 *
 *   【通俗解释 —— "货物分装"】
 *
 *     Flash 的页（256 字节）就是最小货架格。货物（数据）可能跨格。
 *     这个函数做的是：把货物拆成正好能放进格子里的小份。
 *
 *     例：当前写入到第 100 字节处（页还剩 156 字节空间），
 *         你要写入 256 字节 → 先写 156 字节填满此页 →
 *         地址跳到下一页 → 再写剩下的 100 字节。
 *
 *   【工作原理 —— 三步】
 *
 *    第一步：溢出检查
 *       已写字节数 + 本次要写的长度 ≤ 文件总大小？
 *
 *    第二步：跨页处理 while 循环
 *       while (还有数据没写完) {
 *           a. 计算当前页还剩多少空间
 *              公式：256 - (cur_addr % 256)
 *              例：cur_addr = 0x000050 → 80 → 剩余 = 176 字节
 *
 *           b. 本次写入量 = min(剩余页空间, 还没写入的数据量)
 *
 *           c. 扇区擦除检查
 *              如果进了新扇区（跨过 4KB 边界），先擦除
 *              （同一扇区内连续写多页，只擦一次）
 *
 *           d. 调用 W25Q64_PageWrite 写入这一"小片"
 *
 *           e. 地址、偏移向前推进
 *       }
 *
 *    第三步：更新全局状态
 *       g_BytesWritten += len
 *       g_LastErasedSector 更新
 *
 *   【参数说明】
 *    - data: 这一片数据的指针（通常指向 g_RxBuf，即 256 字节缓冲数组）
 *    - len:  这一片数据的长度（通常 256 字节，最后一筐可能不足 256）
 *
 *   @return 0=成功, -1=超过文件大小
 */
int32_t W25Q64_StreamFeed(const uint8_t *data, uint16_t len)
{
    uint32_t remain_in_page;    /* 当前页还剩多少字节可用      */
    uint32_t chunk;             /* 这一轮实际写入多少字节      */
    uint32_t offset = 0;        /* data[] 的读取偏移           */
    uint32_t cur_addr;          /* 当前写入的 Flash 地址       */
    uint32_t last_erased = g_LastErasedSector;

    if (data == NULL || len == 0) return 0;

    /* ① 溢出检查 */
    if (g_BytesWritten + len > g_FileSize) {
        printf("[Stream] 错误：数据超量！已写 %lu + 本次 %u > 总大小 %lu\n",
               (unsigned long)g_BytesWritten, len, (unsigned long)g_FileSize);
        return -1;
    }

    cur_addr = g_FlashAddr + g_BytesWritten;  /* 当前写到哪里 */

    /*
     * ② 核心循环：处理跨页
     */
    while (offset < len) {

        /* a. 计算当前页还剩多少空间 */
        remain_in_page = W25Q64_PAGE_SIZE - (cur_addr % W25Q64_PAGE_SIZE);

        /* b. 本次写入 = min(剩余页空间, 还没写入的数据量) */
        chunk = (remain_in_page < (uint32_t)(len - offset))
                    ? remain_in_page
                    : (uint32_t)(len - offset);

        /* c. 扇区擦除检查（进新扇区才擦，避免重复擦） */
        uint32_t cur_sector = cur_addr / W25Q64_SECTOR_SIZE;
        if (cur_sector != last_erased) {
            W25Q64_SectorErase(cur_addr);
            last_erased = cur_sector;
        }

        /* d. 写入这一"小片"到 Flash */
        W25Q64_PageWrite(cur_addr, data + offset, (uint16_t)chunk);

        /* e. 推进指针 */
        cur_addr += chunk;
        offset   += chunk;
    }

    /* ③ 更新全局状态 */
    g_BytesWritten     += len;
    g_LastErasedSector  = last_erased;

    /* ④ 打印进度（每 4KB 打印一次，避免刷屏） */
    if ((g_BytesWritten % 4096) < len || g_BytesWritten >= g_FileSize) {
        printf("[Stream] 进度: %lu / %lu 字节 (%lu%%)\n",
               (unsigned long)g_BytesWritten,
               (unsigned long)g_FileSize,
               (unsigned long)(g_BytesWritten * 100 / g_FileSize));
    }

    return 0;
}


/* --------------------------------------------------------------------------
 * 函数 3/5: W25Q64_StreamEnd —— 关闭"小门"，收尾登记
 *
 * 【教学 —— 这个函数干了什么？】
 *   1. 完整性检查：收到的总字节数 == 预期文件大小？
 *   2. 从 Flash 读回数据，计算 CRC16 校验码（确认写入无误）
 *   3. 在目录区登记这条记录（填写 used/id/size/start_addr/crc16）
 *   4. 把目录缓存回写到 Flash
 *   5. 更新 g_NextFreeAddr（为下一个文件写入做准备）
 *
 *   @return 0=成功, -1=数据不完整, -2=目录已满
 */
int32_t W25Q64_StreamEnd(void)
{
    int16_t entry_idx;
    uint16_t crc;

    /* ① 完整性检查 */
    if (g_BytesWritten != g_FileSize) {
        printf("[Stream] 错误：数据不完整！预期 %lu 字节，实收 %lu 字节\n",
               (unsigned long)g_FileSize, (unsigned long)g_BytesWritten);
        return -1;
    }

    /* ② 从 Flash 读回数据计算 CRC */
    printf("[Stream] 正在校验 CRC...\n");
    crc = W25Q64_CRC16_FromFlash(g_FlashAddr, g_FileSize);

    /* ③ 在目录中找空位登记 */
    entry_idx = W25Q64_FindFreeEntry();
    if (entry_idx < 0) {
        printf("[Stream] 错误：目录已满！\n");
        return -2;
    }

    g_DirCache[entry_idx].used       = 0x5A;
    g_DirCache[entry_idx].id         = g_FileID;
    g_DirCache[entry_idx].size       = g_FileSize;
    g_DirCache[entry_idx].start_addr = g_FlashAddr;
    g_DirCache[entry_idx].crc16      = crc;
    g_DirDirty = 1;

    /* ④ 回写目录到 Flash */
    W25Q64_DirFlush();

    /* ⑤ 更新全局空闲地址 */
    g_NextFreeAddr = W25Q64_CalcNextFreeAddr();

    printf("[Stream] 写入完成！id=%u, 大小=%lu 字节, CRC=0x%04X\n",
           (unsigned int)g_FileID, (unsigned long)g_FileSize, crc);

    return 0;
}


/* --------------------------------------------------------------------------
 * 函数 4/5: W25Q64_CRC16_FromFlash —— 从 Flash 读取并计算 CRC
 *
 * 【教学 —— 为什么要从 Flash 读回来校验？】
 *   流式写入时数据不留在 RAM，所以写完后再从 Flash 分页读回来算 CRC，
 *   用来确认"Flash 里存的数据"和"PC 发来的原始数据"一致。
 *
 *   如果 PC 端也计算了 CRC 并发送过来，两边一比对就能 100% 确认正确。
 */
uint16_t W25Q64_CRC16_FromFlash(uint32_t addr, uint32_t size)
{
    uint16_t crc = 0xFFFF;
    uint8_t  buf[256];        /* 每次读一页，分页计算 */
    uint32_t offset = 0;

    while (offset < size) {
        uint32_t chunk = (size - offset > 256) ? 256 : (size - offset);
        W25Q64_Read(addr + offset, buf, chunk);
        crc = W25Q64_CRC16_Update(crc, buf, chunk);  /* 分步累计 */
        offset += chunk;
    }
    return crc;
}


/* --------------------------------------------------------------------------
 * 函数 5/5: W25Q64_CRC16_Update —— CRC16 分步更新（支持流式计算）
 *
 * 【教学 —— 为什么需要 Update 版本？】
 *   CRC 算法天然支持"分步"模式：
 *     crc = 0xFFFF;
 *     crc = CRC16_Update(crc, 第1包, 256);
 *     crc = CRC16_Update(crc, 第2包, 256);
 *     ...
 *     最终 crc 和一次性算完整数据的结果完全一样！
 */
uint16_t W25Q64_CRC16_Update(uint16_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i, j;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)(data[i] << 8);
        for (j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;  /* CRC-CCITT 多项式 */
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}


/*===========================================================================
 * ╔════════════════════════════════════════════════════════════════╗
 * ║  第四部分：要加到 User/main.c 的代码（~70 行）                ║
 * ╚════════════════════════════════════════════════════════════════╝
 *
 * 下面是"串口接收 + 状态管理"的全部代码，放在 main.c 中。
 * ===========================================================================*/

/* ---------- 4.1 接收缓冲区 —— "快递暂存筐" ---------- */
#define SERIAL_BUF_SIZE     256         /* 一页 = 256 字节 */
static uint8_t  g_RxBuf[SERIAL_BUF_SIZE];   /* 接收筐（暂存从串口收的字节） */
static uint16_t g_RxIdx = 0;               /* 筐指针（当前已装了几个字节） */

/* ---------- 4.2 状态机 —— "入库流程控制" ---------- */
typedef enum {
    STATE_IDLE      = 0,   /* 空闲：等待"开始写入"指令 */
    STATE_RECEIVING = 1,   /* 接收中：正在从串口收数据并写入 Flash */
    STATE_FINISHING = 2,   /* 收尾中：CRC 校验 + 登记目录 */
} StreamState_t;

static StreamState_t g_State = STATE_IDLE;


/* ---------- 4.3 USART1 中断服务函数 —— "签收快递"的核心 ---------- */

/**
 * USART1_IRQHandler —— 串口接收中断
 *
 * 【教学 —— 这个函数的工作流】
 *
 *   每次串口收到 1 个字节，硬件自动触发此中断：
 *
 *   ① 检查是否是 RXNE 中断（接收寄存器非空）
 *   ② 读走收到的字节（必须读！否则中断会一直重复触发）
 *   ③ 如果当前状态是 RECEIVING：
 *        - 把字节放进 g_RxBuf[g_RxIdx]
 *        - g_RxIdx++
 *        - 如果 g_RxIdx == 256（筐满了）
 *            → 调用 W25Q64_StreamFeed(g_RxBuf, 256) 写入 Flash
 *            → 清空 g_RxIdx = 0，继续接下一筐
 *            → 如果 g_BytesWritten >= g_FileSize → 切到 FINISHING 状态
 *   ④ 处理溢出错误（ORE 中断标志）
 *
 * 【重要！⚠️】
 *   - 中断里不能调用 printf！耗时太长会导致丢数据
 *   - W25Q64_StreamFeed 里的 W25Q64_PageWrite 约几十微秒，可以接受
 *   - printf 放在主循环或低优先级任务里调用
 *
 * 【注意】
 *   Keil 的 startup 文件里通常已有弱定义的 USART1_IRQHandler，
 *   你在 main.c 里写了这个函数就会覆盖它。
 *   如果你的工程里中断向量表 (startup_stm32f10x_hd.s) 用的是
 *   其他名字，请对照修改。
 */
void USART1_IRQHandler(void)
{
    /* ① 必须是 RXNE 中断才处理 */
    if (USART_GetITStatus(DEBUG_USARTx, USART_IT_RXNE) != RESET) {

        /* ② 读走收到的字节（读操作自动清除 RXNE 标志位） */
        uint8_t byte = (uint8_t)USART_ReceiveData(DEBUG_USARTx);

        /* ③ 根据状态处理 */
        if (g_State == STATE_RECEIVING) {

            /* 放进筐里 */
            g_RxBuf[g_RxIdx] = byte;
            g_RxIdx++;

            /* 筐满了 → 写入 Flash */
            if (g_RxIdx >= SERIAL_BUF_SIZE) {
                W25Q64_StreamFeed(g_RxBuf, SERIAL_BUF_SIZE);
                g_RxIdx = 0;

                /* 检查是否全部收完了 */
                if (g_BytesWritten >= g_FileSize) {
                    g_State = STATE_FINISHING;
                }
            }
        }
    }

    /* ④ 处理溢出错误（ORE 中断） */
    if (USART_GetITStatus(DEBUG_USARTx, USART_IT_ORE) != RESET) {
        (void)USART_ReceiveData(DEBUG_USARTx);  /* 读一下 DR 清除标志 */
    }
}


/* ---------- 4.4 主循环处理函数 —— "入库管家" ---------- */

/**
 * SerialFlashLoader_Process —— 在主循环中调用，管理收尾状态
 *
 * 【教学 —— 为什么要在主循环处理？】
 *   中断里已经完成了"接收 + 写页"的工作。
 *   但 StreamEnd（含 CRC 计算，耗时较长）应该放在主循环里处理。
 *
 * 【调用方式】
 *   在 main() 的 while(1) 或 FreeRTOS 任务中调用：
 *       while(1) {
 *           SerialFlashLoader_Process();
 *           vTaskDelay(10);
 *       }
 */
void SerialFlashLoader_Process(void)
{
    switch (g_State) {

    case STATE_IDLE:
        /* 空闲，等待"开始写入"命令 */
        break;

    case STATE_RECEIVING:
        /* 收到了全部数据 + 筐已空 → 切换到收尾状态 */
        if (g_BytesWritten >= g_FileSize && g_RxIdx == 0) {
            g_State = STATE_FINISHING;
        }
        break;

    case STATE_FINISHING:
        printf("\n[Loader] 数据接收完毕，正在收尾...\n");

        if (W25Q64_StreamEnd() == 0) {
            printf("[Loader] ✅ 文件写入成功！\n");
        } else {
            printf("[Loader] ❌ 文件写入失败！\n");
        }

        /* 清理状态，回到空闲 */
        g_State  = STATE_IDLE;
        g_RxIdx  = 0;
        memset(g_RxBuf, 0, SERIAL_BUF_SIZE);
        printf("[Loader] 就绪，等待下一个文件...\n");
        break;

    default:
        break;
    }
}


/* ---------- 4.5 启动接收函数 —— "开始接货" ---------- */

/**
 * SerialFlashLoader_StartReceive —— 开始接收文件
 *
 * 【教学 —— 怎么调用？】
 *   方式 1（最简单，用于调试）：
 *     在 main() 初始化时直接调用：
 *       SerialFlashLoader_StartReceive(0, 212992);
 *       // id=0, 大小 212992 字节（GB2312 16x16 点阵）
 *
 *   方式 2（更灵活）：
 *     通过串口发命令，如 "WRITE 0 212992\n"，解析后调用
 *
 * @param  id    文件编号
 * @param  size  文件总大小（字节数，要提前知道）
 */
void SerialFlashLoader_StartReceive(uint16_t id, uint32_t size)
{
    if (g_State != STATE_IDLE) {
        printf("[Loader] 错误：当前正在接收文件，请等待完成\n");
        return;
    }

    if (W25Q64_StreamStart(id, size) != 0) {
        printf("[Loader] 错误：无法开始写入\n");
        return;
    }

    /* 清空接收筐 */
    g_RxIdx = 0;
    memset(g_RxBuf, 0, SERIAL_BUF_SIZE);

    /* 切换到接收状态 */
    g_State = STATE_RECEIVING;

    printf("[Loader] ✅ 已准备就绪，请用 fireTools 发送 .bin 文件\n");
}


/*===========================================================================
 * ╔════════════════════════════════════════════════════════════════╗
 * ║  第五部分：USART_Config() 里需要补的中断使能代码              ║
 * ╚════════════════════════════════════════════════════════════════╝
 *
 * 你现有的 bsp_usart.c → USART_Config() 只初始化了串口 GPIO 和参数，
 * 但没有开启"接收中断"（RXNE）。需要在函数末尾加上：
 *
 *   // 开启 USART 接收中断
 *   USART_ITConfig(DEBUG_USARTx, USART_IT_RXNE, ENABLE);
 *
 *   // 配置 NVIC 中断优先级
 *   NVIC_InitTypeDef NVIC_InitStructure;
 *   NVIC_InitStructure.NVIC_IRQChannel = DEBUG_USART_IRQ;
 *   NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
 *   NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
 *   NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
 *   NVIC_Init(&NVIC_InitStructure);
 *
 *===========================================================================*/


/*===========================================================================
 * ╔════════════════════════════════════════════════════════════════╗
 * ║  第六部分：完整的 main.c 伪代码（整合后长什么样）            ║
 * ╚════════════════════════════════════════════════════════════════╝
 *
 * int main(void)
 * {
 *     NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
 *
 *     USART_Config();           // 串口初始化（含 RXNE 中断使能）
 *     printf("系统启动...\n");
 *
 *     W25Q64_FileSysInit();     // 初始化 W25Q64 文件系统
 *
 *     // 准备接收字库（id=0, 大小看你的 .bin 文件属性）
 *     SerialFlashLoader_StartReceive(0, 212992);
 *
 *     // LVGL 初始化
 *     lv_init();
 *     // ... lv_disp_drv_init, lv_indev_drv_init ...
 *
 *     while(1) {
 *         SerialFlashLoader_Process();  // ← 就这一行！
 *         lv_timer_handler();
 *         vTaskDelay(5);                // 或 HAL_Delay(5)
 *     }
 * }
 *
 *===========================================================================*/


/*===========================================================================
 * ╔════════════════════════════════════════════════════════════════╗
 * ║  学习检查清单 —— 学完后自测                                   ║
 * ╚════════════════════════════════════════════════════════════════╝
 *
 *  □ 串口每收到 1 字节，触发什么？         → USART1_IRQHandler 中断
 *  □ 收到的字节先存在哪里？               → g_RxBuf[]（接收筐）
 *  □ 什么时候把数据写入 Flash？           → 筐满了（256 字节）时
 *  □ StreamFeed 怎么处理跨页？            → 算页剩余空间，while 循环拆分写入
 *  □ 什么时候擦除扇区？                   → 进入新扇区时（每 4KB 一次）
 *  □ 状态机有几个状态？                   → 3 个（IDLE / RECEIVING / FINISHING）
 *  □ StreamEnd 做了什么？                 → 完整性检查、CRC 校验、登记目录
 *  □ 中断函数里能调用 printf 吗？         → 不推荐，耗时太长会丢数据
 *
 *  全部答对 → 可以自己动手写了！
 *===========================================================================*/


#endif  /* ════════════════ #if 0 结束，以下不编译 ════════════════ */