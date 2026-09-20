#include "bsp_tick.h"

#include "apm32f0xx.h"

static volatile uint32_t s_tick_ms;

void BSP_Tick_Init(void)
{
    s_tick_ms = 0U;
    (void)SysTick_Config(SystemCoreClock / 1000U);
}

void BSP_Tick_Increment(void)
{
    ++s_tick_ms;
}

uint32_t BSP_Tick_GetMs(void)
{
    return s_tick_ms;
}

uint8_t BSP_Tick_Elapsed(uint32_t start_ms, uint32_t period_ms)
{
    return ((uint32_t)(BSP_Tick_GetMs() - start_ms) >= period_ms) ? 1U : 0U;
}
