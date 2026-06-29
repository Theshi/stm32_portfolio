/************************************************************
  * @brief   core_delay.c — DWT cycle counter microsecond delay
  * @note    Uses Cortex-M DWT hardware, not SysTick
  *          Max delay: 60 seconds @ 72MHz
  ***********************************************************/

#include "core_delay.h"


#if USE_DWT_DELAY

#define  DWT_CR      *(__IO uint32_t *)0xE0001000
#define  DWT_CYCCNT  *(__IO uint32_t *)0xE0001004
#define  DEM_CR      *(__IO uint32_t *)0xE000EDFC

#define  DEM_CR_TRCENA                   (1 << 24)
#define  DWT_CR_CYCCNTENA                (1 <<  0)


/**
  * @brief  Initialize DWT cycle counter
  * @param  None
  * @retval None
  */
void CPU_TS_TmrInit(void)
{
    /* Enable DWT trace */
    DEM_CR |= (uint32_t)DEM_CR_TRCENA;

    /* Clear cycle counter */
    DWT_CYCCNT = (uint32_t)0u;

    /* Enable DWT cycle counter */
    DWT_CR |= (uint32_t)DWT_CR_CYCCNTENA;
}


/**
  * @brief  Read current DWT cycle count
  * @param  None
  * @retval Current DWT_CYCCNT value
  */
uint32_t CPU_TS_TmrRd(void)
{
  return ((uint32_t)DWT_CYCCNT);
}


/**
  * @brief  Microsecond delay using DWT cycle counter (32-bit)
  * @param  us : delay length in microseconds
  * @retval None
  */
void CPU_TS_Tmr_Delay_US(__IO uint32_t us)
{
  uint32_t ticks;
  uint32_t told, tnow, tcnt = 0;

#if (CPU_TS_INIT_IN_DELAY_FUNCTION)
  CPU_TS_TmrInit();
#endif

  ticks = us * (GET_CPU_ClkFreq() / 1000000);
  tcnt = 0;
  told = (uint32_t)CPU_TS_TmrRd();

  while (1)
  {
    tnow = (uint32_t)CPU_TS_TmrRd();
    if (tnow != told)
    {
        /* 32-bit counter handles wrap-around */
      if (tnow > told)
      {
        tcnt += tnow - told;
      }
      else
      {
        tcnt += UINT32_MAX - told + tnow;
      }

      told = tnow;

      if (tcnt >= ticks) break;
    }
  }
}

#endif

/*********************************************END OF FILE**********************/
