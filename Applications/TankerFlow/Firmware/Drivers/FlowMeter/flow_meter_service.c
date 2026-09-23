#include "flow_meter_service.h"

#include "flow_meter.h"
#include "platform_port.h"

#define FLOW_METER_SERVICE_REQUEST_MAX  (8U)
#define FLOW_METER_SERVICE_RESPONSE_MAX (32U)
#define FLOW_METER_TOTAL_RAW_TO_MILLI_L  (1000ULL)

static FlowMeterService_Config_T g_config;
static FlowMeterService_Snapshot_T g_snapshot;
static FlowMeterService_Stats_T g_stats;
static FlowMeterService_State_T g_state;
static uint32_t g_state_started_ms;
static uint32_t g_next_poll_ms;
static uint32_t g_last_meter_batch_milli_l;
static uint32_t g_session_meter_total_base_milli_m3;
static uint32_t g_last_direction_event_sequence;
static FlowMeterService_Direction_T g_last_accounting_direction;
static FlowMeterService_Direction_T g_boundary_direction;
static uint8_t g_initialized;
static uint8_t g_running;
static uint8_t g_retry_count;
static uint8_t g_baseline_valid;
static uint8_t g_meter_total_baseline_valid;
static uint8_t g_direction_boundary_pending;

static uint8_t FlowMeterService_Elapsed(uint32_t now_ms,
                                        uint32_t start_ms,
                                        uint32_t duration_ms)
{
    return (uint8_t)(((uint32_t)(now_ms - start_ms)) >= duration_ms);
}

static uint8_t FlowMeterService_TimeReached(uint32_t now_ms, uint32_t target_ms)
{
    return (uint8_t)(((int32_t)(now_ms - target_ms)) >= 0);
}

static FlowMeterService_Direction_T FlowMeterService_DecodeDirection(uint8_t level)
{
    if (level == g_config.inflow_level)
    {
        return FLOW_METER_SERVICE_DIRECTION_INFLOW;
    }
    return FLOW_METER_SERVICE_DIRECTION_OUTFLOW;
}

static void FlowMeterService_ScheduleNext(uint32_t now_ms)
{
    g_next_poll_ms = now_ms + g_config.poll_interval_ms;
    g_state = FLOW_METER_SERVICE_IDLE;
    g_retry_count = 0U;
}

static int FlowMeterService_SendRequest(uint32_t now_ms)
{
    uint8_t request[FLOW_METER_SERVICE_REQUEST_MAX];
    int request_length;
    int sent;

    request_length = ZelzFlow_BuildReadRealtimeRequest(g_config.slave_address,
                                                       request,
                                                       sizeof(request));
    if (request_length <= 0)
    {
        g_stats.protocol_errors++;
        return FLOW_METER_SERVICE_ERR_IO;
    }

    sent = FlowMeter_SendFrame(request, (uint16_t)request_length);
    if (sent != request_length)
    {
        g_stats.transport_errors++;
        return FLOW_METER_SERVICE_ERR_IO;
    }

    g_stats.requests++;
    g_state_started_ms = now_ms;
    g_state = FLOW_METER_SERVICE_WAIT_RESPONSE;
    return FLOW_METER_SERVICE_OK;
}

static void FlowMeterService_SetMeterTotalBaseline(const ZelzFlow_Realtime_T *realtime)
{
    g_session_meter_total_base_milli_m3 = realtime->total_milli_m3;
    g_meter_total_baseline_valid = 1U;
    g_snapshot.session_meter_absolute_milli_l = 0U;
}

static void FlowMeterService_UpdateMeterAbsolute(const ZelzFlow_Realtime_T *realtime)
{
    uint32_t delta_raw;

    if (g_meter_total_baseline_valid == 0U)
    {
        FlowMeterService_SetMeterTotalBaseline(realtime);
        return;
    }

    if (realtime->total_milli_m3 < g_session_meter_total_base_milli_m3)
    {
        /* Meter-side lifetime/total counter was externally reset or wrapped. */
        FlowMeterService_SetMeterTotalBaseline(realtime);
        g_stats.meter_total_rebases++;
        return;
    }

    delta_raw = realtime->total_milli_m3 - g_session_meter_total_base_milli_m3;
    g_snapshot.session_meter_absolute_milli_l =
        (uint64_t)delta_raw * FLOW_METER_TOTAL_RAW_TO_MILLI_L;
}

static void FlowMeterService_ClearDirectionBoundary(void)
{
    g_direction_boundary_pending = 0U;
    g_snapshot.direction_boundary_pending = 0U;
}

static void FlowMeterService_ApplySessionReset(uint32_t now_ms,
                                               const ZelzFlow_Realtime_T *realtime,
                                               FlowMeterService_Direction_T direction)
{
    g_snapshot.session_inflow_milli_l = 0U;
    g_snapshot.session_outflow_milli_l = 0U;
    g_snapshot.session_unclassified_milli_l = 0U;
    g_snapshot.session_net_milli_l = 0;
    g_snapshot.reset_pending = 0U;
    g_snapshot.reset_sequence++;
    g_snapshot.reset_effective_ms = now_ms;

    g_last_meter_batch_milli_l = realtime->batch_total_milli_l;
    g_last_accounting_direction = direction;
    g_baseline_valid = 1U;
    FlowMeterService_SetMeterTotalBaseline(realtime);
    FlowMeterService_ClearDirectionBoundary();
    g_stats.session_resets++;
}

static void FlowMeterService_AccountAmbiguousDelta(uint32_t delta_milli_l,
                                                    FlowMeterService_Direction_T direction)
{
    g_snapshot.session_unclassified_milli_l += delta_milli_l;
    g_last_accounting_direction = direction;
    FlowMeterService_ClearDirectionBoundary();

    if (delta_milli_l != 0U)
    {
        g_stats.ambiguous_direction_intervals++;
    }
}

static void FlowMeterService_AccountSample(uint32_t now_ms,
                                           const ZelzFlow_Realtime_T *realtime,
                                           FlowMeterService_Direction_T direction)
{
    uint32_t delta_milli_l;

    if (g_snapshot.reset_pending != 0U)
    {
        FlowMeterService_ApplySessionReset(now_ms, realtime, direction);
        return;
    }

    FlowMeterService_UpdateMeterAbsolute(realtime);

    if (g_baseline_valid == 0U)
    {
        g_last_meter_batch_milli_l = realtime->batch_total_milli_l;
        g_last_accounting_direction = direction;
        g_baseline_valid = 1U;
        return;
    }

    if (realtime->batch_total_milli_l < g_last_meter_batch_milli_l)
    {
        /*
         * The meter-side high-resolution batch counter can be reset
         * independently of the MCU. Rebase instead of interpreting a
         * decrease as unsigned wrap volume.
         */
        g_last_meter_batch_milli_l = realtime->batch_total_milli_l;
        g_last_accounting_direction = direction;
        FlowMeterService_ClearDirectionBoundary();
        g_stats.meter_rebases++;
        return;
    }

    delta_milli_l = realtime->batch_total_milli_l - g_last_meter_batch_milli_l;
    g_last_meter_batch_milli_l = realtime->batch_total_milli_l;

    if (g_direction_boundary_pending != 0U)
    {
        /*
         * The GPIO edge gives us an exact direction-change timestamp, but a
         * Modbus read happens afterwards. The cumulative delta between the
         * previous sample and this boundary sample can therefore contain both
         * old- and new-direction flow. Keep it as explicit unclassified volume
         * rather than silently dropping it or assigning an arbitrary sign.
         */
        FlowMeterService_AccountAmbiguousDelta(delta_milli_l, g_boundary_direction);
        return;
    }

    if (direction != g_last_accounting_direction)
    {
        /* Fallback if a platform cannot deliver or missed the direction IRQ. */
        g_stats.direction_changes++;
        FlowMeterService_AccountAmbiguousDelta(delta_milli_l, direction);
        return;
    }

    if (direction == FLOW_METER_SERVICE_DIRECTION_INFLOW)
    {
        g_snapshot.session_inflow_milli_l += delta_milli_l;
    }
    else if (direction == FLOW_METER_SERVICE_DIRECTION_OUTFLOW)
    {
        g_snapshot.session_outflow_milli_l += delta_milli_l;
    }

    g_snapshot.session_net_milli_l =
        (int64_t)g_snapshot.session_inflow_milli_l -
        (int64_t)g_snapshot.session_outflow_milli_l;
}

static void FlowMeterService_HandleDirectionEvent(uint32_t now_ms)
{
    PlatformPort_FlowDirectionEvent_T event;
    FlowMeterService_Direction_T direction;
    uint32_t event_delta;

    if (PlatformPort_FlowDirectionEventConsume(&event) == 0U)
    {
        return;
    }

    direction = FlowMeterService_DecodeDirection(event.level);

    event_delta = (uint32_t)(event.sequence - g_last_direction_event_sequence);
    if (event_delta == 0U)
    {
        event_delta = 1U;
    }
    g_last_direction_event_sequence = event.sequence;

    g_stats.direction_interrupts += event_delta;
    g_stats.direction_changes += event_delta;
    if (event_delta > 1U)
    {
        g_stats.direction_events_missed += event_delta - 1U;
    }

    g_snapshot.direction_event_sequence = event.sequence;
    g_snapshot.last_direction_event_ms = event.timestamp_ms;
    g_snapshot.direction_level = event.level;
    g_snapshot.direction = direction;

    if (g_baseline_valid != 0U)
    {
        /* Any captured edge defines a new accounting boundary. */
        g_boundary_direction = direction;
        g_direction_boundary_pending = 1U;
        g_snapshot.direction_boundary_pending = 1U;
    }

    if ((g_running != 0U) && (g_state == FLOW_METER_SERVICE_IDLE))
    {
        /* Direction edges have priority over the regular polling interval. */
        g_next_poll_ms = now_ms;
    }
}

static void FlowMeterService_AcceptSample(uint32_t now_ms,
                                          const ZelzFlow_Realtime_T *realtime)
{
    uint8_t direction_level;
    FlowMeterService_Direction_T direction;

    direction_level = FlowMeter_ReadDirection();
    direction = FlowMeterService_DecodeDirection(direction_level);

    g_snapshot.meter = *realtime;
    g_snapshot.direction_level = direction_level;
    g_snapshot.direction = direction;
    g_snapshot.inserted = FlowMeter_ReadInsertDetect();
    g_snapshot.last_update_ms = now_ms;
    g_snapshot.valid = 1U;

    FlowMeterService_AccountSample(now_ms, realtime, direction);
}

static void FlowMeterService_RetryOrSchedule(uint32_t now_ms)
{
    if (g_retry_count < g_config.max_retries)
    {
        g_retry_count++;
        g_stats.retries++;
        if (FlowMeterService_SendRequest(now_ms) == FLOW_METER_SERVICE_OK)
        {
            return;
        }
    }

    FlowMeterService_ScheduleNext(now_ms);
}

int FlowMeterService_Init(const FlowMeterService_Config_T *config)
{
    if ((config == 0) ||
        (config->slave_address == 0U) ||
        (config->inflow_level > 1U) ||
        (config->poll_interval_ms == 0U) ||
        (config->response_timeout_ms == 0U))
    {
        return FLOW_METER_SERVICE_ERR_INVALID_ARG;
    }

    g_config = *config;
    g_state = FLOW_METER_SERVICE_STOPPED;
    g_state_started_ms = 0U;
    g_next_poll_ms = 0U;
    g_last_meter_batch_milli_l = 0U;
    g_session_meter_total_base_milli_m3 = 0U;
    g_last_direction_event_sequence = 0U;
    g_last_accounting_direction = FLOW_METER_SERVICE_DIRECTION_UNKNOWN;
    g_boundary_direction = FLOW_METER_SERVICE_DIRECTION_UNKNOWN;
    g_initialized = 1U;
    g_running = 0U;
    g_retry_count = 0U;
    g_baseline_valid = 0U;
    g_meter_total_baseline_valid = 0U;
    g_direction_boundary_pending = 0U;

    g_snapshot.meter.batch_total_milli_l = 0U;
    g_snapshot.meter.total_milli_m3 = 0U;
    g_snapshot.meter.instant_milli_m3_per_h = 0U;
    g_snapshot.session_inflow_milli_l = 0U;
    g_snapshot.session_outflow_milli_l = 0U;
    g_snapshot.session_unclassified_milli_l = 0U;
    g_snapshot.session_net_milli_l = 0;
    g_snapshot.session_meter_absolute_milli_l = 0U;
    g_snapshot.last_update_ms = 0U;
    g_snapshot.reset_sequence = 0U;
    g_snapshot.reset_effective_ms = 0U;
    g_snapshot.direction_event_sequence = 0U;
    g_snapshot.last_direction_event_ms = 0U;
    g_snapshot.direction = FLOW_METER_SERVICE_DIRECTION_UNKNOWN;
    g_snapshot.direction_level = 0U;
    g_snapshot.inserted = 0U;
    g_snapshot.valid = 0U;
    g_snapshot.reset_pending = 0U;
    g_snapshot.direction_boundary_pending = 0U;

    FlowMeterService_ResetStats();
    return FLOW_METER_SERVICE_OK;
}

int FlowMeterService_Start(void)
{
    uint32_t now_ms;

    if (g_initialized == 0U)
    {
        return FLOW_METER_SERVICE_ERR_NOT_READY;
    }

    if ((FlowMeter_IsRs485Configured() == 0U) ||
        (FlowMeter_IsPowerEnabled() == 0U))
    {
        return FLOW_METER_SERVICE_ERR_NOT_READY;
    }

    now_ms = PlatformPort_GetMs();
    g_running = 1U;
    g_state = FLOW_METER_SERVICE_IDLE;
    g_next_poll_ms = now_ms;
    g_retry_count = 0U;
    return FLOW_METER_SERVICE_OK;
}

void FlowMeterService_Stop(void)
{
    g_running = 0U;
    g_state = FLOW_METER_SERVICE_STOPPED;
    g_retry_count = 0U;
}

void FlowMeterService_Process(void)
{
    uint8_t response[FLOW_METER_SERVICE_RESPONSE_MAX];
    ZelzFlow_Realtime_T realtime;
    uint8_t exception_code = 0U;
    uint32_t now_ms;
    int length;
    int result;

    now_ms = PlatformPort_GetMs();
    FlowMeterService_HandleDirectionEvent(now_ms);

    if (g_running == 0U)
    {
        return;
    }

    if (g_state == FLOW_METER_SERVICE_IDLE)
    {
        if ((g_snapshot.reset_pending != 0U) ||
            (FlowMeterService_TimeReached(now_ms, g_next_poll_ms) != 0U))
        {
            if (FlowMeterService_SendRequest(now_ms) != FLOW_METER_SERVICE_OK)
            {
                FlowMeterService_RetryOrSchedule(now_ms);
            }
        }
        return;
    }

    if (g_state != FLOW_METER_SERVICE_WAIT_RESPONSE)
    {
        return;
    }

    length = FlowMeter_ReadFrame(response, sizeof(response));
    if (length > 0)
    {
        result = ZelzFlow_ParseReadRealtimeResponse(g_config.slave_address,
                                                    response,
                                                    (uint16_t)length,
                                                    &realtime,
                                                    &exception_code);
        if (result == ZELZ_FLOW_OK)
        {
            g_stats.responses_ok++;
            FlowMeterService_AcceptSample(now_ms, &realtime);
            FlowMeterService_ScheduleNext(now_ms);
            return;
        }

        if (result == ZELZ_FLOW_ERR_EXCEPTION)
        {
            (void)exception_code;
            g_stats.exception_responses++;
            FlowMeterService_ScheduleNext(now_ms);
            return;
        }

        /* Ignore malformed/unrelated frames until this transaction times out. */
        g_stats.protocol_errors++;
    }
    else if (length < 0)
    {
        g_stats.transport_errors++;
        FlowMeterService_RetryOrSchedule(now_ms);
        return;
    }

    if (FlowMeterService_Elapsed(now_ms,
                                 g_state_started_ms,
                                 g_config.response_timeout_ms) != 0U)
    {
        g_stats.timeouts++;
        FlowMeterService_RetryOrSchedule(now_ms);
    }
}

FlowMeterService_State_T FlowMeterService_GetState(void)
{
    return g_state;
}

uint8_t FlowMeterService_IsRunning(void)
{
    return g_running;
}

void FlowMeterService_RequestSessionReset(void)
{
    if (g_initialized == 0U)
    {
        return;
    }

    g_snapshot.reset_pending = 1U;
    if (g_running != 0U)
    {
        /* Cause the next idle pass to establish the new baseline immediately. */
        g_next_poll_ms = PlatformPort_GetMs();
    }
}

uint8_t FlowMeterService_IsSessionResetPending(void)
{
    return g_snapshot.reset_pending;
}

void FlowMeterService_GetSnapshot(FlowMeterService_Snapshot_T *snapshot)
{
    if (snapshot != 0)
    {
        *snapshot = g_snapshot;
    }
}

void FlowMeterService_GetStats(FlowMeterService_Stats_T *stats)
{
    if (stats != 0)
    {
        *stats = g_stats;
    }
}

void FlowMeterService_ResetStats(void)
{
    g_stats.requests = 0U;
    g_stats.responses_ok = 0U;
    g_stats.timeouts = 0U;
    g_stats.retries = 0U;
    g_stats.transport_errors = 0U;
    g_stats.protocol_errors = 0U;
    g_stats.exception_responses = 0U;
    g_stats.meter_rebases = 0U;
    g_stats.meter_total_rebases = 0U;
    g_stats.direction_changes = 0U;
    g_stats.direction_interrupts = 0U;
    g_stats.direction_events_missed = 0U;
    g_stats.ambiguous_direction_intervals = 0U;
    g_stats.session_resets = 0U;
}

uint8_t FlowMeterService_IsFresh(uint32_t max_age_ms)
{
    if ((g_snapshot.valid == 0U) || (max_age_ms == 0U))
    {
        return 0U;
    }

    return (uint8_t)(((uint32_t)(PlatformPort_GetMs() - g_snapshot.last_update_ms)) <= max_age_ms);
}
