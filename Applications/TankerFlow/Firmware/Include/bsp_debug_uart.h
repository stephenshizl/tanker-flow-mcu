#ifndef BSP_DEBUG_UART_H
#define BSP_DEBUG_UART_H

#include <stdint.h>

void BSP_DebugUart_Init(void);
void BSP_DebugUart_WriteByte(uint8_t data);
void BSP_DebugUart_Write(const char *text);

#endif
