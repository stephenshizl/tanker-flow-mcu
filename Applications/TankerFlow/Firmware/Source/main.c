#include "main.h"
#include "apm32f0xx.h"
#include "bsp_debug_uart.h"
#include "bsp_gpio.h"
#include "bsp_peripherals.h"
#include "bsp_selftest.h"
#include "bsp_tick.h"
#include "flow_meter_app.h"
#include "flow_meter_board_config.h"
#include "gnss.h"
#include "modem_4g.h"

static void App_FlowMeterLogState(FlowMeterApp_State_T state)
{
    switch (state)
    {
        case FLOW_METER_APP_STATE_DISABLED:
            BSP_DebugUart_Write("[FLOW] state=DISABLED; board polarities must be confirmed before enable\r\n");
            break;
        case FLOW_METER_APP_STATE_STOPPED:
            BSP_DebugUart_Write("[FLOW] state=STOPPED\r\n");
            break;
        case FLOW_METER_APP_STATE_POWER_SETTLE:
            BSP_DebugUart_Write("[FLOW] state=POWER_SETTLE\r\n");
            break;
        case FLOW_METER_APP_STATE_RUNNING:
            BSP_DebugUart_Write("[FLOW] state=RUNNING; non-blocking ZELZ polling active\r\n");
            break;
        case FLOW_METER_APP_STATE_FAULT:
            BSP_DebugUart_Write("[FLOW] state=FAULT code=");
            BSP_DebugUart_WriteUInt32((uint32_t)FlowMeterApp_GetFault());
            BSP_DebugUart_Write("\r\n");
            break;
        default:
            BSP_DebugUart_Write("[FLOW] state=UNINITIALIZED\r\n");
            break;
    }
}

static void App_FlowMeterBringupInit(void)
{
    FlowMeterApp_Config_T config;
    int result;

    config.enabled = FLOW_METER_BOARD_BRINGUP_ENABLE;
    config.rs485_tx_enable_level = FLOW_METER_BOARD_RS485_TX_ENABLE_LEVEL;
    config.rs485_tx_disable_level = FLOW_METER_BOARD_RS485_TX_DISABLE_LEVEL;
    config.rs485_rx_enable_level = FLOW_METER_BOARD_RS485_RX_ENABLE_LEVEL;
    config.rs485_rx_disable_level = FLOW_METER_BOARD_RS485_RX_DISABLE_LEVEL;
    config.inflow_level = FLOW_METER_BOARD_INFLOW_LEVEL;
    config.insert_present_level = FLOW_METER_BOARD_INSERT_PRESENT_LEVEL;
    config.slave_address = FLOW_METER_BOARD_SLAVE_ADDRESS;
    config.max_retries = FLOW_METER_BOARD_MAX_RETRIES;
    config.power_settle_ms = FLOW_METER_BOARD_POWER_SETTLE_MS;
    config.poll_interval_ms = FLOW_METER_BOARD_POLL_INTERVAL_MS;
    config.response_timeout_ms = FLOW_METER_BOARD_RESPONSE_TIMEOUT_MS;

    result = FlowMeterApp_Init(&config);
    if (result != FLOW_METER_APP_OK)
    {
        BSP_DebugUart_Write("[FLOW] bring-up config rejected; meter power remains off\r\n");
        App_FlowMeterLogState(FlowMeterApp_GetState());
        return;
    }

    if (config.enabled == 0U)
    {
        App_FlowMeterLogState(FlowMeterApp_GetState());
        return;
    }

    result = FlowMeterApp_Start();
    if (result != FLOW_METER_APP_OK)
    {
        BSP_DebugUart_Write("[FLOW] start rejected; meter power remains safe\r\n");
    }
    App_FlowMeterLogState(FlowMeterApp_GetState());
}

int main(void)
{
    uint32_t heartbeat_start_ms;
    FlowMeterApp_State_T flow_state;

    SystemClockConfig();
    BSP_Tick_Init();
    BSP_DebugUart_Init();

    BSP_DebugUart_Write("\r\n[BOOT] " APP_NAME " " APP_VERSION "\r\n");
    BSP_DebugUart_Write("[BSP ] APM32F030xC clock=48MHz tick=1ms\r\n");
    BSP_DebugUart_Write("[UART] USART1 debug  PA9/PA10   115200 8N1 IRQ\r\n");

    BSP_Peripherals_Init();
    GNSS_Init();
    Modem4G_Init();
    Modem4G_Start();
    App_FlowMeterBringupInit();
    flow_state = FlowMeterApp_GetState();

    BSP_DebugUart_Write("[UART] USART2 BT     PA2/PA3     115200 8N1 IRQ\r\n");
    BSP_DebugUart_Write("[UART] USART3 4G     PB10/PB11   115200 8N1 DMA+IDLE\r\n");
    BSP_DebugUart_Write("[UART] USART4 GNSS   PA0/PA1       9600 8N1 DMA+IDLE\r\n");
    BSP_DebugUart_Write("[UART] USART5 FLOW   PB3/PB4       9600 8E1 IRQ+TMR16\r\n");
    BSP_DebugUart_Write("[I2C ] I2C1 PB8/PB9 100kHz, SGM41511 driver enabled\r\n");
    BSP_DebugUart_Write("[ADC ] ADC_IN6..9 raw/average/pin-mV API initialized\r\n");
    BSP_DebugUart_Write("[GPIO] CHARGE_INT falling-edge IRQ; external controls remain safe\r\n");
    BSP_DebugUart_Write("[GNSS] NMEA stream parser enabled: RMC/GGA, integer coordinates\r\n");
    BSP_DebugUart_Write("[4G  ] MC610 async power/AT/SIM/signal/registration state machine started\r\n");
    BSP_DebugUart_Write("[PASS] phase-3E-F application bring-up integration initialized\r\n");

    (void)BSP_SelfTest_Run();

    heartbeat_start_ms = BSP_Tick_GetMs();
    while (1)
    {
        GNSS_Process();
        Modem4G_Process();
        FlowMeterApp_Process();

        if (flow_state != FlowMeterApp_GetState())
        {
            flow_state = FlowMeterApp_GetState();
            App_FlowMeterLogState(flow_state);
        }

        if (BSP_ChargerInterrupt_Consume() != 0U)
        {
            BSP_DebugUart_Write("[EVT ] SGM41511 nINT count=");
            BSP_DebugUart_WriteUInt32(BSP_ChargerInterrupt_Count());
            BSP_DebugUart_Write("\r\n");
        }

        if (BSP_Tick_Elapsed(heartbeat_start_ms, APP_HEARTBEAT_PERIOD_MS) != 0U)
        {
            heartbeat_start_ms = BSP_Tick_GetMs();
            BSP_DebugUart_Write("[ALIVE] 1s\r\n");
        }
    }
}
