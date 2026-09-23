#include "apm32f0xx_int.h"

#include "bsp_gpio.h"
#include "bsp_tick.h"
#include "bsp_uart.h"

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    while (1)
    {
    }
}

void SVC_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
    BSP_Tick_Increment();
}

void EINT4_15_IRQHandler(void)
{
    BSP_ChargerInterrupt_IRQHandler();
    BSP_FlowDirectionInterrupt_IRQHandler();
}

void DMA1_CH2_3_IRQHandler(void)
{
    BSP_Uart_DmaCh2To3IRQHandler();
}

void DMA1_CH4_5_IRQHandler(void)
{
    BSP_Uart_DmaCh4To5IRQHandler();
}

void TMR16_IRQHandler(void)
{
    BSP_Uart_Tmr16IRQHandler();
}

void USART1_IRQHandler(void)
{
    BSP_Uart_Usart1IRQHandler();
}

void USART2_IRQHandler(void)
{
    BSP_Uart_Usart2IRQHandler();
}

void USART3_6_IRQHandler(void)
{
    BSP_Uart_Usart3To6IRQHandler();
}
