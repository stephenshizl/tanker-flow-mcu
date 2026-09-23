#ifndef FLOW_SESSION_H
#define FLOW_SESSION_H

#include <stdint.h>

typedef enum
{
    FLOW_SESSION_OK = 0,
    FLOW_SESSION_ERR_INVALID_ARG = -1,
    FLOW_SESSION_ERR_NOT_READY = -2,
    FLOW_SESSION_ERR_BUSY = -3
} FlowSession_Result_T;

typedef enum
{
    FLOW_SESSION_DIRECTION_UNKNOWN = 0,
    FLOW_SESSION_DIRECTION_INFLOW,
    FLOW_SESSION_DIRECTION_OUTFLOW
} FlowSession_Direction_T;

typedef enum
{
    FLOW_SESSION_DATA_UNAVAILABLE = 0,
    FLOW_SESSION_DATA_STALE,
    FLOW_SESSION_DATA_DIRECTION_BOUNDARY,
    FLOW_SESSION_DATA_VALID
} FlowSession_DataState_T;

typedef struct
{
    /* Raw ZELZ realtime values retained for diagnostics/server reporting. */
    uint32_t meter_batch_milli_l;
    uint32_t meter_total_milli_m3;
    uint32_t instant_milli_m3_per_h;

    /* Direction-aware business session totals, all volume values in 0.001 L. */
    uint64_t inflow_milli_l;
    uint64_t outflow_milli_l;
    uint64_t unclassified_milli_l;
    uint64_t accounted_throughput_milli_l;
    int64_t net_milli_l;

    /* Coarse absolute-throughput cross-check derived from meter register 0x0002. */
    uint64_t meter_absolute_milli_l;

    uint32_t last_update_ms;
    uint32_t reset_sequence;
    uint32_t reset_effective_ms;
    uint32_t direction_event_sequence;
    uint32_t last_direction_event_ms;

    FlowSession_Direction_T direction;
    FlowSession_DataState_T data_state;

    /* Raw GPIO levels remain explicit until board polarity is frozen. */
    uint8_t direction_level;
    uint8_t insert_detect_level;

    uint8_t service_running;
    uint8_t meter_valid;
    uint8_t fresh;
    uint8_t reset_pending;
    uint8_t direction_boundary_pending;
} FlowSession_Status_T;

/*
 * Build an upper-layer snapshot suitable for future 4G/Bluetooth reporting.
 * fresh_max_age_ms is the maximum acceptable age of the latest valid meter
 * sample. Passing 0 intentionally marks otherwise-valid data as stale.
 */
int FlowSession_GetStatus(FlowSession_Status_T *status, uint32_t fresh_max_age_ms);

/*
 * Request a logical business-session clear without modifying meter-side
 * cumulative registers. On success, expected_reset_sequence identifies the
 * reset completion that an asynchronous server/Bluetooth command can wait for.
 */
int FlowSession_RequestReset(uint32_t *expected_reset_sequence);

/* Wrap-safe completion check for a previously accepted reset ticket. */
uint8_t FlowSession_IsResetComplete(uint32_t expected_reset_sequence);

#endif /* FLOW_SESSION_H */
