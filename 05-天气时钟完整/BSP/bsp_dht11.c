/**
 * ============================================================================
 * bsp_dht11.c — DHT11 温湿度传感器驱动
 *
 * 单总线协议，一次完整传输 40bit：
 *   8bit 湿度整数 + 8bit 湿度小数 + 8bit 温度整数 + 8bit 温度小数 + 8bit 校验和
 * ============================================================================
 */
#include "bsp_dht11.h"
#include "core_delay.h"


/* 内部函数声明 */
static void     DHT11_GPIO_Init(void);
static void     DHT11_Mode_IPU(void);
static void     DHT11_Mode_Out_PP(void);
static uint8_t  DHT11_Read_Byte(void);


/**
 * @brief  DHT11 初始化
 */
void DHT11_Init(void)
{
    DHT11_GPIO_Init();
    DHT11_Output_High();
}


/**
 * @brief  配置 DHT11 数据引脚为推挽输出
 */
static void DHT11_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(DHT11_GPIO_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = DHT11_GPIO_Pin;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStructure);
}


/**
 * @brief  切换为输入模式（上拉）
 */
static void DHT11_Mode_IPU(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    GPIO_InitStructure.GPIO_Pin   = DHT11_GPIO_Pin;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStructure);
}


/**
 * @brief  切换为推挽输出模式
 */
static void DHT11_Mode_Out_PP(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    GPIO_InitStructure.GPIO_Pin   = DHT11_GPIO_Pin;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStructure);
}


/**
 * @brief  从 DHT11 读取一个字节（MSB 高位在前）
 */
static uint8_t DHT11_Read_Byte(void)
{
    uint8_t i, data = 0;

    for (i = 0; i < 8; i++)
    {
        /* 等待 50us 低电平结束 */
        while (DHT11_Input() == Bit_RESET);

        Delay_us(40);

        if (DHT11_Input() == Bit_SET)
        {
            /* 高电平 > 40us → bit "1"，等它结束 */
            while (DHT11_Input() == Bit_SET);
            data |= (uint8_t)(0x01 << (7 - i));
        }
        else
        {
            /* 高电平 < 40us → bit "0" */
            data &= (uint8_t)~(0x01 << (7 - i));
        }
    }

    return data;
}


/**
 * @brief  读取 DHT11 温湿度数据
 * @param  DHT11_Data : 存放读取结果的结构体指针
 * @retval SUCCESS : 读取成功（校验通过）
 *         ERROR   : 读取失败或无应答
 */
uint8_t DHT11_Read_TempAndHumidity(DHT11_Data_Type *DHT11_Data)
{
    /* 输出模式，主机拉低 ≥18ms 启动信号 */
    DHT11_Mode_Out_PP();
    DHT11_Output_Low();
    Delay_ms(18);

    /* 拉高 30us 后切换为输入，等待 DHT11 应答 */
    DHT11_Output_High();
    Delay_us(30);
    DHT11_Mode_IPU();

    /* 判断从机是否有低电平应答信号 */
    if (DHT11_Input() == Bit_RESET)
    {
        /* 等待 80us 低电平应答结束 */
        while (DHT11_Input() == Bit_RESET);

        /* 等待 80us 高电平标置信号结束 */
        while (DHT11_Input() == Bit_SET);

        /* 读取 40bit 数据 */
        DHT11_Data->humi_int  = DHT11_Read_Byte();
        DHT11_Data->humi_deci = DHT11_Read_Byte();
        DHT11_Data->temp_int  = DHT11_Read_Byte();
        DHT11_Data->temp_deci = DHT11_Read_Byte();
        DHT11_Data->check_sum = DHT11_Read_Byte();

        /* 恢复输出模式，拉高总线 */
        DHT11_Mode_Out_PP();
        DHT11_Output_High();

        /* 校验：前四项之和应等于校验和 */
        if (DHT11_Data->check_sum ==
            DHT11_Data->humi_int  + DHT11_Data->humi_deci +
            DHT11_Data->temp_int  + DHT11_Data->temp_deci)
            return SUCCESS;
        else
            return ERROR;
    }
    else
    {
        return ERROR;
    }
}
