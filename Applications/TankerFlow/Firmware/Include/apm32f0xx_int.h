#ifndef APM32F0XX_INT_H
#define APM32F0XX_INT_H

void NMI_Handler(void);
void HardFault_Handler(void);
void SVC_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

#endif
