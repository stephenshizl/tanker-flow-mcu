#ifndef FLOW_METER_SERVICE_H
#define FLOW_METER_SERVICE_H

#include <stdint.h>

#include "zelz_flow_meter.h"

typedef enum
{
    FLOW_METER_SERVICE_OK = 0,
    FLOW_METER_SERVICE_ERR_INVALID_ARG = -1,
    FLOW_METER_SERVICE_ERR_NOT_READY = -2,
    FLOW_METER_SERVICE_ERR_IO = -3
} FlowMeterService_Result_T;

typedef enum
{
    FLOW_METER_SERVICE_STOPPED = 0,
    FLOW_METER_SERVICE_IDLE,
    FLOW_METER_SERVICE_WAIT_RESPONSE
} FlowMeterService_State_T;

typedef enum
{
    FLOW_METER_SERVICE_DIRECTION_UNKNOWN = 0,
    FLOW_METER_SERVICE_DIRECTION_INFLOW,
    FLOW_METER_SERVICE_DIRECTION_OUTFLOW
} FlowMeterService_Direction_T;

typedef struct
{
    uint8_t slave_address;
    uint8_t inflow_level;
    uint32_t poll_interval_ms;
    uint32_t response_timeout_ms;
    uint8_t max_retries;
} FlowMeterService_Config_T;

typedef struct
{
    ZelzFlow_Realtime_T meter;

    /* Direction-aware software session totals, all in 0.001 L. */
    uint64_t session_inflow_milli_l;
    uint64_t session_outflow_milli_l;
    uint64_t session_unclassified_milli_l;
    int64_t session_net_milli_l;

    /*
     * Coarse absolute-throughput cross-check derived from meter register 0x0002.
     * ZELZ defines each raw unit as 0.001 m^3 = 1 L = 1000 mL.
     * This is diagnostic only; direction accounting uses the higher-resolution
     * 0x0000 counter (0.001 L per raw unit).
     */
    uint64_t session_meter_absolute_milli_l;

    uint32_t last_update_ms;
    uint32_t reset_sequence;
    uint32_t reset_effective_ms;
    uint32_t direction_event_sequence;
    uint32_t last_direction_event_ms;
    FlowMeterService_Direction_T direction;
    uint8_t direction_level;
    uint8_t inserted;
    uint8_t valid;
    uint8_t reset_pending;
    uint8_t direction_boundary_pending;
} FlowMeterService_Snapshot_T;

typedef struct
{
    uint32_t requests;
    uint32_t responses_ok;
    uint32_t timeouts;
    uint32_t retries;
    uint32_t transport_errors;
    uint32_t protocol_errors;
    uint32_t exception_responses;
    uint32_t meter_rebases;
    uint32_t meter_total_rebases;
    uint32_t direction_changes;
    uint32_t direction_interrupts;
    uint32_t direction_events_missed;
    uint32_t ambiguous_direction_intervals;
    uint32_t session_resets;
} FlowMeterService_Stats_T;

int FlowMeterService_Init(const FlowMeterService_Config_T *config);
int FlowMeterService_Start(void);
void FlowMeterService_Stop(void);
void FlowMeterService_Process(void);

FlowMeterService_State_T FlowMeterService_GetState(void);
uint8_t FlowMeterService_IsRunning(void);

/*
 * Request a software session-total reset. The reset becomes effective on the
 * next valid meter sample, which becomes the new high-resolution and coarse
 * meter baselines. The ZELZ protocol supplied for this project does not define
 * a supported command for clearing 0x0000/0x0002, so the service deliberately
 * does not write or erase meter-side accumulated counters.
 *
 * A server command should acknowledge completion only after reset_pending
 * clears and reset_sequence increments.
 */
void FlowMeterService_RequestSessionReset(void);
uint8_t FlowMeterService_IsSessionResetPending(void);

void FlowMeterService_GetSnapshot(FlowMeterService_Snapshot_T *snapshot);
void FlowMeterService_GetStats(FlowMeterService_Stats_T *stats);
void FlowMeterService_ResetStats(void);

/* Freshness is based on the last valid ZELZ realtime response. */
uint8_t FlowMeterService_IsFresh(uint32_t max_age_ms);

#endif /* FLOW_METER_SERVICE_H */
