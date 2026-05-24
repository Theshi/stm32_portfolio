/**
 * ============================================================================
 * bsp_spi.c — SPI 底层驱动实现（修复版 + 教学注释）
 * ============================================================================
 *
 * 【引脚连接】
 *   PC0 → CS  (片选)
 *   PA5 → SCK (时钟)
 *   PA7 → MOSI (主机输出)
 *   PA6 → MISO (主机输入)
 *
 * 【SPI 模式说明】
 *   W25Q64 支持 SPI 模式 0 和模式 3。这里使用模式 3：
 *   - CPOL=1: 时钟空闲时为高电平
 *   - CPHA=1: 数据在第二个时钟边沿（偶数边沿）采样
 *
 * 【SPI 速度】
 *   预分频器设为 4：72MHz ÷ 4 = 18MHz
 *   （注释写的 4.5MHz 与代码不一致，代码写的是 Prescaler_4 = 18MHz）
 *
 * ============================================================================
 * 原代码存在的问题（已修复）：
 *   1. 【Bug】第42行 Flash_SPI_CS_HIGH() → 修正为 SPI_FLASH_CS_HIGH()
 *   2. 【Bug】第60行 Dummy_Bute → 修正为 Dummy_Byte（拼写错误）
 *   3. 【Bug】第63行参数类型 u8 → 修正为 uint8_t
 *   4. 【Bug】第89行 Dummy_Bute → 修正为 Dummy_Byte
 *   5. 【Bug】第101/104/107/110行 SPI_FLASH_SendByte → 修正为 SPI_Flash_SendByte
 *   6. 【问题】超时回调函数 SPI_TIMEOUT_UserCallback 未实现 → 补充实现
 * ============================================================================
 */

#include "bsp_spi.h"

/*===========================================================================
 * 超时管理
 *===========================================================================*/

static __IO uint32_t SPITimeout = SPIT_LONG_TIMEOUT;

/**
 * @brief  SPI 通信超时回调
 *
 * 【教学】为什么需要超时处理？
 *   如果 SPI 硬件出问题（比如 Flash 焊坏了），while 循环会死等。
 *   超时机制就是"等太久就不等了，返回错误码"，防止程序卡死。
 *
 * @param  errorCode  0=发送超时, 1=接收超时
 * @return 返回 0xFF 作为错误时的假数据
 */
static uint8_t SPI_TIMEOUT_UserCallback(uint8_t errorCode)
{
    /* 这里可以加你自己的错误处理：亮LED、记录日志等 */
    printf("[SPI ERROR] Timeout! code=%d\r\n", errorCode);
    return 0xFF;  /* 返回 0xFF 作为哑数据 */
}

/*===========================================================================
 * SPI 初始化
 *===========================================================================*/

/**
 * @brief  初始化 SPI1 外设及 GPIO 引脚
 *
 * 【教学 — 初始化步骤拆解】
 *
 *  第1步：配置4个 GPIO 引脚
 *    - CS:  普通推挽输出（因为要手动拉高拉低）
 *    - SCK: 复用推挽输出（SPI 硬件自动控制）
 *    - MOSI: 复用推挽输出（SPI 硬件自动控制）
 *    - MISO: 浮空输入（接收 Flash 发来的数据）
 *
 *  第2步：使能 SPI1 时钟 + 4个引脚所在的 GPIO 时钟
 *
 *  第3步：配置 SPI 工作参数
 *    - 全双工（同时收发）
 *    - 主机模式（STM32 是主机，Flash 是从机）
 *    - 8位数据帧
 *    - 模式3（CPOL=1, CPHA=1）
 *    - 软件 NSS（不用硬件片选，自己用 GPIO 控制）
 *    - 分频系数4 → 18MHz
 *    - MSB 高位先行
 */
void SPI_Flash_Init(void)
{
    SPI_InitTypeDef  SPI_InitStruct;
    GPIO_InitTypeDef GPIO_InitStruct;

    /* ---- 第1步：先使能时钟（GPIO 寄存器必须有时钟才能配置！）---- */

    /* 使能 SPI1 时钟 */
    FLASH_SPI_APBxClock_FUN(FLASH_SPI_CLK, ENABLE);

    /* 使能 GPIO 端口时钟（4个引脚分属 GPIOA 和 GPIOC） */
    FLASH_SPI_CS_APBxClock_FUN(FLASH_SPI_CS_CLK
                             | FLASH_SPI_SCK_CLK
                             | FLASH_SPI_MISO_CLK
                             | FLASH_SPI_MOSI_CLK, ENABLE);

    /* ---- 第2步：GPIO 引脚配置（时钟已开，配置才生效）---- */

    /* CS 引脚 —— 普通推挽输出，手动控制 */
    GPIO_InitStruct.GPIO_Pin   = FLASH_SPI_CS_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(FLASH_SPI_CS_PORT, &GPIO_InitStruct);

    /* SCK 引脚 —— SPI 复用推挽 */
    GPIO_InitStruct.GPIO_Pin   = FLASH_SPI_SCK_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(FLASH_SPI_SCK_PORT, &GPIO_InitStruct);

    /* MISO 引脚 —— 浮空输入 */
    GPIO_InitStruct.GPIO_Pin   = FLASH_SPI_MISO_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(FLASH_SPI_MISO_PORT, &GPIO_InitStruct);

    /* MOSI 引脚 —— SPI 复用推挽 */
    GPIO_InitStruct.GPIO_Pin   = FLASH_SPI_MOSI_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(FLASH_SPI_MOSI_PORT, &GPIO_InitStruct);

    /* CS 默认拉高（Flash 在 CS 高电平时不响应） */
    SPI_FLASH_CS_HIGH();    /* 【修复】原代码: Flash_SPI_CS_HIGH() */

    /* ---- 第3步：SPI 模式配置 ---- */
    SPI_InitStruct.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;  /* 全双工 */
    SPI_InitStruct.SPI_Mode              = SPI_Mode_Master;                  /* 主机模式 */
    SPI_InitStruct.SPI_DataSize          = SPI_DataSize_8b;                  /* 8位数据 */
    SPI_InitStruct.SPI_CPOL              = SPI_CPOL_High;                    /* 空闲时钟=高 */
    SPI_InitStruct.SPI_CPHA              = SPI_CPHA_2Edge;                   /* 偶数边沿采样 → 模式3 */
    SPI_InitStruct.SPI_NSS               = SPI_NSS_Soft;                     /* 软件片选 */
    SPI_InitStruct.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;          /* 72MHz/4=18MHz */
    SPI_InitStruct.SPI_FirstBit          = SPI_FirstBit_MSB;                 /* 高位先行 */
    SPI_InitStruct.SPI_CRCPolynomial     = 7;                                /* CRC多项式 */

    SPI_Init(FLASH_SPIx, &SPI_InitStruct);
    SPI_Cmd(FLASH_SPIx, ENABLE);
}

/*===========================================================================
 * SPI 字节收发
 *===========================================================================*/

/**
 * @brief  通过 SPI 发送一个字节，同时接收一个字节
 *
 * 【教学 — SPI 全双工原理】
 *   SPI 是全双工的：你发一个字节的同时，也会收到一个字节。
 *   主机每产生一个时钟脉冲，就把 MOSI 上的一位数据推出去，
 *   同时从 MISO 上采样一位数据进来。8个脉冲后，收发都完成。
 *
 *   所以即使你只想"读"Flash的数据，也必须先"发"一个字节
 *   来产生时钟。这就是 Dummy_Byte(0xFF) 的作用。
 *
 *   ┌─────────┐    MOSI     ┌──────────┐
 *   │  STM32  │ ──────────→ │ W25Q64   │
 *   │ (主机)  │ ←────────── │ (从机)   │
 *   └─────────┘    MISO     └──────────┘
 *
 * 【超时机制】
 *   每次检查标志位前设定 SPITimeout，每查一次减1，
 *   减到0说明硬件异常，调用回调并返回 0xFF。
 *
 * @param  byte  要发送的字节
 * @return 同时接收到的字节
 */
uint8_t SPI_Flash_SendByte(uint8_t byte)
{
    /* 等待发送缓冲区为空（TXE = Transmit Empty） */
    SPITimeout = SPIT_FLAG_TIMEOUT;
    while (SPI_I2S_GetFlagStatus(FLASH_SPIx, SPI_I2S_FLAG_TXE) == RESET)
    {
        if ((SPITimeout--) == 0)
            return SPI_TIMEOUT_UserCallback(0);
    }
    /* 发送数据（写 DR 寄存器） */
    SPI_I2S_SendData(FLASH_SPIx, byte);

    /* 等待接收缓冲区非空（RXNE = Receive Not Empty） */
    SPITimeout = SPIT_FLAG_TIMEOUT;
    while (SPI_I2S_GetFlagStatus(FLASH_SPIx, SPI_I2S_FLAG_RXNE) == RESET)
    {
        if ((SPITimeout--) == 0)
            return SPI_TIMEOUT_UserCallback(1);
    }
    /* 读取接收到的数据 */
    return (uint8_t)SPI_I2S_ReceiveData(FLASH_SPIx);
}

/**
 * @brief  从 Flash 读取一个字节
 *
 * 【教学】为什么是 SendByte(Dummy_Byte)？
 *   因为 SPI 的时钟是由主机（STM32）产生的。
 *   你要"收"数据，必须先"发"一个字节来产生8个时钟脉冲，
 *   Flash 在这8个脉冲里把数据推到 MISO 上。
 *
 *   发出的 0xFF 是"哑数据"，Flash 会忽略它。
 *
 *   【修复】原代码 Dummy_Bute → Dummy_Byte
 */
uint8_t SPI_Flash_ReadByte(void)
{
    return SPI_Flash_SendByte(Dummy_Byte);    /* 【修复】原代码: Dummy_Bute */
}

/*===========================================================================
 * Flash ID 读取
 *===========================================================================*/

/**
 * @brief  读取 W25Q64 的 JEDEC 厂商和设备 ID
 *
 * 【教学 — JEDEC ID 格式】
 *   W25Q64 的 JEDEC ID 是 3 个字节：
 *     Byte1: 制造商 ID = 0xEF (Winbond 华邦)
 *     Byte2: 内存类型   = 0x40 (SPI Flash)
 *     Byte3: 容量       = 0x17 (8MB / 64Mbit)
 *   返回值 = 0xEF4017
 *
 * 【通信流程】
 *   1. CS 拉低（开始通信）
 *   2. 发送命令 0x9F（JEDEC ID 命令）
 *   3. 连续读3个字节（每次发 0xFF 产生时钟）
 *   4. CS 拉高（结束通信）
 *   5. 组合成 32 位返回值
 *
 *   【修复】原代码 SPI_FLASH_SendByte → SPI_Flash_SendByte
 */
uint32_t SPI_Flash_ReadID(void)
{
    uint32_t Temp  = 0;
    uint8_t  Temp0 = 0, Temp1 = 0, Temp2 = 0;

    /* 开始通信 */
    SPI_FLASH_CS_LOW();

    /* 发送 JEDEC ID 命令 0x9F */
    SPI_Flash_SendByte(W25X_JedecDeviceID);

    /* 连续读取 3 字节 ID */
    Temp0 = SPI_Flash_SendByte(Dummy_Byte);
    Temp1 = SPI_Flash_SendByte(Dummy_Byte);
    Temp2 = SPI_Flash_SendByte(Dummy_Byte);

    /* 结束通信 */
    SPI_FLASH_CS_HIGH();

    /* 组合：Temp0 放高16位, Temp1 放中8位, Temp2 放低8位 */
    Temp = ((uint32_t)Temp0 << 16) | ((uint32_t)Temp1 << 8) | Temp2;

    return Temp;
}