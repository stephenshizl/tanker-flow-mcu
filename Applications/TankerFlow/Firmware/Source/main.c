#include "main.h"
#include "apm32f0xx.h"
#include "bsp_debug_uart.h"
#include "bsp_peripherals.h"
#include "bsp_tick.h"

int main(void)
{
    uint32_t heartbeat_start_ms;

    SystemClockConfig();
    BSP_Tick_Init();
    BSP_DebugUart_Init();

    BSP_DebugUart_Write("\r\n[BOOT] " APP_NAME " " APP_VERSION "\r\n");
    BSP_DebugUart_Write("[BSP ] APM32F030xC clock=48MHz tick=1ms\r\n");
    BSP_DebugUart_Write("[UART] USART1 debug  PA9/PA10   115200 8N1 IRQ\r\n");

    BSP_Peripherals_Init();

    BSP_DebugUart_Write("[UART] USART2 BT     PA2/PA3     115200 8N1 IRQ\r\n");
    BSP_DebugUart_Write("[UART] USART3 4G     PB10/PB11   115200 8N1 DMA+IDLE\r\n");
    BSP_DebugUart_Write("[UART] USART4 GNSS   PA0/PA1       9600 8N1 DMA+IDLE\r\n");
    BSP_DebugUart_Write("[UART] USART5 FLOW   PB3/PB4       9600 8E1 IRQ+TMR16\r\n");
    BSP_DebugUart_Write("[I2C ] I2C1 PB8/PB9 100kHz, SGM41511 raw register API\r\n");
    BSP_DebugUart_Write("[ADC ] ADC_IN6..9 raw 12-bit channels initialized\r\n");
    BSP_DebugUart_Write("[GPIO] external power/control pins initialized to safe states\r\n");
    BSP_DebugUart_Write("[PASS] phase-2 peripheral BSP initialized\r\n");

    heartbeat_start_ms = BSP_Tick_GetMs();
    while (1)
    {
        if (BSP_Tick_Elapsed(heartbeat_start_ms, APP_HEARTBEAT_PERIOD_MS) != 0U)
        {
            heartbeat_start_ms = BSP_Tick_GetMs();
            BSP_DebugUart_Write("[ALIVE] 1s\r\n");
        }
    }
}
