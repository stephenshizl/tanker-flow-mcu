#ifndef BSP_TICK_H
#define BSP_TICK_H

#include <stdint.h>

void BSP_Tick_Init(void);
void BSP_Tick_Increment(void);
uint32_t BSP_Tick_GetMs(void);
uint8_t BSP_Tick_Elapsed(uint32_t start_ms, uint32_t period_ms);

#endif
