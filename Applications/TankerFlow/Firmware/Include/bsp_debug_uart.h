#ifndef BSP_DEBUG_UART_H
#define BSP_DEBUG_UART_H

#include <stdint.h>

void BSP_DebugUart_Init(void);
void BSP_DebugUart_WriteByte(uint8_t data);
void BSP_DebugUart_Write(const char *text);
void BSP_DebugUart_WriteUInt32(uint32_t value);
void BSP_DebugUart_WriteHex8(uint8_t value);

#endif /* BSP_DEBUG_UART_H */
