#include "stm32f10x.h"                  // Device header	  
#include "task.h"

uint32_t CurrentFrequency;

void SysTick_Init()
{
	RCC_ClocksTypeDef get_rcc_clock; 
	RCC_GetClocksFreq(&get_rcc_clock);
	
	SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK);//ѡ���ⲿʱ��
	CurrentFrequency = get_rcc_clock.SYSCLK_Frequency;
	SysTick->LOAD = get_rcc_clock.SYSCLK_Frequency/configTICK_RATE_HZ;
	
	SysTick->CTRL|=SysTick_CTRL_TICKINT_Msk;   	//����SYSTICK�ж�
	SysTick->CTRL|=SysTick_CTRL_ENABLE_Msk;   	//����SYSTICK		 	   
}								    
					    
