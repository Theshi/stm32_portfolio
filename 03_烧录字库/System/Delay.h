#ifndef __DELAY_H_
#define __DELAY_H_

#include "FreeRTOS.h"
#include "task.h"

#define delay_ms(_time) vTaskDelay(_time)

void Delay_us(uint32_t us);
void Delay_ms(uint32_t xms);
#endif
