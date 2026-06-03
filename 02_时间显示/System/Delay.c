#include "stm32f10x.h"
#include "SysTick.h"
#include "FreeRTOS.h"	//FreeRTOSʹ��		  
#include "task.h"

uint16_t last_time_tick;//��¼��һ�ε�tick��ֵ
/**
  * @brief  ΢�뼶��ʱ
  * @param  xus
  * @retval ��
  */
void Delay_us(uint32_t xus)
{
	double systick_time = 1000000.0/CurrentFrequency;//ÿһ��tick��ʱ��
	uint32_t last_SysTik;
	uint32_t last_time_over = 0; //�������
	
	last_SysTik = SysTick->VAL;
	last_time_tick = SysTick->VAL;
	while(1){	
		if(last_time_over == 0 && ((last_time_tick - SysTick->VAL)*systick_time>xus)){
			break;
		}
		else if(last_time_over > 0 && ((last_time_tick*systick_time+(last_time_over-1)*SysTick->LOAD*systick_time+(SysTick->LOAD-SysTick->VAL)*systick_time)>xus)){;
			break;
		}
		
		if(last_SysTik<SysTick->VAL){//���ڳ���˵��������һ����װ��
			last_time_over++;
		}
		last_SysTik = SysTick->VAL;
	}
}

void Delay_ms(uint32_t xms){
	Delay_us(xms*1000);
}
