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

/* Standardized complete-frame RX results used by all platform backends. */
#define PLATFORM_PORT_FLOW_NO_FRAME              (0)
#define PLATFORM_PORT_FLOW_ERR_BUFFER_TOO_SMALL (-1)
#define PLATFORM_PORT_FLOW_ERR_IO               (-2)

/*
 * Flow-meter transport primitives.
 *
 * These APIs intentionally expose raw board control levels instead of
 * assuming RS485 DE//RE polarity in portable code. Protocol/device logic
 * can be layered above these primitives once the meter protocol is frozen.
 */
int PlatformPort_FlowReadFrame(uint8_t *data, uint16_t capacity);
/* Must return only after the final UART bit has left the transmitter. */
uint16_t PlatformPort_FlowWrite(const uint8_t *data, uint16_t length);
void PlatformPort_FlowSetPower(uint8_t enable);
void PlatformPort_FlowTxEnablePinWrite(uint8_t high);
void PlatformPort_FlowRxEnablePinWrite(uint8_t high);
uint8_t PlatformPort_FlowDirectionRead(void);
uint8_t PlatformPort_FlowInsertDetectRead(void);

#endif /* PLATFORM_PORT_H */
