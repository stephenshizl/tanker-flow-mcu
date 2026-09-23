#include "flow_meter_app.h"

#include "flow_meter.h"
#include "flow_meter_service.h"
#include "platform_port.h"

static FlowMeterApp_Config_T g_config;
static FlowMeterApp_State_T g_state = FLOW_METER_APP_STATE_UNINITIALIZED;
static FlowMeterApp_Fault_T g_fault = FLOW_METER_APP_FAULT_NONE;
static uint32_t g_power_on_ms;
static uint8_t g_initialized;

static uint8_t FlowMeterApp_LevelValid(uint8_t level)
{
    return (uint8_t)((level == 0U) || (level == 1U));
}

static uint8_t FlowMeterApp_ConfigValid(const FlowMeterApp_Config_T *config)
{
    if (config == 0)
    {
        return 0U;
    }

    if (config->enabled > 1U)
    {
        return 0U;
    }

    if (config->enabled == 0U)
    {
        return 1U;
    }

    if ((FlowMeterApp_LevelValid(config->rs485_tx_enable_level) == 0U) ||
        (FlowMeterApp_LevelValid(config->rs485_tx_disable_level) == 0U) ||
        (FlowMeterApp_LevelValid(config->rs485_rx_enable_level) == 0U) ||
        (FlowMeterApp_LevelValid(config->rs485_rx_disable_level) == 0U) ||
        (FlowMeterApp_LevelValid(config->inflow_level) == 0U) ||
        (FlowMeterApp_LevelValid(config->insert_present_level) == 0U) ||
        (config->slave_address == 0U) ||
        (config->poll_interval_ms == 0U) ||
        (config->response_timeout_ms == 0U))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t FlowMeterApp_Elapsed(uint32_t now_ms,
                                    uint32_t start_ms,
                                    uint32_t duration_ms)
{
    return (uint8_t)(((uint32_t)(now_ms - start_ms)) >= duration_ms);
}

static void FlowMeterApp_SetFault(FlowMeterApp_Fault_T fault)
{
    FlowMeterService_Stop();
    if (FlowMeter_IsPowerEnabled() != 0U)
    {
        FlowMeter_SetPower(0U);
    }
    g_fault = fault;
    g_state = FLOW_METER_APP_STATE_FAULT;
}

int FlowMeterApp_Init(const FlowMeterApp_Config_T *config)
{
    FlowMeter_Rs485Config_T rs485_config;
    FlowMeterService_Config_T service_config;

    g_initialized = 0U;
    g_power_on_ms = 0U;
    g_fault = FLOW_METER_APP_FAULT_NONE;
    g_state = FLOW_METER_APP_STATE_UNINITIALIZED;

    if (FlowMeterApp_ConfigValid(config) == 0U)
    {
        g_fault = FLOW_METER_APP_FAULT_INVALID_CONFIG;
        g_state = FLOW_METER_APP_STATE_FAULT;
        return FLOW_METER_APP_ERR_INVALID_ARG;
    }

    g_config = *config;
    FlowMeter_Init();

    if (g_config.enabled == 0U)
    {
        g_initialized = 1U;
        g_state = FLOW_METER_APP_STATE_DISABLED;
        return FLOW_METER_APP_OK;
    }

    rs485_config.tx_enable_level = g_config.rs485_tx_enable_level;
    rs485_config.tx_disable_level = g_config.rs485_tx_disable_level;
    rs485_config.rx_enable_level = g_config.rs485_rx_enable_level;
    rs485_config.rx_disable_level = g_config.rs485_rx_disable_level;
    if (FlowMeter_ConfigureRs485(&rs485_config) != FLOW_METER_OK)
    {
        g_fault = FLOW_METER_APP_FAULT_RS485_CONFIG;
        g_state = FLOW_METER_APP_STATE_FAULT;
        return FLOW_METER_APP_ERR_DRIVER;
    }

    service_config.slave_address = g_config.slave_address;
    service_config.inflow_level = g_config.inflow_level;
    service_config.poll_interval_ms = g_config.poll_interval_ms;
    service_config.response_timeout_ms = g_config.response_timeout_ms;
    service_config.max_retries = g_config.max_retries;
    if (FlowMeterService_Init(&service_config) != FLOW_METER_SERVICE_OK)
    {
        g_fault = FLOW_METER_APP_FAULT_SERVICE_INIT;
        g_state = FLOW_METER_APP_STATE_FAULT;
        return FLOW_METER_APP_ERR_SERVICE;
    }

    g_initialized = 1U;
    g_state = FLOW_METER_APP_STATE_STOPPED;
    return FLOW_METER_APP_OK;
}

int FlowMeterApp_Start(void)
{
    if (g_initialized == 0U)
    {
        return FLOW_METER_APP_ERR_INVALID_ARG;
    }

    if (g_config.enabled == 0U)
    {
        return FLOW_METER_APP_ERR_DISABLED;
    }

    if ((g_state == FLOW_METER_APP_STATE_POWER_SETTLE) ||
        (g_state == FLOW_METER_APP_STATE_RUNNING))
    {
        return FLOW_METER_APP_OK;
    }

    if (g_state == FLOW_METER_APP_STATE_FAULT)
    {
        return FLOW_METER_APP_ERR_SERVICE;
    }

    FlowMeter_SetPower(1U);
    g_power_on_ms = PlatformPort_GetMs();
    g_state = FLOW_METER_APP_STATE_POWER_SETTLE;
    return FLOW_METER_APP_OK;
}

void FlowMeterApp_Stop(void)
{
    if (g_initialized == 0U)
    {
        return;
    }

    FlowMeterService_Stop();
    if (FlowMeter_IsPowerEnabled() != 0U)
    {
        FlowMeter_SetPower(0U);
    }

    if (g_config.enabled == 0U)
    {
        g_state = FLOW_METER_APP_STATE_DISABLED;
    }
    else
    {
        g_state = FLOW_METER_APP_STATE_STOPPED;
    }
}

void FlowMeterApp_Process(void)
{
    uint32_t now_ms;

    if (g_initialized == 0U)
    {
        return;
    }

    if (g_state == FLOW_METER_APP_STATE_POWER_SETTLE)
    {
        now_ms = PlatformPort_GetMs();
        if (FlowMeterApp_Elapsed(now_ms,
                                 g_power_on_ms,
                                 g_config.power_settle_ms) != 0U)
        {
            if (FlowMeterService_Start() != FLOW_METER_SERVICE_OK)
            {
                FlowMeterApp_SetFault(FLOW_METER_APP_FAULT_SERVICE_START);
                return;
            }
            g_state = FLOW_METER_APP_STATE_RUNNING;
        }
        return;
    }

    if (g_state == FLOW_METER_APP_STATE_RUNNING)
    {
        FlowMeterService_Process();
    }
}

FlowMeterApp_State_T FlowMeterApp_GetState(void)
{
    return g_state;
}

FlowMeterApp_Fault_T FlowMeterApp_GetFault(void)
{
    return g_fault;
}

void FlowMeterApp_GetStatus(FlowMeterApp_Status_T *status)
{
    uint8_t level = 0U;

    if (status == 0)
    {
        return;
    }

    if (g_initialized != 0U)
    {
        level = FlowMeter_ReadInsertDetect();
    }

    status->state = g_state;
    status->fault = g_fault;
    status->enabled = (g_initialized != 0U) ? g_config.enabled : 0U;
    status->power_enabled = FlowMeter_IsPowerEnabled();
    status->insert_detect_level = level;
    status->meter_present = (uint8_t)((g_initialized != 0U) &&
                                      (g_config.enabled != 0U) &&
                                      (level == g_config.insert_present_level));
    status->power_on_ms = g_power_on_ms;
}
