#ifndef APM32F0XX_INT_H
#define APM32F0XX_INT_H

void NMI_Handler(void);
void HardFault_Handler(void);
void SVC_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

void EINT4_15_IRQHandler(void);
void DMA1_CH2_3_IRQHandler(void);
void DMA1_CH4_5_IRQHandler(void);
void TMR16_IRQHandler(void);
void USART1_IRQHandler(void);
void USART2_IRQHandler(void);
void USART3_6_IRQHandler(void);

#endif /* APM32F0XX_INT_H */
