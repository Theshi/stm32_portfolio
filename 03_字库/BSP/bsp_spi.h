/**
 * ============================================================================
 * bsp_spi.h — SPI 底层驱动 + W25Q64 命令宏定义
 * ============================================================================
 *
 * 【教学说明】
 *   这个头文件做了两件事：
 *   1. 定义 SPI1 的引脚连接（CS=PC0, SCK=PA5, MISO=PA6, MOSI=PA7）
 *   2. 定义 W25Q64 Flash 的所有操作码（命令字节）
 *
 *   W25Q64 的每个操作都是"发命令码 + 发地址 + 读/写数据"的模式，
 *   这些宏让你不用背那些十六进制数字。
 * ============================================================================
 */

#ifndef __SPI_H
#define __SPI_H

#include "stm32f10x.h"
#include <stdio.h>

/*===========================================================================
 * SPI 引脚宏定义
 *===========================================================================*/

/* CS 片选引脚 —— PC0，普通 GPIO 推挽输出 */
#define FLASH_SPI_CS_PORT                 GPIOC
#define FLASH_SPI_CS_PIN                  GPIO_Pin_0
#define FLASH_SPI_CS_APBxClock_FUN        RCC_APB2PeriphClockCmd
#define FLASH_SPI_CS_CLK                  RCC_APB2Periph_GPIOC

/* SCK 时钟引脚 —— PA5，SPI1 复用推挽 */
#define FLASH_SPI_SCK_PORT                GPIOA
#define FLASH_SPI_SCK_PIN                 GPIO_Pin_5
#define FLASH_SPI_SCK_APBxClock_FUN       RCC_APB2PeriphClockCmd
#define FLASH_SPI_SCK_CLK                 RCC_APB2Periph_GPIOA

/* MISO 主入从出 —— PA6，浮空输入 */
#define FLASH_SPI_MISO_PORT               GPIOA
#define FLASH_SPI_MISO_PIN                GPIO_Pin_6
#define FLASH_SPI_MISO_APBxClock_FUN      RCC_APB2PeriphClockCmd
#define FLASH_SPI_MISO_CLK                RCC_APB2Periph_GPIOA

/* MOSI 主出从入 —— PA7，SPI1 复用推挽 */
#define FLASH_SPI_MOSI_PORT               GPIOA
#define FLASH_SPI_MOSI_PIN                GPIO_Pin_7
#define FLASH_SPI_MOSI_APBxClock_FUN      RCC_APB2PeriphClockCmd
#define FLASH_SPI_MOSI_CLK                RCC_APB2Periph_GPIOA

/* SPI 外设号 */
#define FLASH_SPIx                        SPI1
#define FLASH_SPI_APBxClock_FUN           RCC_APB2PeriphClockCmd
#define FLASH_SPI_CLK                     RCC_APB2Periph_SPI1

/* CS 高低电平控制宏 */
#define SPI_FLASH_CS_LOW()                GPIO_ResetBits(FLASH_SPI_CS_PORT, FLASH_SPI_CS_PIN)
#define SPI_FLASH_CS_HIGH()               GPIO_SetBits(FLASH_SPI_CS_PORT, FLASH_SPI_CS_PIN)

/* 超时时间 */
#define SPIT_FLAG_TIMEOUT                 ((uint32_t)0x1000)
#define SPIT_LONG_TIMEOUT                 ((uint32_t)(10 * SPIT_FLAG_TIMEOUT))

/*===========================================================================
 * Flash 参数
 *===========================================================================*/

/* Flash 型号 ID */
#define sFLASH_ID                         0xEF4017    /* W25Q64 */

/* 页大小 */
#define SPI_FLASH_PageSize                 256
#define SPI_FLASH_PerWritePageSize         256

/* 哑字节 —— 读取时发送 0xFF 来产生时钟 */
#define Dummy_Byte                         0xFF

/* 状态寄存器位 */
#define WIP_Flag                           0x01        /* 忙标志位 */

/*===========================================================================
 * W25Q64 操作码（命令字节）定义
 *
 * 【教学】
 *   每个操作码是 Flash 芯片规定的，写在数据手册里。
 *   比如你发 0x06 → Flash 就知道你要"写使能"
 *   发 0x20 + 3字节地址 → Flash 就知道你要擦那个扇区
 *===========================================================================*/
#define W25X_WriteEnable                   0x06        /* 写使能          */
#define W25X_WriteDisable                  0x04        /* 写禁止          */
#define W25X_ReadStatusReg                 0x05        /* 读状态寄存器    */
#define W25X_WriteStatusReg                0x01        /* 写状态寄存器    */
#define W25X_ReadData                      0x03        /* 读数据          */
#define W25X_FastReadData                  0x0B        /* 快速读          */
#define W25X_FastReadDual                  0x3B        /* 双线快速读      */
#define W25X_PageProgram                   0x02        /* 页写入（≤256B）*/
#define W25X_BlockErase                    0xD8        /* 块擦除（64KB）  */
#define W25X_SectorErase                   0x20        /* 扇区擦除（4KB） */
#define W25X_ChipErase                     0xC7        /* 整片擦除        */
#define W25X_PowerDown                     0xB9        /* 掉电模式        */
#define W25X_ReleasePowerDown              0xAB        /* 退出掉电        */
#define W25X_DeviceID                      0xAB        /* 设备ID          */
#define W25X_ManufactDeviceID              0x90        /* 厂商ID          */
#define W25X_JedecDeviceID                 0x9F        /* JEDEC ID        */

/*===========================================================================
 * BSP 层函数声明
 *===========================================================================*/

void     SPI_Flash_Init(void);                                  /* SPI 初始化       */
uint8_t  SPI_Flash_SendByte(uint8_t byte);                      /* 收发一个字节     */
uint8_t  SPI_Flash_ReadByte(void);                              /* 读取一个字节     */
uint32_t SPI_Flash_ReadID(void);                                /* 读取 Flash ID    */

#endif /* __SPI_H */