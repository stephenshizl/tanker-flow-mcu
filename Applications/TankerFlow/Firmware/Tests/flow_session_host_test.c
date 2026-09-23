#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "flow_session.h"
#include "flow_meter_service.h"

static FlowMeterService_Snapshot_T g_snapshot;
static uint8_t g_running;
static uint8_t g_fresh;
static uint32_t g_reset_requests;

uint8_t FlowMeterService_IsRunning(void)
{
    return g_running;
}

void FlowMeterService_GetSnapshot(FlowMeterService_Snapshot_T *snapshot)
{
    if (snapshot != 0)
    {
        *snapshot = g_snapshot;
    }
}

uint8_t FlowMeterService_IsFresh(uint32_t max_age_ms)
{
    if (max_age_ms == 0U)
    {
        return 0U;
    }
    return g_fresh;
}

void FlowMeterService_RequestSessionReset(void)
{
    g_reset_requests++;
    g_snapshot.reset_pending = 1U;
}

static int Expect(int condition, const char *message)
{
    if (!condition)
    {
        printf("FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

int main(void)
{
    FlowSession_Status_T status;
    uint32_t expected_sequence = 0U;
    int ok = 1;

    memset(&g_snapshot, 0, sizeof(g_snapshot));
    g_running = 0U;
    g_fresh = 0U;
    g_reset_requests = 0U;

    ok &= Expect(FlowSession_GetStatus(0, 1000U) == FLOW_SESSION_ERR_INVALID_ARG,
                 "null status rejected");

    memset(&status, 0xA5, sizeof(status));
    ok &= Expect(FlowSession_GetStatus(&status, 1000U) == FLOW_SESSION_OK,
                 "empty status readable");
    ok &= Expect(status.data_state == FLOW_SESSION_DATA_UNAVAILABLE,
                 "invalid meter data is unavailable");
    ok &= Expect(status.service_running == 0U, "stopped service reported");

    g_running = 1U;
    g_fresh = 1U;
    g_snapshot.meter.batch_total_milli_l = 123456U;
    g_snapshot.meter.total_milli_m3 = 789U;
    g_snapshot.meter.instant_milli_m3_per_h = 3210U;
    g_snapshot.session_inflow_milli_l = 50000U;
    g_snapshot.session_outflow_milli_l = 20000U;
    g_snapshot.session_unclassified_milli_l = 3000U;
    g_snapshot.session_net_milli_l = 30000;
    g_snapshot.session_meter_absolute_milli_l = 73000U;
    g_snapshot.last_update_ms = 1000U;
    g_snapshot.reset_sequence = 7U;
    g_snapshot.reset_effective_ms = 900U;
    g_snapshot.direction_event_sequence = 11U;
    g_snapshot.last_direction_event_ms = 950U;
    g_snapshot.direction = FLOW_METER_SERVICE_DIRECTION_INFLOW;
    g_snapshot.direction_level = 1U;
    g_snapshot.inserted = 0U;
    g_snapshot.valid = 1U;
    g_snapshot.reset_pending = 0U;
    g_snapshot.direction_boundary_pending = 0U;

    ok &= Expect(FlowSession_GetStatus(&status, 2000U) == FLOW_SESSION_OK,
                 "valid status readable");
    ok &= Expect(status.data_state == FLOW_SESSION_DATA_VALID,
                 "fresh stable data is valid");
    ok &= Expect(status.direction == FLOW_SESSION_DIRECTION_INFLOW,
                 "direction mapped to business API");
    ok &= Expect(status.inflow_milli_l == 50000U, "inflow mapped");
    ok &= Expect(status.outflow_milli_l == 20000U, "outflow mapped");
    ok &= Expect(status.unclassified_milli_l == 3000U, "unclassified mapped");
    ok &= Expect(status.accounted_throughput_milli_l == 73000U,
                 "accounted throughput sums all unsigned volume");
    ok &= Expect(status.net_milli_l == 30000, "net mapped independently");
    ok &= Expect(status.meter_absolute_milli_l == 73000U,
                 "meter absolute cross-check mapped");
    ok &= Expect(status.insert_detect_level == 0U,
                 "insert detect remains raw until polarity frozen");

    g_running = 0U;
    ok &= Expect(FlowSession_GetStatus(&status, 2000U) == FLOW_SESSION_OK,
                 "stopped-but-valid status readable");
    ok &= Expect(status.data_state == FLOW_SESSION_DATA_STALE,
                 "stopped service never reports live valid data");
    g_running = 1U;

    g_snapshot.direction_boundary_pending = 1U;
    ok &= Expect(FlowSession_GetStatus(&status, 2000U) == FLOW_SESSION_OK,
                 "boundary status readable");
    ok &= Expect(status.data_state == FLOW_SESSION_DATA_DIRECTION_BOUNDARY,
                 "pending direction boundary is explicit");

    g_fresh = 0U;
    ok &= Expect(FlowSession_GetStatus(&status, 2000U) == FLOW_SESSION_OK,
                 "stale status readable");
    ok &= Expect(status.data_state == FLOW_SESSION_DATA_STALE,
                 "stale takes precedence over boundary state");

    g_fresh = 1U;
    g_snapshot.direction_boundary_pending = 0U;
    g_running = 0U;
    ok &= Expect(FlowSession_RequestReset(&expected_sequence) == FLOW_SESSION_ERR_NOT_READY,
                 "reset rejected while flow service stopped");
    ok &= Expect(g_reset_requests == 0U, "stopped reset does not reach driver service");

    g_running = 1U;
    ok &= Expect(FlowSession_RequestReset(&expected_sequence) == FLOW_SESSION_OK,
                 "running reset accepted");
    ok &= Expect(expected_sequence == 8U, "reset ticket predicts next completion sequence");
    ok &= Expect(g_reset_requests == 1U, "accepted reset reaches driver service once");
    ok &= Expect(FlowSession_IsResetComplete(expected_sequence) == 0U,
                 "accepted reset is not complete before new baseline");
    ok &= Expect(FlowSession_RequestReset(0) == FLOW_SESSION_ERR_BUSY,
                 "second reset rejected while first is pending");

    g_snapshot.reset_pending = 0U;
    g_snapshot.reset_sequence = expected_sequence;
    g_snapshot.reset_effective_ms = 1234U;
    ok &= Expect(FlowSession_IsResetComplete(expected_sequence) != 0U,
                 "reset ticket completes after driver baseline update");
    ok &= Expect(FlowSession_GetStatus(&status, 2000U) == FLOW_SESSION_OK,
                 "completed reset status readable");
    ok &= Expect(status.reset_sequence == expected_sequence,
                 "completed sequence exposed to upper protocol");
    ok &= Expect(status.reset_effective_ms == 1234U,
                 "reset effective time exposed to upper protocol");

    /* Sequence comparison remains correct across uint32_t wrap. */
    g_snapshot.reset_sequence = 0U;
    ok &= Expect(FlowSession_IsResetComplete(0xFFFFFFFFU) != 0U,
                 "reset completion comparison is wrap-safe");

    if (!ok)
    {
        return 1;
    }

    printf("Flow session host tests: PASS\n");
    return 0;
}
