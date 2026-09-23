#ifndef FLOW_METER_APP_H
#define FLOW_METER_APP_H

#include <stdint.h>

typedef enum
{
    FLOW_METER_APP_OK = 0,
    FLOW_METER_APP_ERR_INVALID_ARG = -1,
    FLOW_METER_APP_ERR_DISABLED = -2,
    FLOW_METER_APP_ERR_DRIVER = -3,
    FLOW_METER_APP_ERR_SERVICE = -4
} FlowMeterApp_Result_T;

typedef enum
{
    FLOW_METER_APP_STATE_UNINITIALIZED = 0,
    FLOW_METER_APP_STATE_DISABLED,
    FLOW_METER_APP_STATE_STOPPED,
    FLOW_METER_APP_STATE_POWER_SETTLE,
    FLOW_METER_APP_STATE_RUNNING,
    FLOW_METER_APP_STATE_FAULT
} FlowMeterApp_State_T;

typedef enum
{
    FLOW_METER_APP_FAULT_NONE = 0,
    FLOW_METER_APP_FAULT_INVALID_CONFIG,
    FLOW_METER_APP_FAULT_RS485_CONFIG,
    FLOW_METER_APP_FAULT_SERVICE_INIT,
    FLOW_METER_APP_FAULT_SERVICE_START
} FlowMeterApp_Fault_T;

typedef struct
{
    uint8_t enabled;

    /* Raw board levels. These must be explicitly confirmed before enable=1. */
    uint8_t rs485_tx_enable_level;
    uint8_t rs485_tx_disable_level;
    uint8_t rs485_rx_enable_level;
    uint8_t rs485_rx_disable_level;
    uint8_t inflow_level;
    uint8_t insert_present_level;

    uint8_t slave_address;
    uint8_t max_retries;
    uint32_t power_settle_ms;
    uint32_t poll_interval_ms;
    uint32_t response_timeout_ms;
} FlowMeterApp_Config_T;

typedef struct
{
    FlowMeterApp_State_T state;
    FlowMeterApp_Fault_T fault;
    uint8_t enabled;
    uint8_t power_enabled;
    uint8_t insert_detect_level;
    uint8_t meter_present;
    uint32_t power_on_ms;
} FlowMeterApp_Status_T;

/*
 * Initialize the application-side bring-up controller.
 *
 * When config->enabled == 0, no meter GPIO is changed and the controller
 * enters DISABLED. When enabled, all raw polarity fields must be 0/1; this
 * prevents accidental RS485/power activation while board polarity is unknown.
 */
int FlowMeterApp_Init(const FlowMeterApp_Config_T *config);

/*
 * Power the meter and enter a non-blocking power-settle state. The Modbus
 * service starts later from FlowMeterApp_Process() after power_settle_ms.
 */
int FlowMeterApp_Start(void);

/* Stop polling and remove meter power. Safe to call repeatedly. */
void FlowMeterApp_Stop(void);

/* Call from the foreground main loop. Never blocks for the power-settle delay. */
void FlowMeterApp_Process(void);

FlowMeterApp_State_T FlowMeterApp_GetState(void);
FlowMeterApp_Fault_T FlowMeterApp_GetFault(void);
void FlowMeterApp_GetStatus(FlowMeterApp_Status_T *status);

#endif /* FLOW_METER_APP_H */
