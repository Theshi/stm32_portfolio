/**
  ******************************************************************************
  * @file    Project/STM32F10x_StdPeriph_Template/stm32f10x_it.c 
  * @author  MCD Application Team
  * @version V3.5.0
  * @date    08-April-2011
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and 
  *          peripherals interrupt service routine.
  ******************************************************************************
  * @attention
  *
  * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
  * TIME. AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
  * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * <h2><center>&copy; COPYRIGHT 2011 STMicroelectronics</center></h2>
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f10x_it.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_usart.h"
#include "bsp_esp8266.h"
/** @addtogroup STM32F10x_StdPeriph_Template
  * @{
  */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/******************************************************************************/
/*            Cortex-M3 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief  This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
  /* Go to infinite loop when Hard Fault exception occurs */
	printf("HardFault");
  while (1)
  {
  }
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  /* Go to infinite loop when Memory Manage exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  /* Go to infinite loop when Bus Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  /* Go to infinite loop when Usage Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
//void SVC_Handler(void)
//{
//}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
//void PendSV_Handler(void)
//{
//}

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
extern void xPortSysTickHandler( void );
void SysTick_Handler(void)
{	
#ifdef INCLUDE_xTaskGetSchedulerState
		//�ж�ϵͳ�Ƿ��ȶ�����
	if(xTaskGetSchedulerState()!=taskSCHEDULER_NOT_STARTED){
#endif
		
	xPortSysTickHandler();
		
#ifdef INCLUDE_xTaskGetSchedulerState
	}
#endif
}

/******************************************************************************/
/*                 STM32F10x Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f10x_xx.s).                                            */
/******************************************************************************/

/**
  * @brief  This function handles PPP interrupt request.
  * @param  None
  * @retval None
  */
/*void PPP_IRQHandler(void)
{
}*/

/* ---- USART1 (DEBUG) 帧接收中断 ---- */
void DEBUG_USART_IRQHandler(void)
{
    uint8_t ucCh;

    if (USART_GetITStatus(DEBUG_USARTx, USART_IT_RXNE) != RESET)
    {
        ucCh = USART_ReceiveData(DEBUG_USARTx);
        if (strUSART_Fram_Record.InfBit.FramLength < (RX_BUF_MAX_LEN - 1))
            strUSART_Fram_Record.Data_RX_BUF[
                strUSART_Fram_Record.InfBit.FramLength++] = ucCh;
    }

    if (USART_GetITStatus(DEBUG_USARTx, USART_IT_IDLE) == SET)
    {
        strUSART_Fram_Record.InfBit.FramFinishFlag = 1;
        ucCh = USART_ReceiveData(DEBUG_USARTx);
    }
}

/* ---- USART3 (ESP8266) 帧接收中断 ---- */
void USART3_IRQHandler(void)
{
    uint8_t ucCh;

    if (USART_GetITStatus(macESP8266_USARTx, USART_IT_RXNE) != RESET)
    {
        ucCh = USART_ReceiveData(macESP8266_USARTx);
        if (strEsp8266_Fram_Record.InfBit.FramLength < (RX_BUF_MAX_LEN - 1))
            strEsp8266_Fram_Record.Data_RX_BUF[
                strEsp8266_Fram_Record.InfBit.FramLength++] = ucCh;
    }

    if (USART_GetITStatus(macESP8266_USARTx, USART_IT_IDLE) == SET)
    {
        strEsp8266_Fram_Record.InfBit.FramFinishFlag = 1;
        ucCh = USART_ReceiveData(macESP8266_USARTx);
    }
}

/**
  * @}
  */


/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
