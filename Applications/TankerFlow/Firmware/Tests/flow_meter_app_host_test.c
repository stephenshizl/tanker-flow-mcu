#include <stdio.h>
#include <string.h>

#include "flow_meter_app.h"
#include "flow_meter.h"
#include "flow_meter_service.h"
#include "platform_port.h"

static uint32_t g_now_ms;
static uint8_t g_flow_power;
static uint8_t g_insert_level;
static unsigned g_flow_init_calls;
static unsigned g_rs485_config_calls;
static unsigned g_service_init_calls;
static unsigned g_service_start_calls;
static unsigned g_service_stop_calls;
static unsigned g_service_process_calls;
static int g_rs485_config_result;
static int g_service_init_result;
static int g_service_start_result;

static int Expect(int condition, const char *message)
{
    if (!condition)
    {
        printf("FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

static void ResetStubs(void)
{
    g_now_ms = 0U;
    g_flow_power = 0U;
    g_insert_level = 0U;
    g_flow_init_calls = 0U;
    g_rs485_config_calls = 0U;
    g_service_init_calls = 0U;
    g_service_start_calls = 0U;
    g_service_stop_calls = 0U;
    g_service_process_calls = 0U;
    g_rs485_config_result = FLOW_METER_OK;
    g_service_init_result = FLOW_METER_SERVICE_OK;
    g_service_start_result = FLOW_METER_SERVICE_OK;
}

static FlowMeterApp_Config_T ValidConfig(void)
{
    FlowMeterApp_Config_T config;

    memset(&config, 0, sizeof(config));
    config.enabled = 1U;
    config.rs485_tx_enable_level = 1U;
    config.rs485_tx_disable_level = 0U;
    config.rs485_rx_enable_level = 0U;
    config.rs485_rx_disable_level = 1U;
    config.inflow_level = 1U;
    config.insert_present_level = 0U;
    config.slave_address = 1U;
    config.max_retries = 2U;
    config.power_settle_ms = 1000U;
    config.poll_interval_ms = 1000U;
    config.response_timeout_ms = 300U;
    return config;
}

uint32_t PlatformPort_GetMs(void)
{
    return g_now_ms;
}

void FlowMeter_Init(void)
{
    g_flow_init_calls++;
    g_flow_power = 0U;
}

int FlowMeter_ConfigureRs485(const FlowMeter_Rs485Config_T *config)
{
    (void)config;
    g_rs485_config_calls++;
    return g_rs485_config_result;
}

void FlowMeter_SetPower(uint8_t enable)
{
    g_flow_power = (enable != 0U) ? 1U : 0U;
}

uint8_t FlowMeter_IsPowerEnabled(void)
{
    return g_flow_power;
}

uint8_t FlowMeter_ReadInsertDetect(void)
{
    return g_insert_level;
}

int FlowMeterService_Init(const FlowMeterService_Config_T *config)
{
    (void)config;
    g_service_init_calls++;
    return g_service_init_result;
}

int FlowMeterService_Start(void)
{
    g_service_start_calls++;
    return g_service_start_result;
}

void FlowMeterService_Stop(void)
{
    g_service_stop_calls++;
}

void FlowMeterService_Process(void)
{
    g_service_process_calls++;
}

static int TestDisabledProfileIsSafe(void)
{
    FlowMeterApp_Config_T config;
    FlowMeterApp_Status_T status;
    int ok = 1;

    ResetStubs();
    memset(&config, 0, sizeof(config));
    config.enabled = 0U;
    config.rs485_tx_enable_level = 0xFFU;
    config.rs485_tx_disable_level = 0xFFU;
    config.rs485_rx_enable_level = 0xFFU;
    config.rs485_rx_disable_level = 0xFFU;
    config.inflow_level = 0xFFU;
    config.insert_present_level = 0xFFU;

    ok &= Expect(FlowMeterApp_Init(&config) == FLOW_METER_APP_OK,
                 "disabled profile accepted");
    ok &= Expect(FlowMeterApp_GetState() == FLOW_METER_APP_STATE_DISABLED,
                 "disabled state");
    ok &= Expect(g_flow_init_calls == 1U, "software driver reset is allowed");
    ok &= Expect(g_rs485_config_calls == 0U, "disabled profile must not configure RS485 pins");
    ok &= Expect(g_service_init_calls == 0U, "disabled profile must not initialize service");
    ok &= Expect(g_flow_power == 0U, "disabled profile keeps meter power off");
    ok &= Expect(FlowMeterApp_Start() == FLOW_METER_APP_ERR_DISABLED,
                 "disabled profile cannot start");

    FlowMeterApp_GetStatus(&status);
    ok &= Expect(status.enabled == 0U, "status reports disabled");
    ok &= Expect(status.power_enabled == 0U, "status reports power off");
    return ok;
}

static int TestUnknownPolarityRejected(void)
{
    FlowMeterApp_Config_T config = ValidConfig();
    int ok = 1;

    ResetStubs();
    config.inflow_level = 0xFFU;
    ok &= Expect(FlowMeterApp_Init(&config) == FLOW_METER_APP_ERR_INVALID_ARG,
                 "unknown enabled polarity rejected");
    ok &= Expect(FlowMeterApp_GetState() == FLOW_METER_APP_STATE_FAULT,
                 "invalid config enters fault");
    ok &= Expect(FlowMeterApp_GetFault() == FLOW_METER_APP_FAULT_INVALID_CONFIG,
                 "invalid config fault reason");
    ok &= Expect(g_flow_init_calls == 0U, "invalid enabled config does not touch driver");
    ok &= Expect(g_flow_power == 0U, "invalid config keeps power off");
    return ok;
}

static int TestNonBlockingPowerSettleAndRun(void)
{
    FlowMeterApp_Config_T config = ValidConfig();
    FlowMeterApp_Status_T status;
    int ok = 1;

    ResetStubs();
    g_insert_level = config.insert_present_level;

    ok &= Expect(FlowMeterApp_Init(&config) == FLOW_METER_APP_OK,
                 "valid config initialized");
    ok &= Expect(g_rs485_config_calls == 1U, "RS485 configured once");
    ok &= Expect(g_service_init_calls == 1U, "service configured once");
    ok &= Expect(FlowMeterApp_GetState() == FLOW_METER_APP_STATE_STOPPED,
                 "initialized state stopped");

    g_now_ms = 100U;
    ok &= Expect(FlowMeterApp_Start() == FLOW_METER_APP_OK, "start accepted");
    ok &= Expect(g_flow_power == 1U, "start enables meter power");
    ok &= Expect(FlowMeterApp_GetState() == FLOW_METER_APP_STATE_POWER_SETTLE,
                 "non-blocking power settle state");

    g_now_ms = 1099U;
    FlowMeterApp_Process();
    ok &= Expect(g_service_start_calls == 0U, "service not started before settle deadline");

    g_now_ms = 1100U;
    FlowMeterApp_Process();
    ok &= Expect(g_service_start_calls == 1U, "service starts at settle deadline");
    ok &= Expect(FlowMeterApp_GetState() == FLOW_METER_APP_STATE_RUNNING,
                 "state becomes running");

    FlowMeterApp_Process();
    ok &= Expect(g_service_process_calls == 1U, "running state forwards foreground process");

    FlowMeterApp_GetStatus(&status);
    ok &= Expect(status.meter_present == 1U, "confirmed DET_INSERT polarity maps to present");
    ok &= Expect(status.power_on_ms == 100U, "status keeps power-on timestamp");

    FlowMeterApp_Stop();
    ok &= Expect(g_flow_power == 0U, "stop removes meter power");
    ok &= Expect(FlowMeterApp_GetState() == FLOW_METER_APP_STATE_STOPPED,
                 "stop returns to stopped");
    return ok;
}

static int TestServiceStartFailureIsSafe(void)
{
    FlowMeterApp_Config_T config = ValidConfig();
    int ok = 1;

    ResetStubs();
    g_service_start_result = FLOW_METER_SERVICE_ERR_NOT_READY;
    config.power_settle_ms = 0U;

    ok &= Expect(FlowMeterApp_Init(&config) == FLOW_METER_APP_OK,
                 "failure test initializes");
    ok &= Expect(FlowMeterApp_Start() == FLOW_METER_APP_OK,
                 "failure test starts power settle");
    FlowMeterApp_Process();

    ok &= Expect(FlowMeterApp_GetState() == FLOW_METER_APP_STATE_FAULT,
                 "service-start failure enters fault");
    ok &= Expect(FlowMeterApp_GetFault() == FLOW_METER_APP_FAULT_SERVICE_START,
                 "service-start failure reason retained");
    ok &= Expect(g_flow_power == 0U, "fault path removes meter power");
    ok &= Expect(g_service_stop_calls != 0U, "fault path stops service");
    return ok;
}

int main(void)
{
    int ok = 1;

    ok &= TestDisabledProfileIsSafe();
    ok &= TestUnknownPolarityRejected();
    ok &= TestNonBlockingPowerSettleAndRun();
    ok &= TestServiceStartFailureIsSafe();

    if (!ok)
    {
        return 1;
    }

    printf("Flow meter app host tests: PASS\n");
    return 0;
}
