/**
 * ============================================================================
 * W25Q64 Flash 驱动 —— 不定量数据写入管理系统
 * ============================================================================
 *
 * 【核心场景】
 *   向 Flash 中写入"大小不固定"的数据块（比如不同字号的字库：16x16 字体
 *   每个字32字节、32x32每个字128字节、图片大小也不同），并能在需要时正确读出。
 *
 * 【设计思路 —— "仓库管理"类比】
 *   把 W25Q64 的 8MB 空间想象成一个仓库：
 *
 *   ┌────────────────┬────────────────────────────────────────┐
 *   │  目录区 16KB   │          数据区 8MB-16KB               │
 *   │ (货物登记本)   │      (实际货物存放的仓库)              │
 *   └────────────────┴────────────────────────────────────────┘
 *
 *   - 目录区：像一本"登记本"，每登记一条占16字节，记录:
 *       货号(id)、货物大小(size)、放在哪个货架位置(start_addr)、校验码(crc16)
 *   - 数据区：像仓库的货架，货物（字库数据）一个挨一个紧密存放
 *
 *   "空间计算" = 找下一个空闲的货架位置
 *   "货物分装" = 把大数据切成多页，一页页写入 Flash
 *
 * ============================================================================
 */

#include "W25Q64.h"
#include <string.h>

/*===========================================================================
 * 模块内部变量
 *===========================================================================*/
static W25Q64_DirEntry_t g_DirCache[W25Q64_DIR_MAX_ENTRIES]; /* 目录内存缓存  */
static uint32_t          g_NextFreeAddr = 0;                  /* 下一个可写地址 */
static uint8_t           g_DirDirty     = 0;                  /* 目录是否需回写 */

/*===========================================================================
 * 底层 SPI 收发（基于 bsp_spi 的宏）
 *===========================================================================*/
static uint8_t SPI_SendByte(uint8_t byte) {
    return SPI_Flash_SendByte(byte);
}

#define CS_LOW()    SPI_FLASH_CS_LOW()
#define CS_HIGH()   SPI_FLASH_CS_HIGH()

/*===========================================================================
 * 底层 Flash 命令操作
 *===========================================================================*/

/**
 * @brief  初始化 W25Q64（基于 bsp_spi 中已有的 SPI_Init）
 */
void W25Q64_Init(void)
{
    SPI_Flash_Init();
    CS_HIGH();
}

/**
 * @brief  读取厂商/设备 ID
 */
uint32_t W25Q64_ReadID(void)
{
    return SPI_Flash_ReadID();
}

/**
 * @brief  读取状态寄存器
 */
uint8_t W25Q64_ReadSR(void)
{
    uint8_t sr;
    CS_LOW();
    SPI_SendByte(W25X_ReadStatusReg);
    sr = SPI_SendByte(Dummy_Byte);
    CS_HIGH();
    return sr;
}

/**
 * @brief  写使能
 */
void W25Q64_WriteEnable(void)
{
    CS_LOW();
    SPI_SendByte(W25X_WriteEnable);
    CS_HIGH();
}

/**
 * @brief  等待 Flash 内部操作完成
 */
void W25Q64_WaitBusy(void)
{
    uint32_t timeout = 0xFFFFFF;
    while ((W25Q64_ReadSR() & W25Q64_SR_BUSY) != 0) {
        if (--timeout == 0) break;
    }
}

/**
 * @brief  扇区擦除（4KB）
 * @param  addr  扇区内的任意地址（自动对齐到扇区边界）
 */
void W25Q64_SectorErase(uint32_t addr)
{
    W25Q64_WriteEnable();
    CS_LOW();
    SPI_SendByte(W25X_SectorErase);
    SPI_SendByte((uint8_t)(addr >> 16));
    SPI_SendByte((uint8_t)(addr >> 8));
    SPI_SendByte((uint8_t)(addr));
    CS_HIGH();
    W25Q64_WaitBusy();
}

/**
 * @brief  块擦除（64KB）
 */
void W25Q64_BlockErase(uint32_t addr)
{
    W25Q64_WriteEnable();
    CS_LOW();
    SPI_SendByte(W25X_BlockErase);
    SPI_SendByte((uint8_t)(addr >> 16));
    SPI_SendByte((uint8_t)(addr >> 8));
    SPI_SendByte((uint8_t)(addr));
    CS_HIGH();
    W25Q64_WaitBusy();
}

/**
 * @brief  整片擦除（谨慎使用，耗时较长）
 */
void W25Q64_ChipErase(void)
{
    W25Q64_WriteEnable();
    CS_LOW();
    SPI_SendByte(W25X_ChipErase);
    CS_HIGH();
    W25Q64_WaitBusy();
}

/**
 * @brief  页写入（单页最多256字节，地址不能跨页）
 * @param  addr  写入起始地址
 * @param  buf   数据缓冲区
 * @param  len   写入长度（必须 ≤ 256 且不跨页）
 */
void W25Q64_PageWrite(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    if (len == 0) return;
    if (len > W25Q64_PAGE_SIZE) len = W25Q64_PAGE_SIZE;

    W25Q64_WriteEnable();
    CS_LOW();
    SPI_SendByte(W25X_PageProgram);
    SPI_SendByte((uint8_t)(addr >> 16));
    SPI_SendByte((uint8_t)(addr >> 8));
    SPI_SendByte((uint8_t)(addr));
    for (i = 0; i < len; i++) {
        SPI_SendByte(buf[i]);
    }
    CS_HIGH();
    W25Q64_WaitBusy();
}

/**
 * @brief  任意长度、任意地址读取
 * @param  addr  读取起始地址
 * @param  buf   接收缓冲区
 * @param  len   读取长度
 */
void W25Q64_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;
    if (len == 0) return;

    CS_LOW();
    SPI_SendByte(W25X_ReadData);
    SPI_SendByte((uint8_t)(addr >> 16));
    SPI_SendByte((uint8_t)(addr >> 8));
    SPI_SendByte((uint8_t)(addr));
    for (i = 0; i < len; i++) {
        buf[i] = SPI_SendByte(Dummy_Byte);
    }
    CS_HIGH();
}

/*===========================================================================
 * CRC16 校验工具
 *===========================================================================*/

/**
 * @brief  CRC16-CCITT 校验计算
 *         用于验证写入数据的完整性
 */
uint16_t W25Q64_CRC16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFF;
    uint32_t i, j;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = (crc << 1);
        }
    }
    return crc;
}

/*===========================================================================
 * 不定量数据写入 —— 核心算法
 *===========================================================================*/

/**
 * ---------------------------------------------------------------------------
 *  算法1：写入变长数据 — 跨页"切面包"算法
 * ---------------------------------------------------------------------------
 *
 *  【问题】
 *   W25Q64 的页编程命令一次最多写 256 字节，且不能自动跨页。
 *   如果我要写入 1000 字节的数据，怎么处理？
 *
 *  【解决思路 —— "切面包"】
 *   把一大块数据想象成一条面包，Flash 的每一页（256B）是一个"面包袋"，
 *   每个袋子只能装 256 片。如果面包太长：
 *
 *     第1步：切下第 0~255 字节 → 装进第1个袋子（写第1页）
 *     第2步：切下第 256~511 字节 → 装进第2个袋子（写第2页）
 *     第3步：切下第 512~767 字节 → 装进第3个袋子（写第3页）
 *     第4步：切下第 768~999 字节 → 装进第4个袋子（写第4页）
 *
 *   注意：在写入第1个袋子之前，需要先把该袋子所在的"货厢"（扇区，4KB）
 *   擦干净。后续袋子如果在同一货厢内，就不用再擦了。
 *
 * ---------------------------------------------------------------------------
 *
 *  【算法伪代码】
 *
 *   function 切面包并写入(起始地址, 数据指针, 总长度):
 *      剩余长度 = 总长度
 *      当前地址 = 起始地址
 *      数据偏移 = 0
 *
 *      循环: 只要 剩余长度 > 0
 *          |
 *          1. 空间计算 —— 我这个位置能写多少？
 *          |   当前页剩余 = 256 - (当前地址 % 256)
 *          |   本次写入量 = min(剩余长度, 当前页剩余)
 *          |
 *          2. 判断是否需要擦除扇区
 *          |   如果 (当前地址 % 4096 == 0)
 *          |       擦除该扇区
 *          |
 *          3. 写入这一"片"
 *          |   PageWrite(当前地址, 数据+数据偏移, 本次写入量)
 *          |
 *          4. 更新位置，继续下一轮
 *          |   当前地址 += 本次写入量
 *          |   数据偏移 += 本次写入量
 *          |   剩余长度 -= 本次写入量
 *          |
 *      结束循环
 *      return 成功
 *
 * ---------------------------------------------------------------------------
 */
static int32_t W25Q64_WriteVariableData(uint32_t start_addr,
                                         const uint8_t *data,
                                         uint32_t size)
{
    uint32_t remain     = size;         /* 还剩下多少字节要写 */
    uint32_t cur_addr   = start_addr;   /* 当前写入地址 */
    uint32_t offset     = 0;            /* 数据指针偏移 */
    uint32_t last_erased_sector = 0xFFFFFFFF;  /* 上次擦除过的扇区 */

    while (remain > 0)
    {
        /*
         * 步骤1：空间计算 —— 当前页还能写多少？
         *
         * 例如：cur_addr = 0x000120（288），则 256 - (288 % 256) = 224 字节可用
         *       这就是"页内剩余空间"
         */
        uint32_t page_offset = cur_addr % W25Q64_PAGE_SIZE;
        uint32_t space_in_page = W25Q64_PAGE_SIZE - page_offset;
        uint32_t chunk = (remain < space_in_page) ? remain : space_in_page;

        /*
         * 步骤2：扇区擦除检查
         *
         * 扇区大小 4KB（4096 字节），只有在扇区起始位置才需要擦除。
         * 同一个扇区内连续写多页时，只擦第一次。
         */
        uint32_t cur_sector = cur_addr / W25Q64_SECTOR_SIZE;
        if (cur_sector != last_erased_sector) {
            /*
             * 换到新扇区了，先擦除再写。
             * 擦除是以扇区为单位，必须一次性擦4KB。
             */
            W25Q64_SectorErase(cur_addr);
            last_erased_sector = cur_sector;
        }

        /*
         * 步骤3：写入这一"片"面包
         */
        W25Q64_PageWrite(cur_addr, data + offset, (uint16_t)chunk);

        /*
         * 步骤4：推进指针，准备下一轮
         */
        cur_addr += chunk;
        offset   += chunk;
        remain   -= chunk;
    }

    return 0;  /* 成功 */
}


/*===========================================================================
 * 目录管理 —— "登记本"的增删查
 *===========================================================================*/

/**
 * @brief  把目录缓存写入 Flash（回写登记本）
 */
static void W25Q64_DirFlush(void)
{
    uint32_t dir_size = W25Q64_DIR_MAX_ENTRIES * sizeof(W25Q64_DirEntry_t);
    uint32_t i;

    /* 擦除目录区所有扇区 */
    for (i = 0; i < W25Q64_DIR_SIZE; i += W25Q64_SECTOR_SIZE) {
        W25Q64_SectorErase(W25Q64_DIR_START_ADDR + i);
    }

    /* 一次性把目录缓存写入（内部会跨页自动切分） */
    W25Q64_WriteVariableData(W25Q64_DIR_START_ADDR,
                             (uint8_t *)g_DirCache, dir_size);

    g_DirDirty = 0;
}

/**
 * @brief  从 Flash 加载目录到内存缓存（打开登记本翻一遍）
 */
static void W25Q64_DirLoad(void)
{
    uint32_t dir_size = W25Q64_DIR_MAX_ENTRIES * sizeof(W25Q64_DirEntry_t);
    W25Q64_Read(W25Q64_DIR_START_ADDR, (uint8_t *)g_DirCache, dir_size);
}

/**
 * @brief  在目录中查找一个空闲位置
 * @return 0~127=目录索引, -1=没有空位
 */
static int16_t W25Q64_FindFreeEntry(void)
{
    int16_t i;
    for (i = 0; i < W25Q64_DIR_MAX_ENTRIES; i++) {
        if (g_DirCache[i].used != 0x5A) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief  根据 id 查找目录项
 * @return 0~127=目录索引, -1=没找到
 */
static int16_t W25Q64_FindEntryByID(uint16_t id)
{
    int16_t i;
    for (i = 0; i < W25Q64_DIR_MAX_ENTRIES; i++) {
        if (g_DirCache[i].used == 0x5A && g_DirCache[i].id == id) {
            return i;
        }
    }
    return -1;
}

/**
 * ---------------------------------------------------------------------------
 *  算法2：空间计算 —— 新货物应该放在哪个货架位置？
 * ---------------------------------------------------------------------------
 *
 *  【问题】
 *   要写入一个新文件（比如 12 号字库），怎么知道它该放在 Flash 的哪个地址？
 *
 *  【解决思路 —— "紧挨着码放"】
 *
 *   把数据区想象成一条长长的货架，每个文件（货物）一个挨一个紧密码放：
 *
 *   货架起始                         货架末尾
 *   ┌──────┬────────┬─────┬──────────┬──────┬───────────┐
 *   │ 1号  │  2号   │ 3号 │   4号    │ 空位 │    空     │
 *   │ 4KB  │  12KB  │ 8KB │   6KB    │      │           │
 *   └──────┴────────┴─────┴──────────┴──────┴───────────┘
 *                                        ↑
 *                                   下一个可写位置
 *
 *   找出下一个空位的算法：
 *     扫描所有"已使用"的目录项，找到它们中 起始地址+大小 的最大值，
 *     这个最大值就是"已经占用的最大位置"，下一个可写位置 = 那个最大值的末尾。
 *
 *   数学表达：
 *     next_addr = max( 每个文件.start_addr + 每个文件.size )，对所有已用文件
 *     如果没有任何文件，则 next_addr = 数据区起始地址
 *
 * ---------------------------------------------------------------------------
 */
static uint32_t W25Q64_CalcNextFreeAddr(void)
{
    uint32_t next_addr = W25Q64_DATA_START_ADDR;  /* 初始值：数据区起点 */
    int16_t i;

    for (i = 0; i < W25Q64_DIR_MAX_ENTRIES; i++) {
        if (g_DirCache[i].used == 0x5A) {
            /* 这个位置有货，计算它的"尾部地址" */
            uint32_t file_end = g_DirCache[i].start_addr + g_DirCache[i].size;
            if (file_end > next_addr) {
                next_addr = file_end;  /* 更新为更大的尾部 */
            }
        }
    }
    return next_addr;
}


/*===========================================================================
 * 对外公开的文件系统接口
 *===========================================================================*/

/**
 * @brief  初始化文件系统
 *         上电后调用一次，加载目录并计算当前空闲位置
 */
void W25Q64_FileSysInit(void)
{
    W25Q64_Init();
    W25Q64_DirLoad();
    g_NextFreeAddr = W25Q64_CalcNextFreeAddr();
    g_DirDirty = 0;

    printf("[W25Q64] 文件系统初始化完成\n");
    printf("         下一个可写地址: 0x%06X (%d KB)\n",
           (unsigned int)g_NextFreeAddr,
           (int)(g_NextFreeAddr / 1024));
}

/**
 * ---------------------------------------------------------------------------
 *  @brief  写入一个文件（不定量数据写入的完整流程）
 *
 *  【工作流程 —— "入库登记 → 找位置 → 搬货 → 记录"】
 *
 *  1. 校验：检查 id 是否已存在（若存在则覆盖旧文件）
 *  2. 空间计算：找到 Flash 中的下一个空闲起始地址
 *  3. 容量检查：确认剩余空间足够
 *  4. 货物分装（切面包写入）：
 *      把数据按页大小切片，一页页写入 Flash
 *      遇到扇区边界时先擦除再写
 *  5. 登记入册：
 *      在目录区记录 货号、大小、位置、校验码
 *      把目录回写到 Flash
 *
 *  @param  id    数据编号（你自己定义，比如 0=16字库, 1=24字库, 2=天气图标）
 *  @param  data  数据指针
 *  @param  size  数据大小（字节，可以任意大小！）
 *  @return 0=成功, -1=空间不足, -2=目录已满
 * ---------------------------------------------------------------------------
 */
int32_t W25Q64_FileWrite(uint16_t id, const uint8_t *data, uint32_t size)
{
    int16_t entry_idx;

    if (data == NULL || size == 0) return -3;  /* 参数无效 */

    /*
     * 步骤1：检查 id 是否已存在，若存在则标记为删除
     *        等写入新数据后，旧数据自然失去引用（目录指向新地址）
     */
    entry_idx = W25Q64_FindEntryByID(id);
    if (entry_idx >= 0) {
        /* 先标记旧条目为"已删除"（不立即回收空间，下次整理时再处理） */
        g_DirCache[entry_idx].used = 0xA5;
        g_DirDirty = 1;
    }

    /*
     * 步骤2：空间计算 —— 决定新数据放哪里
     *
     * 重新计算下一个空闲地址。因为刚才可能标记删除了一个条目，
     * 但被删除的条目占用的物理空间暂时不回收（避免频繁移动数据），
     * 新数据仍然追加到末尾。
     *
     * 这样做的原因是：Flash 擦除按扇区（4KB），如果被删除的文件
     * 位于两个有效文件之间，那个空洞大小的空间很难直接复用。
     * 所以采用"追加写入"策略：新数据始终往后放。
     */
    g_NextFreeAddr = W25Q64_CalcNextFreeAddr();

    /*
     * 步骤3：容量检查
     */
    if (g_NextFreeAddr + size > W25Q64_DATA_START_ADDR + W25Q64_DATA_TOTAL_SIZE) {
        printf("[W25Q64] 空间不足！需要 %lu 字节，仅剩 %lu 字节\n",
               (unsigned long)size,
               (unsigned long)(W25Q64_DATA_START_ADDR + W25Q64_DATA_TOTAL_SIZE - g_NextFreeAddr));
        return -1;  /* 空间不足 */
    }

    /*
     * 步骤4：货物分装 —— 把不定量数据拆成页大小，逐页写入
     *
     * 这是整个算法的核心！调用内部函数 W25Q64_WriteVariableData，
     * 它会自动处理：
     *   - 页内剩余空间计算（空间计算）
     *   - 跨页拆分（货物分装）
     *   - 扇区擦除（到新扇区时自动擦）
     */
    printf("[W25Q64] 开始写入 id=%u, size=%lu 字节, 起始地址=0x%06X\n",
           id, (unsigned long)size, (unsigned int)g_NextFreeAddr);

    if (W25Q64_WriteVariableData(g_NextFreeAddr, data, size) != 0) {
        return -4;  /* 写入失败 */
    }

    /*
     * 步骤5：登记入册 —— 在目录中记录这个新文件
     */
    entry_idx = W25Q64_FindFreeEntry();
    if (entry_idx < 0) {
        printf("[W25Q64] 目录已满！\n");
        return -2;
    }

    g_DirCache[entry_idx].used       = 0x5A;   /* 标记已使用 */
    g_DirCache[entry_idx].id         = id;
    g_DirCache[entry_idx].size       = size;
    g_DirCache[entry_idx].start_addr = g_NextFreeAddr;
    g_DirCache[entry_idx].crc16      = W25Q64_CRC16(data, size);
    g_DirDirty = 1;

    /* 把更新后的目录回写到 Flash */
    W25Q64_DirFlush();

    /* 更新下一个空闲地址 */
    g_NextFreeAddr += size;

    printf("[W25Q64] 写入完成！id=%u, CRC16=0x%04X, 地址=0x%06X~0x%06X\n",
           id,
           (unsigned int)g_DirCache[entry_idx].crc16,
           (unsigned int)g_DirCache[entry_idx].start_addr,
           (unsigned int)(g_DirCache[entry_idx].start_addr + size - 1));

    return 0;
}

/**
 * @brief  读取一个文件
 * @param  id          数据编号
 * @param  buf         接收缓冲区
 * @param  buf_size    缓冲区大小
 * @param  actual_size 输出参数：实际数据大小（不需要则传 NULL）
 * @return 0=成功, -1=文件不存在, -2=缓冲区太小
 */
int32_t W25Q64_FileRead(uint16_t id, uint8_t *buf, uint32_t buf_size,
                         uint32_t *actual_size)
{
    int16_t entry_idx = W25Q64_FindEntryByID(id);
    if (entry_idx < 0) {
        return -1;  /* 文件不存在 */
    }

    if (actual_size != NULL) {
        *actual_size = g_DirCache[entry_idx].size;
    }

    if (buf_size < g_DirCache[entry_idx].size) {
        return -2;  /* 缓冲区太小 */
    }

    /* 直接从 Flash 读取（不需要切分，读不涉及页限制） */
    W25Q64_Read(g_DirCache[entry_idx].start_addr,
                buf,
                g_DirCache[entry_idx].size);

    /* 校验 CRC */
    uint16_t calc_crc = W25Q64_CRC16(buf, g_DirCache[entry_idx].size);
    if (calc_crc != g_DirCache[entry_idx].crc16) {
        printf("[W25Q64] CRC校验失败！id=%u, 存储=0x%04X, 计算=0x%04X\n",
               id,
               (unsigned int)g_DirCache[entry_idx].crc16,
               (unsigned int)calc_crc);
        /* 校验失败仍返回数据，但返回错误码 */
        return -3;
    }

    return 0;
}

/**
 * @brief  删除一个文件（仅标记删除，物理空间不回收）
 */
int32_t W25Q64_FileDelete(uint16_t id)
{
    int16_t entry_idx = W25Q64_FindEntryByID(id);
    if (entry_idx < 0) {
        return -1;
    }

    g_DirCache[entry_idx].used = 0xA5;  /* 标记删除 */
    g_DirDirty = 1;
    W25Q64_DirFlush();

    printf("[W25Q64] 已删除 id=%u\n", id);
    return 0;
}

/**
 * @brief  获取数据区剩余空间（字节）
 */
int32_t W25Q64_GetFreeSpace(void)
{
    uint32_t used_end = W25Q64_CalcNextFreeAddr();
    uint32_t data_end = W25Q64_DATA_START_ADDR + W25Q64_DATA_TOTAL_SIZE;
    return (int32_t)(data_end - used_end);
}

/**
 * @brief  格式化：删除所有文件，擦除数据和目录
 */
void W25Q64_Format(void)
{
    uint32_t i;

    printf("[W25Q64] 正在格式化...\n");

    /* 擦除数据区（按块擦更快） */
    for (i = W25Q64_DATA_START_ADDR;
         i < W25Q64_DATA_START_ADDR + W25Q64_DATA_TOTAL_SIZE;
         i += W25Q64_BLOCK_SIZE) {
        W25Q64_BlockErase(i);
    }

    /* 擦除目录区 */
    for (i = 0; i < W25Q64_DIR_SIZE; i += W25Q64_SECTOR_SIZE) {
        W25Q64_SectorErase(W25Q64_DIR_START_ADDR + i);
    }

    /* 清零内存中的目录缓存 */
    memset(g_DirCache, 0, sizeof(g_DirCache));
    g_NextFreeAddr = W25Q64_DATA_START_ADDR;
    g_DirDirty = 0;

    printf("[W25Q64] 格式化完成！\n");
}