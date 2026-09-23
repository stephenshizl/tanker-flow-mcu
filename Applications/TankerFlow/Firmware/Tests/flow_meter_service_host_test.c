#include <stdio.h>
#include <string.h>

#include "flow_meter.h"
#include "flow_meter_service.h"
#include "modbus_rtu.h"
#include "platform_port.h"

static uint32_t g_now_ms;
static uint8_t g_direction_level;
static uint8_t g_insert_level;
static uint8_t g_power_level;
static uint8_t g_tx_level;
static uint8_t g_rx_level;
static uint8_t g_tx_data[32];
static uint16_t g_tx_length;
static uint16_t g_tx_return_length;
static uint8_t g_rx_frame[64];
static uint16_t g_rx_frame_length;
static PlatformPort_FlowDirectionEvent_T g_direction_event;
static uint8_t g_direction_event_pending;

static int Expect(int condition, const char *message)
{
    if (!condition)
    {
        printf("FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

uint32_t PlatformPort_GetMs(void) { return g_now_ms; }
uint16_t PlatformPort_ModemRead(uint8_t *data, uint16_t max_length) { (void)data; (void)max_length; return 0U; }
uint16_t PlatformPort_ModemWrite(const uint8_t *data, uint16_t length) { (void)data; return length; }
void PlatformPort_ModemResetAssert(void) {}
void PlatformPort_ModemResetRelease(void) {}
void PlatformPort_ModemPowerKeyAssert(void) {}
void PlatformPort_ModemPowerKeyRelease(void) {}
uint16_t PlatformPort_GnssRead(uint8_t *data, uint16_t max_length) { (void)data; (void)max_length; return 0U; }

int PlatformPort_FlowReadFrame(uint8_t *data, uint16_t capacity)
{
    uint16_t length;

    if (g_rx_frame_length == 0U)
    {
        return PLATFORM_PORT_FLOW_NO_FRAME;
    }
    if (capacity < g_rx_frame_length)
    {
        return PLATFORM_PORT_FLOW_ERR_BUFFER_TOO_SMALL;
    }

    length = g_rx_frame_length;
    memcpy(data, g_rx_frame, length);
    g_rx_frame_length = 0U;
    return (int)length;
}

uint16_t PlatformPort_FlowWrite(const uint8_t *data, uint16_t length)
{
    g_tx_length = length;
    if (length <= sizeof(g_tx_data))
    {
        memcpy(g_tx_data, data, length);
    }
    return g_tx_return_length;
}

void PlatformPort_FlowSetPower(uint8_t enable) { g_power_level = enable; }
void PlatformPort_FlowTxEnablePinWrite(uint8_t high) { g_tx_level = high; }
void PlatformPort_FlowRxEnablePinWrite(uint8_t high) { g_rx_level = high; }
uint8_t PlatformPort_FlowDirectionRead(void) { return g_direction_level; }
uint8_t PlatformPort_FlowInsertDetectRead(void) { return g_insert_level; }

uint8_t PlatformPort_FlowDirectionEventConsume(PlatformPort_FlowDirectionEvent_T *event)
{
    if ((g_direction_event_pending == 0U) || (event == 0))
    {
        return 0U;
    }

    *event = g_direction_event;
    g_direction_event_pending = 0U;
    return 1U;
}

static void EncodeZelzU32(uint32_t value, uint8_t *data)
{
    data[0] = (uint8_t)((value >> 8U) & 0xFFU);
    data[1] = (uint8_t)(value & 0xFFU);
    data[2] = (uint8_t)((value >> 24U) & 0xFFU);
    data[3] = (uint8_t)((value >> 16U) & 0xFFU);
}

static void QueueRealtimeResponse(uint8_t slave_address,
                                  uint32_t batch_milli_l,
                                  uint32_t total_milli_m3,
                                  uint32_t instant_milli_m3_per_h)
{
    uint16_t crc;

    g_rx_frame[0] = slave_address;
    g_rx_frame[1] = MODBUS_RTU_FUNC_READ_HOLDING_REGISTERS;
    g_rx_frame[2] = 12U;
    EncodeZelzU32(batch_milli_l, &g_rx_frame[3]);
    EncodeZelzU32(total_milli_m3, &g_rx_frame[7]);
    EncodeZelzU32(instant_milli_m3_per_h, &g_rx_frame[11]);
    crc = ModbusRtu_Crc16(g_rx_frame, 15U);
    g_rx_frame[15] = (uint8_t)(crc & 0xFFU);
    g_rx_frame[16] = (uint8_t)((crc >> 8U) & 0xFFU);
    g_rx_frame_length = 17U;
}

static void QueueDirectionEdge(uint8_t new_level,
                               uint32_t timestamp_ms,
                               uint32_t sequence)
{
    g_direction_level = new_level;
    g_direction_event.sequence = sequence;
    g_direction_event.timestamp_ms = timestamp_ms;
    g_direction_event.level = new_level;
    g_direction_event_pending = 1U;
}

static void ResetHarness(void)
{
    FlowMeter_Rs485Config_T rs485;

    g_now_ms = 0U;
    g_direction_level = 1U;
    g_insert_level = 1U;
    g_power_level = 0U;
    g_tx_level = 0U;
    g_rx_level = 0U;
    g_tx_length = 0U;
    g_tx_return_length = 8U;
    g_rx_frame_length = 0U;
    g_direction_event_pending = 0U;
    memset(&g_direction_event, 0, sizeof(g_direction_event));
    memset(g_tx_data, 0, sizeof(g_tx_data));
    memset(g_rx_frame, 0, sizeof(g_rx_frame));

    FlowMeter_Init();
    rs485.tx_enable_level = 1U;
    rs485.tx_disable_level = 0U;
    rs485.rx_enable_level = 0U;
    rs485.rx_disable_level = 1U;
    (void)FlowMeter_ConfigureRs485(&rs485);
    FlowMeter_SetPower(1U);
}

static void PollWithResponse(uint32_t batch_milli_l,
                             uint32_t total_milli_m3,
                             uint32_t instant_milli_m3_per_h)
{
    FlowMeterService_Process();
    QueueRealtimeResponse(1U, batch_milli_l, total_milli_m3, instant_milli_m3_per_h);
    FlowMeterService_Process();
}

int main(void)
{
    FlowMeterService_Config_T config;
    FlowMeterService_Snapshot_T snapshot;
    FlowMeterService_Stats_T stats;
    int result;
    int ok = 1;

    ResetHarness();

    config.slave_address = 1U;
    config.inflow_level = 1U;
    config.poll_interval_ms = 100U;
    config.response_timeout_ms = 50U;
    config.max_retries = 1U;

    result = FlowMeterService_Init(&config);
    ok &= Expect(result == FLOW_METER_SERVICE_OK, "service init");
    result = FlowMeterService_Start();
    ok &= Expect(result == FLOW_METER_SERVICE_OK, "service start");

    FlowMeterService_Process();
    ok &= Expect(g_tx_length == 8U, "combined realtime request sent");
    ok &= Expect((g_tx_data[0] == 1U) && (g_tx_data[1] == 3U), "request slave/function");
    ok &= Expect((g_tx_data[2] == 0U) && (g_tx_data[3] == 0U), "request starts at register 0");
    ok &= Expect((g_tx_data[4] == 0U) && (g_tx_data[5] == 6U), "request reads six registers");

    QueueRealtimeResponse(1U, 100000U, 100U, 1200U);
    FlowMeterService_Process();
    FlowMeterService_GetSnapshot(&snapshot);
    ok &= Expect(snapshot.valid != 0U, "first response becomes valid snapshot");
    ok &= Expect(snapshot.session_inflow_milli_l == 0U, "first sample establishes baseline only");
    ok &= Expect(snapshot.session_outflow_milli_l == 0U, "first sample no outflow");
    ok &= Expect(snapshot.session_unclassified_milli_l == 0U, "first sample no unclassified volume");
    ok &= Expect(snapshot.session_net_milli_l == 0, "first sample net zero");
    ok &= Expect(snapshot.session_meter_absolute_milli_l == 0U, "coarse meter total baseline");
    ok &= Expect(snapshot.direction == FLOW_METER_SERVICE_DIRECTION_INFLOW, "inflow level mapping");
    ok &= Expect(snapshot.inserted == 1U, "insert detect captured");

    g_now_ms = 100U;
    PollWithResponse(150000U, 101U, 1300U);
    FlowMeterService_GetSnapshot(&snapshot);
    ok &= Expect(snapshot.session_inflow_milli_l == 50000U, "same-direction inflow delta accumulated");
    ok &= Expect(snapshot.session_net_milli_l == 50000, "net = inflow - outflow");
    ok &= Expect(snapshot.session_meter_absolute_milli_l == 1000U, "meter total used as coarse absolute cross-check");

    /*
     * Direction edge occurs before the regular 200 ms poll. The service must
     * consume the ISR event and issue an immediate boundary read at 150 ms.
     */
    g_now_ms = 150U;
    QueueDirectionEdge(0U, 145U, 1U);
    FlowMeterService_Process();
    ok &= Expect(FlowMeterService_GetState() == FLOW_METER_SERVICE_WAIT_RESPONSE,
                 "direction edge forces immediate meter read");
    QueueRealtimeResponse(1U, 180000U, 101U, 1400U);
    FlowMeterService_Process();
    FlowMeterService_GetSnapshot(&snapshot);
    ok &= Expect(snapshot.direction == FLOW_METER_SERVICE_DIRECTION_OUTFLOW,
                 "direction interrupt updates current direction");
    ok &= Expect(snapshot.direction_event_sequence == 1U,
                 "direction interrupt sequence captured");
    ok &= Expect(snapshot.last_direction_event_ms == 145U,
                 "direction interrupt timestamp captured");
    ok &= Expect(snapshot.session_inflow_milli_l == 50000U,
                 "boundary delta not misclassified as inflow");
    ok &= Expect(snapshot.session_outflow_milli_l == 0U,
                 "boundary delta not misclassified as outflow");
    ok &= Expect(snapshot.session_unclassified_milli_l == 30000U,
                 "boundary delta preserved as unclassified volume");
    ok &= Expect(snapshot.direction_boundary_pending == 0U,
                 "boundary resolved on valid meter sample");

    g_now_ms = 250U;
    PollWithResponse(200000U, 102U, 1500U);
    FlowMeterService_GetSnapshot(&snapshot);
    ok &= Expect(snapshot.session_outflow_milli_l == 20000U,
                 "stable outflow delta accumulated after boundary");
    ok &= Expect(snapshot.session_net_milli_l == 30000,
                 "directional net total excludes ambiguous boundary volume");
    ok &= Expect(snapshot.session_meter_absolute_milli_l == 2000U,
                 "coarse total continues from software session baseline");

    FlowMeterService_RequestSessionReset();
    ok &= Expect(FlowMeterService_IsSessionResetPending() != 0U,
                 "software session reset becomes pending");
    g_now_ms = 260U;
    PollWithResponse(210000U, 102U, 1500U);
    FlowMeterService_GetSnapshot(&snapshot);
    ok &= Expect(snapshot.reset_pending == 0U, "reset completes on valid sample");
    ok &= Expect(snapshot.reset_sequence == 1U, "reset sequence increments");
    ok &= Expect(snapshot.reset_effective_ms == 260U, "reset effective timestamp recorded");
    ok &= Expect(snapshot.session_inflow_milli_l == 0U, "reset clears inflow session total");
    ok &= Expect(snapshot.session_outflow_milli_l == 0U, "reset clears outflow session total");
    ok &= Expect(snapshot.session_unclassified_milli_l == 0U, "reset clears unclassified volume");
    ok &= Expect(snapshot.session_net_milli_l == 0, "reset clears net session total");
    ok &= Expect(snapshot.session_meter_absolute_milli_l == 0U,
                 "reset establishes new coarse total baseline without clearing meter");

    g_now_ms = 360U;
    PollWithResponse(225000U, 103U, 1600U);
    FlowMeterService_GetSnapshot(&snapshot);
    ok &= Expect(snapshot.session_outflow_milli_l == 15000U,
                 "post-reset outflow starts from new baseline");
    ok &= Expect(snapshot.session_net_milli_l == -15000,
                 "post-reset net may be negative");
    ok &= Expect(snapshot.session_meter_absolute_milli_l == 1000U,
                 "post-reset coarse total delta tracked without meter-side clear");
    ok &= Expect(FlowMeterService_IsFresh(1U) != 0U, "fresh snapshot");

    /* Meter-side high-resolution counter decrease: rebase, never add wrap volume. */
    g_now_ms = 460U;
    PollWithResponse(1000U, 104U, 1000U);
    FlowMeterService_GetSnapshot(&snapshot);
    ok &= Expect(snapshot.session_outflow_milli_l == 15000U,
                 "meter counter decrease does not corrupt session total");

    /* Timeout triggers one retry, then a later response completes it. */
    g_now_ms = 560U;
    FlowMeterService_Process();
    g_now_ms = 611U;
    FlowMeterService_Process();
    ok &= Expect(FlowMeterService_GetState() == FLOW_METER_SERVICE_WAIT_RESPONSE,
                 "timeout retry remains waiting");
    QueueRealtimeResponse(1U, 2000U, 104U, 900U);
    FlowMeterService_Process();

    FlowMeterService_GetStats(&stats);
    ok &= Expect(stats.responses_ok >= 6U, "valid response statistics");
    ok &= Expect(stats.direction_interrupts == 1U, "direction IRQ statistic");
    ok &= Expect(stats.direction_changes == 1U, "direction change statistic");
    ok &= Expect(stats.ambiguous_direction_intervals == 1U,
                 "ambiguous boundary statistic");
    ok &= Expect(stats.session_resets == 1U, "session reset statistic");
    ok &= Expect(stats.meter_rebases == 1U, "meter rebase statistic");
    ok &= Expect(stats.timeouts == 1U, "timeout statistic");
    ok &= Expect(stats.retries == 1U, "retry statistic");

    if (!ok)
    {
        return 1;
    }

    printf("Flow meter service host tests: PASS\n");
    return 0;
}
