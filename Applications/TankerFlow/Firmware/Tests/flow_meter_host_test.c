#include <stdio.h>
#include <string.h>

#include "flow_meter.h"
#include "platform_port.h"

static uint8_t g_tx_level;
static uint8_t g_rx_level;
static uint8_t g_power_level;
static uint8_t g_direction_level;
static uint8_t g_insert_level;
static uint8_t g_write_data[64];
static uint16_t g_write_length;
static uint16_t g_write_return_length;
static uint8_t g_rx_frame[64];
static uint16_t g_rx_frame_length;
static int g_rx_forced_result;
static char g_events[32];
static uint8_t g_event_count;

static int Expect(int condition, const char *message)
{
    if (!condition)
    {
        printf("FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

static void Event(char event)
{
    if (g_event_count < sizeof(g_events))
    {
        g_events[g_event_count++] = event;
    }
}

uint32_t PlatformPort_GetMs(void) { return 0U; }
uint16_t PlatformPort_ModemRead(uint8_t *data, uint16_t max_length) { (void)data; (void)max_length; return 0U; }
uint16_t PlatformPort_ModemWrite(const uint8_t *data, uint16_t length) { (void)data; return length; }
void PlatformPort_ModemResetAssert(void) {}
void PlatformPort_ModemResetRelease(void) {}
void PlatformPort_ModemPowerKeyAssert(void) {}
void PlatformPort_ModemPowerKeyRelease(void) {}
uint16_t PlatformPort_GnssRead(uint8_t *data, uint16_t max_length) { (void)data; (void)max_length; return 0U; }

int PlatformPort_FlowReadFrame(uint8_t *data, uint16_t capacity)
{
    if (g_rx_forced_result != 0)
    {
        return g_rx_forced_result;
    }
    if (g_rx_frame_length == 0U)
    {
        return PLATFORM_PORT_FLOW_NO_FRAME;
    }
    if (capacity < g_rx_frame_length)
    {
        return PLATFORM_PORT_FLOW_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(data, g_rx_frame, g_rx_frame_length);
    return (int)g_rx_frame_length;
}

uint16_t PlatformPort_FlowWrite(const uint8_t *data, uint16_t length)
{
    Event('W');
    g_write_length = length;
    if (length <= sizeof(g_write_data))
    {
        memcpy(g_write_data, data, length);
    }
    return g_write_return_length;
}

void PlatformPort_FlowSetPower(uint8_t enable)
{
    g_power_level = enable;
}

void PlatformPort_FlowTxEnablePinWrite(uint8_t high)
{
    g_tx_level = high;
    Event(high ? 'T' : 't');
}

void PlatformPort_FlowRxEnablePinWrite(uint8_t high)
{
    g_rx_level = high;
    Event(high ? 'R' : 'r');
}

uint8_t PlatformPort_FlowDirectionRead(void) { return g_direction_level; }
uint8_t PlatformPort_FlowInsertDetectRead(void) { return g_insert_level; }

static void ResetHarness(void)
{
    g_tx_level = 0U;
    g_rx_level = 0U;
    g_power_level = 0U;
    g_direction_level = 0U;
    g_insert_level = 0U;
    g_write_length = 0U;
    g_write_return_length = 0U;
    g_rx_frame_length = 0U;
    g_rx_forced_result = 0;
    g_event_count = 0U;
    memset(g_write_data, 0, sizeof(g_write_data));
    memset(g_rx_frame, 0, sizeof(g_rx_frame));
    memset(g_events, 0, sizeof(g_events));
}

int main(void)
{
    FlowMeter_Rs485Config_T config;
    FlowMeter_Stats_T stats;
    uint8_t tx[] = {0x01U, 0x03U, 0x00U, 0x00U};
    uint8_t rx[16];
    int result;
    int ok = 1;

    ResetHarness();
    FlowMeter_Init();
    ok &= Expect(FlowMeter_IsRs485Configured() == 0U, "RS485 starts unconfigured");
    ok &= Expect(FlowMeter_IsPowerEnabled() == 0U, "flow power starts disabled logically");
    ok &= Expect(g_event_count == 0U, "init must not change raw RS485 pins");

    result = FlowMeter_SendFrame(tx, sizeof(tx));
    ok &= Expect(result == FLOW_METER_ERR_NOT_CONFIGURED, "TX rejected until RS485 polarity is configured");

    config.tx_enable_level = 1U;
    config.tx_disable_level = 0U;
    config.rx_enable_level = 0U;
    config.rx_disable_level = 1U;
    result = FlowMeter_ConfigureRs485(&config);
    ok &= Expect(result == FLOW_METER_OK, "valid RS485 config accepted");
    ok &= Expect(FlowMeter_IsRs485Configured() != 0U, "RS485 configured flag set");
    ok &= Expect((g_tx_level == 0U) && (g_rx_level == 0U), "configure enters RX idle levels");

    FlowMeter_SetPower(1U);
    ok &= Expect((FlowMeter_IsPowerEnabled() != 0U) && (g_power_level == 1U), "explicit power enable forwarded");

    g_event_count = 0U;
    g_write_return_length = sizeof(tx);
    result = FlowMeter_SendFrame(tx, sizeof(tx));
    ok &= Expect(result == (int)sizeof(tx), "full TX succeeds");
    ok &= Expect(g_write_length == sizeof(tx), "TX length forwarded");
    ok &= Expect(memcmp(g_write_data, tx, sizeof(tx)) == 0, "TX bytes forwarded");
    ok &= Expect(strcmp(g_events, "RTWtr") == 0, "half-duplex sequence is RX-off TX-on write TX-off RX-on");

    g_event_count = 0U;
    g_write_return_length = 2U;
    result = FlowMeter_SendFrame(tx, sizeof(tx));
    ok &= Expect(result == FLOW_METER_ERR_IO, "short TX reported as IO error");
    ok &= Expect((g_tx_level == 0U) && (g_rx_level == 0U), "RX idle restored after TX error");

    g_rx_frame[0] = 0x11U;
    g_rx_frame[1] = 0x22U;
    g_rx_frame[2] = 0x33U;
    g_rx_frame_length = 3U;
    result = FlowMeter_ReadFrame(rx, sizeof(rx));
    ok &= Expect(result == 3, "complete RX frame returned");
    ok &= Expect(memcmp(rx, g_rx_frame, 3U) == 0, "RX bytes copied");

    result = FlowMeter_ReadFrame(rx, 2U);
    ok &= Expect(result == FLOW_METER_ERR_BUFFER_TOO_SMALL, "small RX buffer normalized");

    g_rx_forced_result = PLATFORM_PORT_FLOW_ERR_IO;
    result = FlowMeter_ReadFrame(rx, sizeof(rx));
    ok &= Expect(result == FLOW_METER_ERR_IO, "platform RX error normalized to driver IO error");

    g_direction_level = 1U;
    g_insert_level = 1U;
    ok &= Expect(FlowMeter_ReadDirection() == 1U, "direction input forwarded");
    ok &= Expect(FlowMeter_ReadInsertDetect() == 1U, "insert input forwarded");

    FlowMeter_GetStats(&stats);
    ok &= Expect(stats.tx_frames == 1U, "TX frame statistic");
    ok &= Expect(stats.tx_bytes == sizeof(tx), "TX byte statistic");
    ok &= Expect(stats.tx_errors == 1U, "TX error statistic");
    ok &= Expect(stats.rx_frames == 1U, "RX frame statistic");
    ok &= Expect(stats.rx_bytes == 3U, "RX byte statistic");
    ok &= Expect(stats.rx_buffer_too_small == 1U, "RX small-buffer statistic");
    ok &= Expect(stats.rx_transport_errors == 1U, "RX transport-error statistic");

    if (!ok)
    {
        return 1;
    }

    printf("Flow meter host tests: PASS\n");
    return 0;
}
