#include "flow_meter.h"

#include "platform_port.h"

static FlowMeter_Rs485Config_T g_rs485_config;
static FlowMeter_Stats_T g_stats;
static uint8_t g_rs485_configured;
static uint8_t g_power_enabled;

static uint8_t FlowMeter_LevelValid(uint8_t level)
{
    return (uint8_t)((level == 0U) || (level == 1U));
}

static void FlowMeter_SetRxIdle(void)
{
    PlatformPort_FlowTxEnablePinWrite(g_rs485_config.tx_disable_level);
    PlatformPort_FlowRxEnablePinWrite(g_rs485_config.rx_enable_level);
}

void FlowMeter_Init(void)
{
    g_rs485_config.tx_enable_level = 0U;
    g_rs485_config.tx_disable_level = 0U;
    g_rs485_config.rx_enable_level = 0U;
    g_rs485_config.rx_disable_level = 0U;
    g_rs485_configured = 0U;
    g_power_enabled = 0U;
    FlowMeter_ResetStats();
}

int FlowMeter_ConfigureRs485(const FlowMeter_Rs485Config_T *config)
{
    if (config == 0)
    {
        return FLOW_METER_ERR_INVALID_ARG;
    }

    if ((FlowMeter_LevelValid(config->tx_enable_level) == 0U) ||
        (FlowMeter_LevelValid(config->tx_disable_level) == 0U) ||
        (FlowMeter_LevelValid(config->rx_enable_level) == 0U) ||
        (FlowMeter_LevelValid(config->rx_disable_level) == 0U))
    {
        return FLOW_METER_ERR_INVALID_ARG;
    }

    g_rs485_config = *config;
    g_rs485_configured = 1U;
    FlowMeter_SetRxIdle();
    return FLOW_METER_OK;
}

uint8_t FlowMeter_IsRs485Configured(void)
{
    return g_rs485_configured;
}

void FlowMeter_SetPower(uint8_t enable)
{
    g_power_enabled = (enable != 0U) ? 1U : 0U;
    PlatformPort_FlowSetPower(g_power_enabled);
}

uint8_t FlowMeter_IsPowerEnabled(void)
{
    return g_power_enabled;
}

int FlowMeter_SendFrame(const uint8_t *data, uint16_t length)
{
    uint16_t written;

    if ((data == 0) || (length == 0U))
    {
        return FLOW_METER_ERR_INVALID_ARG;
    }

    if (g_rs485_configured == 0U)
    {
        return FLOW_METER_ERR_NOT_CONFIGURED;
    }

    /* Disable local receive before enabling the half-duplex transmitter. */
    PlatformPort_FlowRxEnablePinWrite(g_rs485_config.rx_disable_level);
    PlatformPort_FlowTxEnablePinWrite(g_rs485_config.tx_enable_level);

    written = PlatformPort_FlowWrite(data, length);

    /* Always restore receive-idle state, including short-write failures. */
    FlowMeter_SetRxIdle();

    if (written != length)
    {
        g_stats.tx_errors++;
        return FLOW_METER_ERR_IO;
    }

    g_stats.tx_frames++;
    g_stats.tx_bytes += written;
    return (int)written;
}

int FlowMeter_ReadFrame(uint8_t *data, uint16_t capacity)
{
    int result;

    if ((data == 0) || (capacity == 0U))
    {
        return FLOW_METER_ERR_INVALID_ARG;
    }

    result = PlatformPort_FlowReadFrame(data, capacity);
    if (result > 0)
    {
        g_stats.rx_frames++;
        g_stats.rx_bytes += (uint16_t)result;
        return result;
    }

    if (result == PLATFORM_PORT_FLOW_NO_FRAME)
    {
        return 0;
    }

    if (result == PLATFORM_PORT_FLOW_ERR_BUFFER_TOO_SMALL)
    {
        g_stats.rx_buffer_too_small++;
        return FLOW_METER_ERR_BUFFER_TOO_SMALL;
    }

    g_stats.rx_transport_errors++;
    return FLOW_METER_ERR_IO;
}

uint8_t FlowMeter_ReadDirection(void)
{
    return PlatformPort_FlowDirectionRead();
}

uint8_t FlowMeter_ReadInsertDetect(void)
{
    return PlatformPort_FlowInsertDetectRead();
}

void FlowMeter_GetStats(FlowMeter_Stats_T *stats)
{
    if (stats != 0)
    {
        *stats = g_stats;
    }
}

void FlowMeter_ResetStats(void)
{
    g_stats.tx_frames = 0U;
    g_stats.tx_bytes = 0U;
    g_stats.tx_errors = 0U;
    g_stats.rx_frames = 0U;
    g_stats.rx_bytes = 0U;
    g_stats.rx_buffer_too_small = 0U;
    g_stats.rx_transport_errors = 0U;
}
