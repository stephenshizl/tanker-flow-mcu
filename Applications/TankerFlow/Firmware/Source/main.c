#include "main.h"

#include "apm32f0xx.h"
#include "bsp_debug_uart.h"
#include "bsp_tick.h"

int main(void)
{
    uint32_t heartbeat_start_ms;

    SystemClockConfig();
    BSP_Tick_Init();
    BSP_DebugUart_Init();

    BSP_DebugUart_Write("\r\n[BOOT] " APP_NAME " " APP_VERSION "\r\n");
    BSP_DebugUart_Write("[BSP ] APM32F030xC clock=48MHz tick=1ms\r\n");
    BSP_DebugUart_Write("[UART] USART1 PA9/PA10 115200 8N1\r\n");
    BSP_DebugUart_Write("[PASS] phase-1 minimal bring-up started\r\n");

    heartbeat_start_ms = BSP_Tick_GetMs();

    while (1)
    {
        if (BSP_Tick_Elapsed(heartbeat_start_ms, APP_HEARTBEAT_PERIOD_MS) != 0U)
        {
            heartbeat_start_ms += APP_HEARTBEAT_PERIOD_MS;
            BSP_DebugUart_Write("[ALIVE] 1s\r\n");
        }
    }
}
