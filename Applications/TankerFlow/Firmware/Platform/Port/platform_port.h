#ifndef PLATFORM_PORT_H
#define PLATFORM_PORT_H

#include <stdint.h>

/*
 * Portable hardware contract used by device drivers.
 *
 * Device/protocol code must depend on this interface instead of MCU-specific
 * BSP headers. A platform adapter implements these functions for APM32,
 * STM32, HC32, or host tests. Keep this layer small and allocation-free.
 */

uint32_t PlatformPort_GetMs(void);

uint16_t PlatformPort_ModemRead(uint8_t *data, uint16_t max_length);
uint16_t PlatformPort_ModemWrite(const uint8_t *data, uint16_t length);
void PlatformPort_ModemResetAssert(void);
void PlatformPort_ModemResetRelease(void);
void PlatformPort_ModemPowerKeyAssert(void);
void PlatformPort_ModemPowerKeyRelease(void);

uint16_t PlatformPort_GnssRead(uint8_t *data, uint16_t max_length);

#endif /* PLATFORM_PORT_H */
