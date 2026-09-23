#include "bsp_gpio.h"

#include "board_pinmap.h"
#include "bsp_tick.h"
#include "apm32f0xx_eint.h"
#include "apm32f0xx_gpio.h"
#include "apm32f0xx_misc.h"
#include "apm32f0xx_rcm.h"
#include "apm32f0xx_syscfg.h"

static volatile uint32_t g_charger_interrupt_count;
static volatile uint8_t g_charger_interrupt_pending;

static volatile uint32_t g_flow_direction_interrupt_count;
static volatile uint32_t g_flow_direction_interrupt_timestamp_ms;
static volatile uint8_t g_flow_direction_interrupt_level;
static volatile uint8_t g_flow_direction_interrupt_pending;

static void GPIO_ConfigOutput(GPIO_T *port, uint16_t pin, uint8_t initial_high)
{
    GPIO_Config_T config;

    if (initial_high != 0U)
    {
        GPIO_SetBit(port, pin);
    }
    else
    {
        GPIO_ClearBit(port, pin);
    }

    GPIO_ConfigStructInit(&config);
    config.pin = pin;
    config.mode = GPIO_MODE_OUT;
    config.outtype = GPIO_OUT_TYPE_PP;
    config.speed = GPIO_SPEED_2MHz;
    config.pupd = GPIO_PUPD_NO;
    GPIO_Config(port, &config);
}

static void GPIO_ConfigInput(GPIO_T *port, uint16_t pin, GPIO_PUPD_T pull)
{
    GPIO_Config_T config;

    GPIO_ConfigStructInit(&config);
    config.pin = pin;
    config.mode = GPIO_MODE_IN;
    config.outtype = GPIO_OUT_TYPE_PP;
    config.speed = GPIO_SPEED_2MHz;
    config.pupd = pull;
    GPIO_Config(port, &config);
}


static void GPIO_ConfigFlowDirectionInterrupt(void)
{
    EINT_Config_T eint_config;

    g_flow_direction_interrupt_count = 0U;
    g_flow_direction_interrupt_timestamp_ms = 0U;
    g_flow_direction_interrupt_level = GPIO_ReadInputBit(BOARD_FLOW_DIR_PORT, BOARD_FLOW_DIR_PIN);
    g_flow_direction_interrupt_pending = 0U;

    SYSCFG_EINTLine(SYSCFG_PORT_GPIOB, SYSCFG_PIN_14);
    EINT_ConfigStructInit(&eint_config);
    eint_config.line = EINT_LINE14;
    eint_config.mode = EINT_MODE_INTERRUPT;
    eint_config.trigger = EINT_TRIGGER_ALL;
    eint_config.lineCmd = ENABLE;
    EINT_Config(&eint_config);
    EINT_ClearIntFlag(EINT_LINE14);
    NVIC_EnableIRQRequest(EINT4_15_IRQn, 3U);
}

static void GPIO_ConfigChargerInterrupt(void)
{
    EINT_Config_T eint_config;

    g_charger_interrupt_count = 0U;
    g_charger_interrupt_pending = 0U;

    SYSCFG_EINTLine(SYSCFG_PORT_GPIOA, SYSCFG_PIN_11);
    EINT_ConfigStructInit(&eint_config);
    eint_config.line = EINT_LINE11;
    eint_config.mode = EINT_MODE_INTERRUPT;
    eint_config.trigger = EINT_TRIGGER_FALLING;
    eint_config.lineCmd = ENABLE;
    EINT_Config(&eint_config);
    EINT_ClearIntFlag(EINT_LINE11);
    NVIC_EnableIRQRequest(EINT4_15_IRQn, 3U);
}

void BSP_BoardGpio_Init(void)
{
    RCM_EnableAHBPeriphClock(RCM_AHB_PERIPH_GPIOA | RCM_AHB_PERIPH_GPIOB);
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_SYSCFG);

    /* Safe power-up states: peripherals off, reset lines released where direct. */
    GPIO_ConfigOutput(BOARD_4G_RST_PORT, BOARD_4G_RST_PIN, 0U);
    GPIO_ConfigOutput(BOARD_4G_PWRKEY_PORT, BOARD_4G_PWRKEY_PIN, 0U);
    GPIO_ConfigInput(BOARD_4G_WAKE_IN_PORT, BOARD_4G_WAKE_IN_PIN, GPIO_PUPD_NO);
    GPIO_ConfigOutput(BOARD_4G_WAKE_OUT_PORT, BOARD_4G_WAKE_OUT_PIN, 0U);

    GPIO_ConfigOutput(BOARD_GNSS_EN_PORT, BOARD_GNSS_EN_PIN, 0U);
    GPIO_ConfigOutput(BOARD_GNSS_RST_PORT, BOARD_GNSS_RST_PIN, 1U);
    GPIO_ConfigOutput(BOARD_BT_EN_PORT, BOARD_BT_EN_PIN, 0U);
    GPIO_ConfigOutput(BOARD_FLOW_EN_PORT, BOARD_FLOW_EN_PIN, 0U);

    GPIO_ConfigOutput(BOARD_RS485_TXEN_PORT, BOARD_RS485_TXEN_PIN, 0U);
    GPIO_ConfigOutput(BOARD_RS485_RXEN_PORT, BOARD_RS485_RXEN_PIN, 0U);
    GPIO_ConfigInput(BOARD_FLOW_DIR_PORT, BOARD_FLOW_DIR_PIN, GPIO_PUPD_NO);
    GPIO_ConfigInput(BOARD_INSERT_PORT, BOARD_INSERT_PIN, GPIO_PUPD_NO);
    GPIO_ConfigFlowDirectionInterrupt();

    /* SGM41511 nCE is active-low. Default high keeps charging disabled during bring-up. */
    GPIO_ConfigOutput(BOARD_CHARGE_EN_PORT, BOARD_CHARGE_EN_PIN, 1U);
    GPIO_ConfigInput(BOARD_CHARGE_INT_PORT, BOARD_CHARGE_INT_PIN, GPIO_PUPD_PU);
    GPIO_ConfigChargerInterrupt();
}

void BSP_4G_ResetAssert(void)       { GPIO_SetBit(BOARD_4G_RST_PORT, BOARD_4G_RST_PIN); }
void BSP_4G_ResetRelease(void)      { GPIO_ClearBit(BOARD_4G_RST_PORT, BOARD_4G_RST_PIN); }
void BSP_4G_PowerKeyAssert(void)    { GPIO_SetBit(BOARD_4G_PWRKEY_PORT, BOARD_4G_PWRKEY_PIN); }
void BSP_4G_PowerKeyRelease(void)   { GPIO_ClearBit(BOARD_4G_PWRKEY_PORT, BOARD_4G_PWRKEY_PIN); }
uint8_t BSP_4G_WakeupIn_Read(void)  { return GPIO_ReadInputBit(BOARD_4G_WAKE_IN_PORT, BOARD_4G_WAKE_IN_PIN); }

void BSP_4G_WakeupOut_Write(uint8_t high)
{
    GPIO_WriteBitValue(BOARD_4G_WAKE_OUT_PORT, BOARD_4G_WAKE_OUT_PIN, high ? Bit_SET : Bit_RESET);
}

void BSP_Gnss_SetPower(uint8_t enable)
{
    GPIO_WriteBitValue(BOARD_GNSS_EN_PORT, BOARD_GNSS_EN_PIN, enable ? Bit_SET : Bit_RESET);
}

void BSP_Gnss_ResetAssert(void)  { GPIO_ClearBit(BOARD_GNSS_RST_PORT, BOARD_GNSS_RST_PIN); }
void BSP_Gnss_ResetRelease(void) { GPIO_SetBit(BOARD_GNSS_RST_PORT, BOARD_GNSS_RST_PIN); }

void BSP_Bluetooth_SetPower(uint8_t enable)
{
    GPIO_WriteBitValue(BOARD_BT_EN_PORT, BOARD_BT_EN_PIN, enable ? Bit_SET : Bit_RESET);
}

void BSP_Flow_SetPower(uint8_t enable)
{
    GPIO_WriteBitValue(BOARD_FLOW_EN_PORT, BOARD_FLOW_EN_PIN, enable ? Bit_SET : Bit_RESET);
}

void BSP_RS485_TxEnablePin_Write(uint8_t high)
{
    GPIO_WriteBitValue(BOARD_RS485_TXEN_PORT, BOARD_RS485_TXEN_PIN, high ? Bit_SET : Bit_RESET);
}

void BSP_RS485_RxEnablePin_Write(uint8_t high)
{
    GPIO_WriteBitValue(BOARD_RS485_RXEN_PORT, BOARD_RS485_RXEN_PIN, high ? Bit_SET : Bit_RESET);
}

uint8_t BSP_FlowDirection_Read(void) { return GPIO_ReadInputBit(BOARD_FLOW_DIR_PORT, BOARD_FLOW_DIR_PIN); }

uint32_t BSP_FlowDirectionInterrupt_Count(void)
{
    return g_flow_direction_interrupt_count;
}

uint8_t BSP_FlowDirectionEvent_Consume(uint32_t *sequence,
                                       uint32_t *timestamp_ms,
                                       uint8_t *level)
{
    uint8_t pending;

    NVIC_DisableIRQRequest(EINT4_15_IRQn);
    pending = g_flow_direction_interrupt_pending;
    if (pending != 0U)
    {
        if (sequence != 0)
        {
            *sequence = g_flow_direction_interrupt_count;
        }
        if (timestamp_ms != 0)
        {
            *timestamp_ms = g_flow_direction_interrupt_timestamp_ms;
        }
        if (level != 0)
        {
            *level = g_flow_direction_interrupt_level;
        }
        g_flow_direction_interrupt_pending = 0U;
    }
    NVIC_EnableIRQRequest(EINT4_15_IRQn, 3U);

    return pending;
}

void BSP_FlowDirectionInterrupt_IRQHandler(void)
{
    if (EINT_ReadIntFlag(EINT_LINE14) == SET)
    {
        EINT_ClearIntFlag(EINT_LINE14);
        g_flow_direction_interrupt_count++;
        g_flow_direction_interrupt_timestamp_ms = BSP_Tick_GetMs();
        g_flow_direction_interrupt_level = BSP_FlowDirection_Read();
        g_flow_direction_interrupt_pending = 1U;
    }
}

uint8_t BSP_InsertDetect_Read(void)   { return GPIO_ReadInputBit(BOARD_INSERT_PORT, BOARD_INSERT_PIN); }

void BSP_Charger_SetEnable(uint8_t enable)
{
    /* SGM41511 nCE: low=enabled, high=disabled. */
    GPIO_WriteBitValue(BOARD_CHARGE_EN_PORT, BOARD_CHARGE_EN_PIN, enable ? Bit_RESET : Bit_SET);
}

uint8_t BSP_ChargerInterrupt_Read(void)
{
    return GPIO_ReadInputBit(BOARD_CHARGE_INT_PORT, BOARD_CHARGE_INT_PIN);
}

uint32_t BSP_ChargerInterrupt_Count(void)
{
    return g_charger_interrupt_count;
}

uint8_t BSP_ChargerInterrupt_Consume(void)
{
    uint8_t pending;

    /* Avoid losing a new nINT edge between the foreground read and clear. */
    NVIC_DisableIRQRequest(EINT4_15_IRQn);
    pending = g_charger_interrupt_pending;
    g_charger_interrupt_pending = 0U;
    NVIC_EnableIRQRequest(EINT4_15_IRQn, 3U);
    return pending;
}

void BSP_ChargerInterrupt_IRQHandler(void)
{
    if (EINT_ReadIntFlag(EINT_LINE11) == SET)
    {
        EINT_ClearIntFlag(EINT_LINE11);
        g_charger_interrupt_count++;
        g_charger_interrupt_pending = 1U;
    }
}
