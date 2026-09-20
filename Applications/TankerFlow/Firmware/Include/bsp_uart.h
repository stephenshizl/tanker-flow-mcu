#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

typedef enum
{
    BSP_UART_DEBUG = 1,
    BSP_UART_BLUETOOTH = 2,
    BSP_UART_4G = 3,
    BSP_UART_GNSS = 4,
    BSP_UART_FLOW = 5
} BSP_UartPort_T;

typedef struct
{
    uint32_t rx_bytes;
    uint32_t idle_events;
    uint32_t dma_full_events;
    uint32_t ring_overflow;
} BSP_UartStats_T;

void BSP_Uart_Init(void);
uint16_t BSP_Uart_Available(BSP_UartPort_T port);
uint16_t BSP_Uart_Read(BSP_UartPort_T port, uint8_t *data, uint16_t max_length);
void BSP_Uart_Write(BSP_UartPort_T port, const uint8_t *data, uint16_t length);
uint32_t BSP_Uart_OverflowCount(BSP_UartPort_T port);
void BSP_Uart_GetStats(BSP_UartPort_T port, BSP_UartStats_T *stats);

/* USART5/Modbus RTU frame API. Frame boundary comes from TMR16 ~3.5 chars. */
int BSP_Flow_ReadFrame(uint8_t *data, uint16_t capacity);

/* IRQ dispatch entry points called from apm32f0xx_int.c. */
void BSP_Uart_Usart1IRQHandler(void);
void BSP_Uart_Usart2IRQHandler(void);
void BSP_Uart_Usart3To6IRQHandler(void);
void BSP_Uart_DmaCh2To3IRQHandler(void);
void BSP_Uart_DmaCh4To5IRQHandler(void);
void BSP_Uart_Tmr16IRQHandler(void);

#endif /* BSP_UART_H */
