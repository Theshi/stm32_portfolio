#ifndef __CORE_DELAY_H
#define __CORE_DELAY_H

#include "stm32f10x.h"

#define USE_DWT_DELAY		1	/* Use DWT core precise delay */

#if USE_DWT_DELAY
#define USE_TICK_DELAY		0	/* Don't use SysTick delay */
#else
#define USE_TICK_DELAY		1	/* Use SysTick delay */
#endif


#if USE_DWT_DELAY

#define Delay_ms(ms)  	CPU_TS_Tmr_Delay_MS(ms)
#define Delay_us(us)  	CPU_TS_Tmr_Delay_US(us)
/* Max delay 60s = 2^32 / 72000000 */
#define Delay_s(s)  	  CPU_TS_Tmr_Delay_S(s)

/* Get core clock frequency */
#define GET_CPU_ClkFreq()       (SystemCoreClock)
#define SysClockFreq            (SystemCoreClock)
/*
 * Set to 0 and call CPU_TS_TmrInit() once in main()
 * to avoid re-initialization on every delay call
 */
#define CPU_TS_INIT_IN_DELAY_FUNCTION   0


uint32_t CPU_TS_TmrRd(void);
void CPU_TS_TmrInit(void);

/* Must call CPU_TS_TmrInit() before using these */
void CPU_TS_Tmr_Delay_US(uint32_t us);
#define CPU_TS_Tmr_Delay_MS(ms)     CPU_TS_Tmr_Delay_US(ms*1000)
#define CPU_TS_Tmr_Delay_S(s)       CPU_TS_Tmr_Delay_MS(s*1000)

#endif

#endif /* __CORE_DELAY_H */
