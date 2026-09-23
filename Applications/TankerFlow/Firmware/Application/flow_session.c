#include "flow_session.h"

#include "flow_meter_service.h"

static FlowSession_Direction_T FlowSession_MapDirection(FlowMeterService_Direction_T direction)
{
    if (direction == FLOW_METER_SERVICE_DIRECTION_INFLOW)
    {
        return FLOW_SESSION_DIRECTION_INFLOW;
    }
    if (direction == FLOW_METER_SERVICE_DIRECTION_OUTFLOW)
    {
        return FLOW_SESSION_DIRECTION_OUTFLOW;
    }
    return FLOW_SESSION_DIRECTION_UNKNOWN;
}

int FlowSession_GetStatus(FlowSession_Status_T *status, uint32_t fresh_max_age_ms)
{
    FlowMeterService_Snapshot_T snapshot;
    uint8_t fresh;

    if (status == 0)
    {
        return FLOW_SESSION_ERR_INVALID_ARG;
    }

    FlowMeterService_GetSnapshot(&snapshot);
    fresh = FlowMeterService_IsFresh(fresh_max_age_ms);

    status->meter_batch_milli_l = snapshot.meter.batch_total_milli_l;
    status->meter_total_milli_m3 = snapshot.meter.total_milli_m3;
    status->instant_milli_m3_per_h = snapshot.meter.instant_milli_m3_per_h;

    status->inflow_milli_l = snapshot.session_inflow_milli_l;
    status->outflow_milli_l = snapshot.session_outflow_milli_l;
    status->unclassified_milli_l = snapshot.session_unclassified_milli_l;
    status->accounted_throughput_milli_l =
        snapshot.session_inflow_milli_l +
        snapshot.session_outflow_milli_l +
        snapshot.session_unclassified_milli_l;
    status->net_milli_l = snapshot.session_net_milli_l;
    status->meter_absolute_milli_l = snapshot.session_meter_absolute_milli_l;

    status->last_update_ms = snapshot.last_update_ms;
    status->reset_sequence = snapshot.reset_sequence;
    status->reset_effective_ms = snapshot.reset_effective_ms;
    status->direction_event_sequence = snapshot.direction_event_sequence;
    status->last_direction_event_ms = snapshot.last_direction_event_ms;

    status->direction = FlowSession_MapDirection(snapshot.direction);
    status->direction_level = snapshot.direction_level;
    status->insert_detect_level = snapshot.inserted;
    status->service_running = FlowMeterService_IsRunning();
    status->meter_valid = snapshot.valid;
    status->fresh = fresh;
    status->reset_pending = snapshot.reset_pending;
    status->direction_boundary_pending = snapshot.direction_boundary_pending;

    if (snapshot.valid == 0U)
    {
        status->data_state = FLOW_SESSION_DATA_UNAVAILABLE;
    }
    else if ((status->service_running == 0U) || (fresh == 0U))
    {
        status->data_state = FLOW_SESSION_DATA_STALE;
    }
    else if (snapshot.direction_boundary_pending != 0U)
    {
        status->data_state = FLOW_SESSION_DATA_DIRECTION_BOUNDARY;
    }
    else
    {
        status->data_state = FLOW_SESSION_DATA_VALID;
    }

    return FLOW_SESSION_OK;
}

int FlowSession_RequestReset(uint32_t *expected_reset_sequence)
{
    FlowMeterService_Snapshot_T snapshot;

    if (FlowMeterService_IsRunning() == 0U)
    {
        return FLOW_SESSION_ERR_NOT_READY;
    }

    FlowMeterService_GetSnapshot(&snapshot);
    if (snapshot.reset_pending != 0U)
    {
        return FLOW_SESSION_ERR_BUSY;
    }

    if (expected_reset_sequence != 0)
    {
        *expected_reset_sequence = snapshot.reset_sequence + 1U;
    }

    FlowMeterService_RequestSessionReset();
    return FLOW_SESSION_OK;
}

uint8_t FlowSession_IsResetComplete(uint32_t expected_reset_sequence)
{
    FlowMeterService_Snapshot_T snapshot;

    FlowMeterService_GetSnapshot(&snapshot);
    return (uint8_t)(((int32_t)(snapshot.reset_sequence - expected_reset_sequence)) >= 0);
}
