#ifndef FLOW_METER_H
#define FLOW_METER_H

#include <stdint.h>

/*
 * Portable flow-meter transport driver.
 *
 * This layer owns half-duplex RS485 direction sequencing and complete-frame
 * transport, but intentionally does not assume a meter protocol, Modbus
 * address/register map, CRC policy, or board-specific DE//RE polarity.
 */

typedef enum
{
    FLOW_METER_OK = 0,
    FLOW_METER_ERR_INVALID_ARG = -1,
    FLOW_METER_ERR_NOT_CONFIGURED = -2,
    FLOW_METER_ERR_IO = -3,
    FLOW_METER_ERR_BUFFER_TOO_SMALL = -4
} FlowMeter_Result_T;

typedef struct
{
    uint8_t tx_enable_level;
    uint8_t tx_disable_level;
    uint8_t rx_enable_level;
    uint8_t rx_disable_level;
} FlowMeter_Rs485Config_T;

typedef struct
{
    uint32_t tx_frames;
    uint32_t tx_bytes;
    uint32_t tx_errors;
    uint32_t rx_frames;
    uint32_t rx_bytes;
    uint32_t rx_buffer_too_small;
    uint32_t rx_transport_errors;
} FlowMeter_Stats_T;

void FlowMeter_Init(void);

/*
 * Configure raw MCU output levels used for half-duplex RS485 sequencing.
 * Calling this function also places the interface into idle receive mode:
 * TX disabled, RX enabled. All fields must be 0 or 1.
 */
int FlowMeter_ConfigureRs485(const FlowMeter_Rs485Config_T *config);
uint8_t FlowMeter_IsRs485Configured(void);

/* Explicit power control; FlowMeter_Init() does not power the meter on. */
void FlowMeter_SetPower(uint8_t enable);
uint8_t FlowMeter_IsPowerEnabled(void);

/*
 * Blocking transport TX. The platform write must return only after the last
 * UART bit has left the peripheral so the driver can safely restore RX mode.
 * Returns transmitted byte count (>0) or a negative FlowMeter_Result_T.
 */
int FlowMeter_SendFrame(const uint8_t *data, uint16_t length);

/*
 * Read one complete frame already delimited by the platform RX layer.
 * Returns frame length (>0), 0 when no frame is ready, or a negative
 * FlowMeter_Result_T.
 */
int FlowMeter_ReadFrame(uint8_t *data, uint16_t capacity);

uint8_t FlowMeter_ReadDirection(void);
uint8_t FlowMeter_ReadInsertDetect(void);

void FlowMeter_GetStats(FlowMeter_Stats_T *stats);
void FlowMeter_ResetStats(void);

#endif /* FLOW_METER_H */
