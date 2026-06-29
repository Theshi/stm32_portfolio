#ifndef __COMMON_H
#define __COMMON_H

#include "stm32f10x.h"

/* NVIC priority grouping used by FreeRTOS (NVIC_PriorityGroup_4) */
#define macNVIC_PriorityGroup_x    NVIC_PriorityGroup_4

/* USART printf — lightweight formatted output without C library linkage */
void USART_printf(USART_TypeDef *USARTx, char *Data, ...);

#endif /* __COMMON_H */
