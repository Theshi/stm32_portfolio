/**
 * ============================================================================
 * W25Q64 不定量数据写入 —— "空间计算与货物分装"算法教学
 * ============================================================================
 *
 * 【适用场景】
 *   你的字库就是个典型例子：每个汉字的大小不一样（16x16 点阵 = 32 字节），
 *   但字库有几千个汉字，每批烧录的量不固定，你需要一个通用算法来管理 Flash 空间。
 *
 *   更通用的场景：就像快递公司打包——
 *   仓库（W25Q64）被分成很多个"货架格"（页，256 字节），
 *   你要往里面放入"包裹"（你的数据），每个包裹大小都不一样，
 *   你要决定每个包裹放哪个货架格、一个货架格能放几个包裹，
 *   不能让一个包裹跨在两个货架格之间。
 *
 * ============================================================================
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │         第一章：认识你的仓库 —— W25Q64 的结构                    │
 * └─────────────────────────────────────────────────────────────────┘
 *
 *   W25Q64 总容量：8MB = 8,388,608 字节
 *
 *   层次结构（从大到小）：
 *
 *   ┌──────────────────────────────────┐
 *   │  Block (块) = 64KB (65,536 字节) │   ← 擦除单位 1
 *   │  ┌────────────────────────────┐  │
 *   │  │ Sector (扇区) = 4KB (4096) │  │   ← 擦除单位 2 (常用！)
 *   │  │ ┌────────────────────────┐ │  │
 *   │  │ │ Page (页) = 256 字节   │ │  │   ← 写入单位 (必须按页对齐！)
 *   │  │ └────────────────────────┘ │  │
 *   │  │   一个扇区 = 16 页         │  │
 *   │  └────────────────────────────┘  │
 *   │   一个块 = 16 扇区 = 256 页      │
 *   └──────────────────────────────────┘
 *
 *   总共：128 个 Block = 2048 个 Sector = 32768 个 Page
 *
 *   ★ 核心约束（三条铁律）：
 *      铁律1: 写之前必须擦除（擦除后所有位变 1）
 *      铁律2: 擦除最小单位是 Sector（4KB），不能只擦一页
 *      铁律3: 一页内可以跨地址连续写，但不能跨页写（256 字节边界）
 *
 * ============================================================================
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │         第二章：核心思想 —— "货物分装"                            │
 * └─────────────────────────────────────────────────────────────────┘
 *
 *   想象你是一个仓库管理员：
 *
 *   ┌────────────────────────────────────────────────────┐
 *   │                                                    │
 *   │   仓库(W25Q64) 被分成很多个 "货架格" (Page)        │
 *   │   每个货架格固定大小 = 256 字节                     │
 *   │                                                    │
 *   │   你要存放这些 "包裹" (你的数据):                   │
 *   │   ┌────┐  ┌──────┐  ┌──┐  ┌────────┐  ┌─────┐     │
 *   │   │500B│  │ 100B │  │2KB│  │  32B   │  │700B │     │
 *   │   └────┘  └──────┘  └──┘  └────────┘  └─────┘     │
 *   │                                                    │
 *   │   规则：一个包裹必须完整地放在货架格里，             │
 *   │         不能把半个包裹塞一个格子、半个塞另一个       │
 *   │         （这个规则来自 W25Q64 的实际限制，           │
 *   │           因为数据写入是按页对齐的）                 │
 *   │                                                    │
 *   └────────────────────────────────────────────────────┘
 *
 *   但实际上 W25Q64 的"真正约束"是：
 *   "一页内部可以跨地址连续写入任意数据（超出一页时自动回卷）"
 *   所以我们其实可以在物理层面把数据 "铺开" 写，不用担心跨页。
 *
 *   真正需要"货物分装"的原因是 ———— 你需要在页的粒度上管理：
 *   ① 哪些数据已经写过了，哪些页是空闲的
 *   ② 写入前必须擦除整个 Sector，所以你需要把"逻辑上一起的数据"
 *      放在同一个 Sector 里，避免擦除时影响其他数据
 *   ③ 数据的"包头"需要记录数据长度，方便读取时知道读多少
 *
 * ============================================================================
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │         第三章：空间计算 —— 一个包裹需要几个货架格？            │
 * └─────────────────────────────────────────────────────────────────┘
 *
 *   ★ 核心公式：
 *
 *       需要的页数 = (数据长度 + 页大小 - 1) / 页大小
 *
 *   这个就是经典的"向上取整"除法：
 *
 *       ceil(data_size / 256) = (data_size + 255) / 256
 *
 *   举例：
 *     100 字节 → (100+255)/256 = 1 页  （只占 1 页，浪费 156 字节）
 *     256 字节 → (256+255)/256 = 1 页  （刚好 1 页）
 *     257 字节 → (257+255)/256 = 2 页  （占 2 页，浪费 255 字节）
 *     500 字节 → (500+255)/256 = 2 页
 *    4096 字节 → (4096+255)/256 = 16 页 = 1 个 Sector
 *
 *   更进一步——需要的扇区数：
 *
 *       需要的扇区数 = (数据长度 + 4095) / 4096
 *
 *   这对于决定"要不要擦除一个新扇区"非常有用！
 *
 * ============================================================================
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │         第四章：数据结构设计 —— 仓库的"登记册"                   │
 * └─────────────────────────────────────────────────────────────────┘
 *
 *   我们需要一个"仓库登记册"来记录：
 *   ① 每个"包裹"存在哪个位置（起始页码 / 起始地址）
 *   ② 每个"包裹"有多大
 *   ③ 仓库哪些位置已经用掉了
 *
 *   下面设计两种方案：
 *
 *   【方案 A：简单目录表】（适合少量数据，比如字库文件）
 *   ┌────────────────────────────────────────────────────┐
 *   │  目录项 0: id=0, 起始页=0,   大小=xxx (字库)       │
 *   │  目录项 1: id=1, 起始页=500, 大小=yyy (配置)       │
 *   │  目录项 2: id=2, 起始页=600, 大小=zzz (日志)       │
 *   │  ...                                               │
 *   │  空闲标记: 下一个可用的页号                          │
 *   └────────────────────────────────────────────────────┘
 *
 *   【方案 B：位图法】（适合频繁增删的小数据块）
 *   用一个 bit 表示一个 Sector 是否被占用：
 *      W25Q64 有 2048 个 Sector → 需要 2048 bits = 256 字节
 *      一个 uint8_t map[256] 就够了！
 *
 * ============================================================================
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │         第五章：完整代码实现                                      │
 * └─────────────────────────────────────────────────────────────────┘
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * 第一部分：W25Q64 硬件参数（这些是你已有的）
 * ========================================================================= */
#define W25Q64_PAGE_SIZE        256     /* 一页 256 字节 */
#define W25Q64_SECTOR_SIZE      4096    /* 一个扇区 4096 字节 (4KB) */
#define W25Q64_BLOCK_SIZE       65536   /* 一个块 65536 字节 (64KB) */
#define W25Q64_PAGES_PER_SECTOR 16      /* 一个扇区 = 16 页 */
#define W25Q64_TOTAL_PAGES      32768   /* 总共 32768 页 */
#define W25Q64_TOTAL_SECTORS    2048    /* 总共 2048 扇区 */
#define W25Q64_TOTAL_SIZE       8388608 /* 总共 8MB */

/* =========================================================================
 * 第二部分：核心宏定义 —— 空间计算的"万能公式"
 * ========================================================================= */

/**
 * ★★★★★ 核心公式 ★★★★★
 *
 * CEIL_DIV(a, b) = 向上取整的除法
 *
 *   传统写法：(a + b - 1) / b
 *
 *   作用：计算"需要几个 b 才能装下 a"
 *
 *   举例：
 *     CEIL_DIV(100,  256) = 1   → 100 字节需要 1 页
 *     CEIL_DIV(256,  256) = 1   → 256 字节需要 1 页
 *     CEIL_DIV(257,  256) = 2   → 257 字节需要 2 页
 *     CEIL_DIV(4096, 4096) = 1  → 4096 字节需要 1 扇区
 *     CEIL_DIV(5000, 4096) = 2  → 5000 字节需要 2 扇区
 */
#define CEIL_DIV(a, b)      (((a) + (b) - 1) / (b))

/* 计算数据需要多少页 */
#define PAGES_NEEDED(size)  CEIL_DIV((size), W25Q64_PAGE_SIZE)

/* 计算数据需要多少扇区 */
#define SECTORS_NEEDED(size) CEIL_DIV((size), W25Q64_SECTOR_SIZE)

/* 地址 → 页码 / 扇区号 / 块号 */
#define ADDR_TO_PAGE(addr)      ((addr) / W25Q64_PAGE_SIZE)
#define ADDR_TO_SECTOR(addr)    ((addr) / W25Q64_SECTOR_SIZE)
#define ADDR_TO_BLOCK(addr)     ((addr) / W25Q64_BLOCK_SIZE)

/* 页码 / 扇区号 → 地址 */
#define PAGE_TO_ADDR(page)      ((uint32_t)(page) * W25Q64_PAGE_SIZE)
#define SECTOR_TO_ADDR(sector)  ((uint32_t)(sector) * W25Q64_SECTOR_SIZE)

/* 判断地址是否在某页的起始位置（页对齐） */
#define IS_PAGE_ALIGNED(addr)   (((addr) % W25Q64_PAGE_SIZE) == 0)

/* 计算一个扇区内，从当前页到扇区末尾还剩多少页 */
#define PAGES_REMAIN_IN_SECTOR(current_page) \
    (W25Q64_PAGES_PER_SECTOR - ((current_page) % W25Q64_PAGES_PER_SECTOR))

/* 计算一个扇区内，从当前地址到扇区末尾还剩多少字节 */
#define BYTES_REMAIN_IN_SECTOR(addr) \
    (W25Q64_SECTOR_SIZE - ((addr) % W25Q64_SECTOR_SIZE))

/* =========================================================================
 * 第三部分：数据结构 —— 仓库登记册
 * ========================================================================= */

/* ---- 方案 A：目录表（简单、适合少量大文件） ---- */

/* 目录项：描述 Flash 中存储的一个"包裹" */
typedef struct {
    uint16_t id;            /* 数据编号（你代码里的"字库 id"） */
    uint32_t start_page;    /* 起始页码 */
    uint32_t size;          /* 数据大小（字节） */
    uint32_t crc32;         /* CRC32 校验值，确保数据完整 */
    uint8_t  valid;         /* 0xFF = 无效, 0x00 = 有效 */
} __attribute__((packed)) FileDirEntry_t;

/* 目录表：N 条目录项 + 系统信息 */
#define MAX_DIR_ENTRIES     32  /* 最多存 32 个"包裹" */

typedef struct {
    FileDirEntry_t entries[MAX_DIR_ENTRIES]; /* 目录项数组 */
    uint32_t free_start_page;                /* 空闲区的起始页码 */
    uint32_t magic;                          /* 魔数 0xAA55AA55，用于校验目录完整性 */
} __attribute__((packed)) FileDirTable_t;

/* 默认存放位置：W25Q64 的最后一个 Sector，0x7FE000 */
#define DIR_TABLE_ADDR      0x7FE000


/* ---- 方案 B：位图法（适合频繁增删的小数据块） ---- */

/* 用一个 bit 标记一个 Sector 是否被占用 */
/* W25Q64 共 2048 个 Sector → 需要 2048 bit = 256 字节 */
#define BITMAP_SIZE         (W25Q64_TOTAL_SECTORS / 8)

typedef struct {
    uint8_t map[BITMAP_SIZE];   /* 位图：1 = 已占用, 0 = 空闲 */
    uint32_t used_sectors;      /* 快速统计：已用扇区数 */
    uint32_t total_sectors;     /* 总扇区数 = 2048 */
} __attribute__((packed)) SectorBitmap_t;


/* =========================================================================
 * 第四部分：核心算法 —— "货物分装"的具体实现
 * ========================================================================= */

/**
 * ========================================================================
 * 函数：W25Q64_CalcWritePlan
 *
 * ★ 这是整个算法的核心！★
 *
 * 功能：给定一段数据，计算它应该放在 Flash 的哪个位置，
 *       以及需要分几批写入。
 *
 * ========================================================================
 *
 * 【通俗类比：快递员怎么安排包裹？】
 *
 *   你不是一次性把一卡车包裹全倒进仓库，而是一箱一箱搬进去。
 *   W25Q64 也一样：你手里的"一箱"最大就是 256 字节（1 页）。
 *   所以你要把一个大包裹拆成若干个 ≤256 字节的小箱，逐个搬进去。
 *
 * ========================================================================
 *
 * 参数说明：
 *   data_size        = 你要存储的数据总大小（字节）
 *   start_page       = 你想从哪个物理页开始放（0~32767）
 *   write_plan       = 输出：一个数组，每个元素描述一批写入操作
 *   max_plans        = write_plan 数组的最大容量
 *
 * 返回值：
 *   返回实际的"批次"数量（≤ max_plans）
 *
 * ========================================================================
 *
 * 示例：把 700 字节的数据从第 0 页开始写入
 *
 *   页大小 = 256 字节
 *
 *   700 字节 / 256 = 2 页余 188 字节 → 总共需要 3 页
 *
 *   输出 write_plan[] 数组：
 *
 *     ┌─────批次0─────┐  ┌─────批次1─────┐  ┌─────批次2─────┐
 *     │ 起始页: 0     │  │ 起始页: 1     │  │ 起始页: 2     │
 *     │ 写入256字节   │  │ 写入256字节   │  │ 写入188字节   │
 *     │ 地址:0x000000 │  │ 地址:0x000100 │  │ 地址:0x000200 │
 *     │ ← 数据[0:255] │  │ ← 数据[256:511]│  │ ← 数据[512:699]│
 *     └───────────────┘  └───────────────┘  └───────────────┘
 *
 *   然后你只需要写一个 for 循环：
 *
 *     for (int i = 0; i < num_plans; i++) {

 *         W25Q64_PageProgram(plan[i].addr, &data[plan[i].offset],
 *                            plan[i].len);
 *     }
 *
 *   搞定！
 *
 * ======================================================================== */

/* 单批写入计划 */
typedef struct {
    uint32_t addr;          /* Flash 物理地址（例如 0x000100） */
    uint32_t offset;        /* 在原始数据缓冲区中的偏移（例如 256） */
    uint16_t len;           /* 这一批写多少字节（≤ 256） */
    uint16_t page;          /* 所在页码（调试用） */
} WritePlan_t;

int W25Q64_CalcWritePlan(uint32_t data_size,
                          uint32_t start_page,
                          WritePlan_t *write_plan,
                          int max_plans)
{
    int plan_idx = 0;               /* 当前计划的批次号 */
    uint32_t data_offset = 0;       /* 已经处理了多少字节数据 */
    uint32_t remaining = data_size; /* 还剩多少字节没写入 */
    uint32_t current_page = start_page;

    while (remaining > 0 && plan_idx < max_plans) {

        /* ──────────────────────────────────────────────
         * 第 1 步：计算这批能写多少字节
         *
         * 两种情况：
         *   ① 剩余数据 ≤ 256 → 这是最后一批，写 remaining 字节
         *   ② 剩余数据 > 256  → 写满一页，即 256 字节
         *
         * ────────────────────────────────────────────── */
        uint16_t batch_len = (remaining > W25Q64_PAGE_SIZE)
                             ? W25Q64_PAGE_SIZE
                             : (uint16_t)remaining;

        /* ──────────────────────────────────────────────
         * 第 2 步：填充这一批的写入计划
         * ────────────────────────────────────────────── */
        write_plan[plan_idx].addr   = PAGE_TO_ADDR(current_page);
        write_plan[plan_idx].offset = data_offset;
        write_plan[plan_idx].len    = batch_len;
        write_plan[plan_idx].page   = current_page;

        /* ──────────────────────────────────────────────
         * 第 3 步：更新状态，准备下一批
         * ────────────────────────────────────────────── */
        data_offset  += batch_len;
        remaining    -= batch_len;
        current_page += 1;
        plan_idx     += 1;
    }

    /* 返回总共需要写几批 */
    return plan_idx;
}


/**
 * ========================================================================
 * 函数：W25Q64_StreamWrite（流式写入 —— 你最终调用的"一键写入"函数）
 *
 * 这个函数模拟你的 W25Q64_StreamFeed 的逻辑，但更清晰地展示了
 * "空间计算 + 货物分装"的整个过程。
 *
 * ========================================================================
 *
 * 【执行流程图】
 *
 *   用户调用：StreamWrite(FONT_BIN, 2527232, 0)
 *                    │
 *                    ▼
 *   ┌────────────────────────────────┐
 *   │ 第 1 步：空间计算              │
 *   │   pages_needed = (2527232+255)/256 = 9872 页 │
 *   │   sectors_needed = (2527232+4095)/4096 = 618 扇区 │
 *   │                                │
 *   │   ✓ 空间够吗？(9872 ≤ 32768)   │
 *   │   ✓ 需要先擦除 618 个扇区吗？  │
 *   │                                │
 *   ├────────────────────────────────┤
 *   │ 第 2 步：逐批写入              │
 *   │   for (还剩数据) {             │
 *   │     计算这批写多少（≤256字节） │
 *   │     调用 PageProgram(addr, buf, len) │
 *   │     addr += 256                │
 *   │     buf  += 256                │
 *   │   }                            │
 *   │                                │
 *   ├────────────────────────────────┤
 *   │ 第 3 步：登记目录              │
 *   │   dir[0] = {id=0, start=0,     │
 *   │             size=2527232}      │
 *   │   把目录表写入 Flash 末尾      │
 *   └────────────────────────────────┘
 *
 * ======================================================================== */

int W25Q64_StreamWrite(const uint8_t *data,
                        uint32_t data_size,
                        uint32_t start_page,
                        WritePlan_t *plan_buf,
                        int plan_buf_capacity)
{
    /* ──────────────────────────────────────────────
     * 【空间计算】第 1 步：算账 —— 这笔货要占多大地方？
     * ────────────────────────────────────────────── */
    uint32_t pages_needed   = PAGES_NEEDED(data_size);
    uint32_t sectors_needed = SECTORS_NEEDED(data_size);

    printf("[空间计算] 数据大小: %lu 字节\r\n", data_size);
    printf("  → 需要 %lu 页 (= %lu KB)\r\n",
           pages_needed,
           (pages_needed * W25Q64_PAGE_SIZE) / 1024);

    /* 检查是否超出 Flash 容量 */
    if (start_page + pages_needed > W25Q64_TOTAL_PAGES) {
        printf("[错误] 空间不足！需要 %lu 页，但只剩 %lu 页\r\n",
               pages_needed,
               W25Q64_TOTAL_PAGES - start_page);
        return -1;
    }

    /* ──────────────────────────────────────────────
     * 第 2 步：必须先擦除目标区域
     *
     * W25Q64 只能把 bit 从 1 写成 0，不能反过来。
     * 所以写之前必须把整个扇区擦除（全部变成 0xFF）。
     *
     * 这里我们擦除所有需要的扇区。
     *
     * 【关键理解】为什么要按扇区擦除？
     *   因为 W25Q64 的最小擦除单位就是 Sector (4KB)，
     *   你不能只擦一页。如果只擦一页，会连带擦掉
     *   同扇区的其他 15 页！
     *
     *   所以设计存储结构时，尽量让"逻辑上一起的数据"
     *   落在同一个扇区里。
     * ────────────────────────────────────────────── */
    uint32_t start_sector = ADDR_TO_SECTOR(PAGE_TO_ADDR(start_page));
    uint32_t end_sector   = ADDR_TO_SECTOR(
        PAGE_TO_ADDR(start_page + pages_needed - 1));

    printf("[擦除] 擦除扇区 %lu ~ %lu (%lu 个扇区)...\r\n",
           start_sector, end_sector,
           end_sector - start_sector + 1);

    for (uint32_t sec = start_sector; sec <= end_sector; sec++) {
        /* W25Q64_SectorErase(SECTOR_TO_ADDR(sec)); */   /* 实际调用你的函数 */
        /* WaitForBusy(); */                             /* 等待擦除完成 */
        printf("  擦除扇区 %lu (地址 0x%06lX) OK\r\n",
               sec, SECTOR_TO_ADDR(sec));
    }

    /* ──────────────────────────────────────────────
     * 【货物分装】第 3 步：制定写入计划
     *
     * 把大块数据拆成若干个 ≤256 字节的小批次
     * ────────────────────────────────────────────── */
    int num_plans = W25Q64_CalcWritePlan(data_size, start_page,
                                          plan_buf, plan_buf_capacity);

    printf("[分装计划] 共计 %d 批写入:\r\n", num_plans);

    /* ──────────────────────────────────────────────
     * 第 4 步：逐批执行写入
     * ────────────────────────────────────────────── */
    for (int i = 0; i < num_plans; i++) {
        WritePlan_t *p = &plan_buf[i];

        printf("  第 %3d 批: 页 %5lu, 地址 0x%06lX, "
               "写入 %3u 字节 (数据偏移 %5lu)\r\n",
               i, p->page, p->addr, p->len, p->offset);

        /* ----- 实际写入操作 ----- */
        /* W25Q64_PageProgram(p->addr, &data[p->offset], p->len); */
        /* WaitForBusy(); */

        /* ----- 验证写入（可选但推荐！） ----- */
        /* 读取刚写入的数据，和原始数据比对 */
    }

    printf("[完成] 共写入 %lu 字节到 %lu 页\r\n", data_size, pages_needed);

    return 0;
}


/**
 * ========================================================================
 * 函数：W25Q64_AppendData（追加写入 —— 支持不定量多次写入）
 *
 * 场景：你每次收到的数据量不固定，可能是 100 字节，也可能是 10KB。
 *       你要把它们逐个追加到 Flash 中，每次写完后更新"空闲指针"。
 *
 * ========================================================================
 *
 * 【通俗类比：流水线上的打包工】
 *
 *   传送带上源源不断地来包裹，大小不一。
 *   你面前是一排货架格（页），你每次都把包裹放进当前格子里。
 *   当前格子装不下的时候，就换下一个格子。
 *
 *   你需要记录两个指针：
 *     g_current_page  → 当前写到哪一页了（"空闲指针"）
 *     g_offset_in_page → 当前页里已经用了多少字节
 *
 * ========================================================================
 *
 * 假设 g_current_page = 0, g_offset_in_page = 0：
 *
 *   第 1 次写入 100 字节:
 *     → 写入页 0，偏移 0~99
 *     → g_offset_in_page = 100（这页还剩 156 字节）
 *
 *   第 2 次写入 200 字节:
 *     → 100 + 200 = 300 > 256，装不下！
 *     → 先把当前页填满：写 156 字节（地址 0x000064，数据 0~155）
 *     → 换下一页：g_current_page = 1，g_offset_in_page = 0
 *     → 再写剩余 44 字节（地址 0x000100，数据 156~199）
 *     → g_offset_in_page = 44
 *
 *   第 3 次写入 50 字节:
 *     → 44 + 50 = 94 ≤ 256，装得下
 *     → 写 50 字节（地址 0x00012C，数据 0~49）
 *     → g_offset_in_page = 94
 *
 *   这样就实现了连续的"不定量数据追加写入"！
 *
 * ======================================================================== */

/* 全局状态：记录"流水线"当前写到哪了 */
static uint32_t g_current_page     = 0;   /* 当前写到第几页 */
static uint16_t g_offset_in_page   = 0;   /* 当前页内偏移（0~255） */
static uint32_t g_total_written    = 0;   /* 总共写入了多少字节（统计用） */

int W25Q64_AppendData(const uint8_t *data, uint32_t data_len,
                       WritePlan_t *plan_buf, int plan_buf_capacity)
{
    uint32_t src_offset = 0;        /* 数据源的偏移 */
    uint32_t remaining  = data_len; /* 还剩多少字节没写入 */

    int plan_idx = 0;

    printf("[追加写入] 接收 %lu 字节，当前处于 页%lu 偏移%u\r\n",
           data_len, g_current_page, g_offset_in_page);

    while (remaining > 0) {

        /* ──────────────────────────────────────────
         * 第 1 步：计算当前页还剩多少空间
         *
         *   g_offset_in_page  = 已经用了多少（0~255）
         *   剩余空间 = 256 - g_offset_in_page
         *
         *   如果 g_offset_in_page = 100 → 剩余 156 字节
         *   如果 g_offset_in_page = 0   → 剩余 256 字节（全新页）
         *   如果 g_offset_in_page = 256 → 剩余 0（该换页了！）
         * ────────────────────────────────────────── */
        uint16_t space_left = W25Q64_PAGE_SIZE - g_offset_in_page;

        /* 如果当前页满了，自动翻到下一页 */
        if (space_left == 0) {
            g_current_page++;
            g_offset_in_page = 0;
            space_left = W25Q64_PAGE_SIZE;

            printf("  → 翻页到 页 %lu\r\n", g_current_page);
        }

        /* ──────────────────────────────────────────
         * 第 2 步：决定这一批写入多少字节
         *
         *   取 min(剩余数据, 当前页剩余空间)
         *
         *   举例：
         *     remaining = 500, space_left = 256 → 写 256
         *     remaining = 100, space_left = 156 → 写 100
         *     remaining = 200, space_left = 50  → 写 50（需翻页）
         * ────────────────────────────────────────── */
        uint16_t batch_len = (remaining < space_left)
                             ? (uint16_t)remaining
                             : space_left;

        /* ──────────────────────────────────────────
         * 第 3 步：记录这批写入的计划
         * ────────────────────────────────────────── */
        if (plan_buf != NULL && plan_idx < plan_buf_capacity) {
            plan_buf[plan_idx].addr   = PAGE_TO_ADDR(g_current_page)
                                        + g_offset_in_page;
            plan_buf[plan_idx].offset = src_offset;
            plan_buf[plan_idx].len    = batch_len;
            plan_buf[plan_idx].page   = g_current_page;
        }

        /* ──────────────────────────────────────────
         * 第 4 步：执行实际写入
         *
         * W25Q64 支持页内任意地址写入（不需要从页首开始），
         * 但一次写入不能跨页。
         * ────────────────────────────────────────── */
        /* W25Q64_PageProgram(PAGE_TO_ADDR(g_current_page) + g_offset_in_page,
                              &data[src_offset],
                              batch_len);  */
        /* WaitForBusy(); */

        printf("  写 %3u 字节 → 页%lu 偏移%u (地址 0x%06lX)\r\n",
               batch_len, g_current_page, g_offset_in_page,
               PAGE_TO_ADDR(g_current_page) + g_offset_in_page);

        /* ──────────────────────────────────────────
         * 第 5 步：更新状态指针
         * ────────────────────────────────────────── */
        g_offset_in_page += batch_len;
        src_offset       += batch_len;
        remaining        -= batch_len;
        g_total_written  += batch_len;
        plan_idx++;
    }

    printf("[完成] 本次写入 %lu 字节，累计写入 %lu 字节\r\n",
           data_len, g_total_written);
    printf("  当前指针: 页 %lu, 偏移 %u\r\n",
           g_current_page, g_offset_in_page);

    return plan_idx;
}


/* =========================================================================
 * 第五部分：扇区边界感知的"智能分装"（进阶版）
 * =========================================================================
 *
 *  上面的算法只管"页"不管"扇区"。
 *  但在实际应用中，你可能希望"一个逻辑数据块不要跨越扇区边界"。
 *
 *  原因：
 *    如果一个大文件跨了两个扇区，将来你要删除它的时候，
 *    需要擦除两个扇区，而第二个扇区可能还存了别的文件的数据！
 *    → 这就是为什么最好让每个"包裹"从扇区开头开始放。
 *
 *  下面是一个示例函数，展示如何实现"扇区对齐的智能分装"。
 */

/**
 * ========================================================================
 * 函数：W25Q64_CalcWritePlan_SectorAware
 *
 * 与基础版 W25Q64_CalcWritePlan 的区别：
 *   - 基础版只管页，不管扇区，数据会连续地铺满所有页
 *   - 进阶版保证：每个"数据块"从扇区边界开始
 *     （如果当前扇区写不下了，跳到下一个扇区的开头）
 *
 * ========================================================================
 *
 * 【通俗类比：文件柜的抽屉】
 *
 *   基础版 = 一本书紧挨着一本书放在书架上
 *   进阶版 = 每个文件放在一个单独的文件夹里，每个文件夹占一个抽屉
 *
 *   进阶版的"浪费"更多（抽屉里可能有空余），
 *   但好处是：删掉一个文件只需扔掉一个抽屉，不影响其他文件。
 *
 * ======================================================================== */

int W25Q64_CalcWritePlan_SectorAware(uint32_t data_size,
                                      uint32_t start_page,
                                      WritePlan_t *write_plan,
                                      int max_plans)
{
    int plan_idx = 0;
    uint32_t data_offset = 0;
    uint32_t remaining = data_size;
    uint32_t current_page = start_page;

    while (remaining > 0 && plan_idx < max_plans) {

        /* ──────────────────────────────────────────
         * 额外检查：当前页是否在扇区边界上？
         *
         * 如果不是（比如前一文件写了 200 页，
         * 第 200 页是某个扇区的第 8 页），
         * 我们就跳到下一个扇区的开头。
         *
         * 这个"浪费策略"让每个文件都从扇区开头开始。
         * ────────────────────────────────────────── */
        uint32_t cur_sector_start_page =
            (current_page / W25Q64_PAGES_PER_SECTOR) * W25Q64_PAGES_PER_SECTOR;

        if (current_page != cur_sector_start_page) {
            /* 不在扇区开头 → 跳到下一个扇区 */
            printf("  [扇区对齐] 页 %lu 不是扇区开头，跳到页 %lu\r\n",
                   current_page,
                   cur_sector_start_page + W25Q64_PAGES_PER_SECTOR);

            current_page = cur_sector_start_page + W25Q64_PAGES_PER_SECTOR;
        }

        /* ──────────────────────────────────────────
         * 和基础版一样的逐页写入逻辑
         * ────────────────────────────────────────── */
        uint16_t batch_len = (remaining > W25Q64_PAGE_SIZE)
                             ? W25Q64_PAGE_SIZE
                             : (uint16_t)remaining;

        write_plan[plan_idx].addr   = PAGE_TO_ADDR(current_page);
        write_plan[plan_idx].offset = data_offset;
        write_plan[plan_idx].len    = batch_len;
        write_plan[plan_idx].page   = current_page;

        data_offset  += batch_len;
        remaining    -= batch_len;
        current_page += 1;
        plan_idx     += 1;
    }

    return plan_idx;
}


/* =========================================================================
 * 第六部分：仓库空间管理 —— 扇区位图
 * =========================================================================
 *
 *  写完数据后，你需要知道哪些扇区已经被用了，
 *  这样下次写数据才能找到空闲位置。
 *
 *  位图：用 1 个 bit 表示 1 个扇区是否被占用
 *    W25Q64 有 2048 个扇区 → 2048 bit = 256 字节
 */

/**
 * 在位图中标记一段扇区为"已占用"
 */
void Bitmap_MarkUsed(SectorBitmap_t *bitmap, uint32_t start_sector, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        uint32_t sector = start_sector + i;
        uint32_t byte_idx = sector / 8;
        uint32_t bit_idx  = sector % 8;

        if (byte_idx < BITMAP_SIZE) {
            bitmap->map[byte_idx] |= (1 << bit_idx);
        }
    }
    bitmap->used_sectors += count;
}

/**
 * 在位图中标记一段扇区为"空闲"
 */
void Bitmap_MarkFree(SectorBitmap_t *bitmap, uint32_t start_sector, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        uint32_t sector = start_sector + i;
        uint32_t byte_idx = sector / 8;
        uint32_t bit_idx  = sector % 8;

        if (byte_idx < BITMAP_SIZE) {
            bitmap->map[byte_idx] &= ~(1 << bit_idx);
        }
    }
    if (bitmap->used_sectors >= count) {
        bitmap->used_sectors -= count;
    }
}

/**
 * 检查某个扇区是否被占用
 */
int Bitmap_IsUsed(SectorBitmap_t *bitmap, uint32_t sector)
{
    uint32_t byte_idx = sector / 8;
    uint32_t bit_idx  = sector % 8;

    if (byte_idx >= BITMAP_SIZE) return 1; /* 越界就当占用了 */

    return (bitmap->map[byte_idx] & (1 << bit_idx)) ? 1 : 0;
}

/**
 * 查找连续空闲扇区 —— 这是你分配空间时的核心函数
 *
 * 参数：
 *   bitmap          = 位图
 *   sectors_needed  = 需要多少个连续的扇区
 *   start_sector    = 从哪个扇区开始搜索
 *   found_sector    = 输出：找到的起始扇区号
 *
 * 返回值：0 = 找到, -1 = 没找到
 *
 * 例如：你需要 10 个连续的空闲扇区，
 *       这个函数从 start_sector 开始往后找 10 个连续的 0。
 */
int Bitmap_FindFreeConsecutive(SectorBitmap_t *bitmap,
                                uint32_t sectors_needed,
                                uint32_t start_sector,
                                uint32_t *found_sector)
{
    uint32_t consecutive_free = 0;
    uint32_t candidate_start = 0;

    for (uint32_t sec = start_sector; sec < W25Q64_TOTAL_SECTORS; sec++) {
        if (!Bitmap_IsUsed(bitmap, sec)) {
            /* 这个扇区是空闲的 */
            if (consecutive_free == 0) {
                candidate_start = sec;              /* 记录连续空闲区的起点 */
            }
            consecutive_free++;

            if (consecutive_free >= sectors_needed) {
                *found_sector = candidate_start;     /* 找到了！ */
                return 0;
            }
        } else {
            consecutive_free = 0;                    /* 中断了，重新计数 */
        }
    }

    /* 找不到足够大的连续空闲空间 */
    return -1;
}

/**
 * 打印位图统计信息
 */
void Bitmap_PrintStats(SectorBitmap_t *bitmap)
{
    uint32_t free_sectors = W25Q64_TOTAL_SECTORS - bitmap->used_sectors;
    uint32_t free_kb      = free_sectors * W25Q64_SECTOR_SIZE / 1024;
    uint32_t used_kb      = bitmap->used_sectors * W25Q64_SECTOR_SIZE / 1024;

    printf("[Flash 空间统计]\r\n");
    printf("  总容量: 8192 KB (8 MB)\r\n");
    printf("  已用:   %lu KB (%lu 个扇区)\r\n", used_kb, bitmap->used_sectors);
    printf("  空闲:   %lu KB (%lu 个扇区)\r\n", free_kb, free_sectors);
    printf("  使用率: %lu%%\r\n",
           (bitmap->used_sectors * 100) / W25Q64_TOTAL_SECTORS);
}


/* =========================================================================
 * 第七部分：模拟演示 —— main 函数（你可以复制到你的板子上运行）
 * =========================================================================
 *
 *  下面的 main() 函数演示了整个流程：
 *   ① 定义一段不定长的数据
 *   ② 计算它需要多少页/扇区
 *   ③ 制定写入计划
 *   ④ 模拟执行写入
 *   ⑤ 更新位图
 *   ⑥ 打印统计信息
 */

#ifdef __RUN_DEMO__
int main(void)
{
    /* ---- 初始化仓库登记册 ---- */
    SectorBitmap_t bitmap = {0};
    bitmap.total_sectors = W25Q64_TOTAL_SECTORS;

    /* ---- 模拟数据：一段 700 字节的数据 ---- */
    uint8_t test_data[700];
    for (int i = 0; i < 700; i++) {
        test_data[i] = (uint8_t)(i & 0xFF);   /* 填充 0,1,2,...,255,0,1,... */
    }

    /* ---- 写入计划缓冲区 ---- */
    WritePlan_t plans[128];  /* 最多 128 批 */
    int num_plans;

    /* ==================================================================
     * 场景 1：基础写入 —— 连续铺满
     * ================================================================== */
    printf("\n========== 场景 1：基础写入（连续铺满） ==========\r\n");

    num_plans = W25Q64_CalcWritePlan(700,     /* 数据大小 */
                                      0,       /* 起始页 */
                                      plans,   /* 输出计划 */
                                      128);    /* 缓冲区大小 */

    for (int i = 0; i < num_plans; i++) {
        printf("  计划 %d: 页=%lu, 地址=0x%06lX, "
               "偏移=%lu, 长度=%u\r\n",
               i, plans[i].page, plans[i].addr,
               plans[i].offset, plans[i].len);
    }

    /* ==================================================================
     * 场景 2：多次不定量追加写入
     * ================================================================== */
    printf("\n========== 场景 2：多次不定量追加写入 ==========\r\n");

    /* 重置全局状态 */
    g_current_page   = 0;
    g_offset_in_page = 0;
    g_total_written  = 0;

    /* 第 1 次：写 100 字节 */
    W25Q64_AppendData(test_data, 100, plans, 128);

    /* 第 2 次：写 200 字节 */
    W25Q64_AppendData(test_data, 200, plans, 128);

    /* 第 3 次：写 50 字节 */
    W25Q64_AppendData(test_data, 50, plans, 128);

    /* 第 4 次：写 500 字节（会跨页！） */
    W25Q64_AppendData(test_data, 500, plans, 128);

    /* ==================================================================
     * 场景 3：扇区感知写入 + 位图管理
     * ================================================================== */
    printf("\n========== 场景 3：扇区感知写入 + 位图 ==========\r\n");

    /* 假设：字库 3MB 需要 3*1024*1024/4096 = 768 个扇区 */
    uint32_t font_size = 3 * 1024 * 1024;  /* 3MB */
    uint32_t sectors_needed = SECTORS_NEEDED(font_size);
    uint32_t found_sector = 0;

    printf("字库大小: %lu 字节 → 需要 %lu 个扇区\r\n",
           font_size, sectors_needed);

    if (Bitmap_FindFreeConsecutive(&bitmap, sectors_needed,
                                    0, &found_sector) == 0) {
        printf("→ 找到空闲空间，从扇区 %lu 开始\r\n", found_sector);
        printf("→ 物理地址: 0x%06lX\r\n", SECTOR_TO_ADDR(found_sector));

        /* 标记这些扇区为已用 */
        Bitmap_MarkUsed(&bitmap, found_sector, sectors_needed);

        /* 扇区感知写入计划 */
        num_plans = W25Q64_CalcWritePlan_SectorAware(
                        font_size, ADDR_TO_PAGE(SECTOR_TO_ADDR(found_sector)),
                        plans, 128);

        printf("→ 共分 %d 批写入\r\n", num_plans);
    }

    Bitmap_PrintStats(&bitmap);

    printf("\r\n========== 演示完毕 ==========\r\n");
    return 0;
}
#endif /* __RUN_DEMO__ */


/*
 * =========================================================================
 * 第八部分：关键公式速查表
 * =========================================================================
 *
 * ┌──────────────────────────────────────────────────────────────────┐
 * │                    空间计算万能公式速查                          │
 * ├──────────────────────────────────────────────────────────────────┤
 * │                                                                  │
 * │  1. 字节 → 页数:   pages = (size + 255) / 256                   │
 * │                                                                  │
 * │  2. 字节 → 扇区数:  sectors = (size + 4095) / 4096              │
 * │                                                                  │
 * │  3. 页码 → 地址:   addr = page * 256                             │
 * │                                                                  │
 * │  4. 地址 → 页码:   page = addr / 256                             │
 * │                                                                  │
 * │  5. 地址 → 扇区号: sector = addr / 4096                          │
 * │                                                                  │
 * │  6. 当前页剩余空间: space_left = 256 - (addr % 256)            │
 * │                                                                  │
 * │  7. 扇区内剩余空间: remain = 4096 - (addr % 4096)              │
 * │                                                                  │
 * │  8. 地址是否页对齐: (addr % 256) == 0                           │
 * │                                                                  │
 * │  9. 地址是否扇区对齐: (addr % 4096) == 0                        │
 * │                                                                  │
 * │ 10. 上一扇区起始地址: ((addr / 4096) - 1) * 4096               │
 * │     下一扇区起始地址: ((addr / 4096) + 1) * 4096               │
 * │     当前扇区起始地址: (addr / 4096) * 4096                      │
 * │                                                                  │
 * └──────────────────────────────────────────────────────────────────┘
 *
 * =========================================================================
 * 第九部分：常见问题 FAQ
 * =========================================================================
 *
 * Q1: 为什么不能跨页写入？
 * A1: W25Q64 的 Page Program 命令（0x02）的地址计数器在 256 字节边界
 *     会回卷到当前页的开头。比如地址 0x0000FF 写 2 字节，第 2 字节
 *     会回到 0x000000 而不是 0x000100。这是硬件限制。
 *
 * Q2: 为什么写入前要擦除？
 * A2: Flash 只能把 bit 从 1 写成 0，不能从 0 写成 1。
 *     擦除 = 把所有 bit 恢复成 1（0xFF）。
 *     所以"先擦后写"是必须的铁律。
 *
 * Q3: 为什么要按扇区擦除，不能按页擦除？
 * A3: W25Q64 的最小擦除单位是 Sector (4KB)。
 *     不存在"页擦除"这个硬件命令。
 *
 * Q4: 为什么有时浪费空间也要让数据从扇区开头开始？
 * A4: 因为删除数据需要擦除整个扇区。
 *     如果你的数据跨了两个扇区，其中一个扇区可能还存着别的数据，
 *     你就没法单独删掉这个数据了！
 *
 * Q5: 位图存在哪里？
 * A5: 可以存在 W25Q64 的最后一个 Sector（地址 0x7FE000）。
 *     或者存在 STM32 的内部 Flash 里（如果空间够的话）。
 *     或者存在 EEPROM 里。
 *
 * =========================================================================
 *
 * 【总结：三步学会这个算法】
 *
 *   第 1 步：理解 W25Q64 的三个层次 —— 页(256B) / 扇区(4KB) / 块(64KB)
 *
 *   第 2 步：记住核心公式 —— pages_needed = (data_size + 255) / 256
 *           这个公式告诉你"需要几个货架格"
 *
 *   第 3 步：写一个 while 循环 ——
 *           while (有剩余数据) {
 *               计算这批写多少（min(剩余, 256)）
 *               调用 PageProgram
 *               更新指针
 *           }
 *
 *   如果你能自己手写出上面这个 while 循环，你就已经完全学会了！
 *
 * =========================================================================
 */
