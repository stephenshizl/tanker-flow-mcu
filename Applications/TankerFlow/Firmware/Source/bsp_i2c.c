#include "bsp_i2c.h"

#include "board_pinmap.h"
#include "bsp_tick.h"
#include "apm32f0xx_gpio.h"
#include "apm32f0xx_i2c.h"
#include "apm32f0xx_rcm.h"

#define BSP_I2C_TIMEOUT_MS  (20U)

static volatile uint32_t g_i2c_error_count;

static void I2C_ClearErrorFlags(void)
{
    if (I2C_ReadStatusFlag(I2C1, I2C_FLAG_NACK) == SET)
    {
        I2C_ClearIntFlag(I2C1, I2C_INT_FLAG_NACK);
    }
    if (I2C_ReadStatusFlag(I2C1, I2C_FLAG_BUSERR) == SET)
    {
        I2C_ClearIntFlag(I2C1, I2C_INT_FLAG_BUSERR);
    }
    if (I2C_ReadStatusFlag(I2C1, I2C_FLAG_ALF) == SET)
    {
        I2C_ClearIntFlag(I2C1, I2C_INT_FLAG_ALF);
    }
    if (I2C_ReadStatusFlag(I2C1, I2C_FLAG_OVR) == SET)
    {
        I2C_ClearIntFlag(I2C1, I2C_INT_FLAG_OVR);
    }
    if (I2C_ReadStatusFlag(I2C1, I2C_FLAG_STOP) == SET)
    {
        I2C_ClearIntFlag(I2C1, I2C_INT_FLAG_STOP);
    }
}

static void I2C_AbortTransfer(void)
{
    if (I2C_ReadStatusFlag(I2C1, I2C_FLAG_BUSY) == SET)
    {
        I2C_EnableGenerateStop(I2C1);
    }
    I2C_ClearErrorFlags();
}

static int I2C_RecordError(int error)
{
    if (error != BSP_I2C_OK)
    {
        g_i2c_error_count++;
        I2C_AbortTransfer();
    }
    return error;
}

static int I2C_WaitFlag(I2C_FLAG_T flag, uint8_t expected)
{
    uint32_t start;

    start = BSP_Tick_GetMs();
    while (I2C_ReadStatusFlag(I2C1, flag) != expected)
    {
        if (I2C_ReadStatusFlag(I2C1, I2C_FLAG_NACK) == SET)
        {
            return BSP_I2C_ERROR_NACK;
        }
        if (I2C_ReadStatusFlag(I2C1, I2C_FLAG_BUSERR) == SET)
        {
            return BSP_I2C_ERROR_BUS;
        }
        if (I2C_ReadStatusFlag(I2C1, I2C_FLAG_ALF) == SET)
        {
            return BSP_I2C_ERROR_ARBITRATION;
        }
        if (I2C_ReadStatusFlag(I2C1, I2C_FLAG_OVR) == SET)
        {
            return BSP_I2C_ERROR_OVERRUN;
        }
        if (BSP_Tick_Elapsed(start, BSP_I2C_TIMEOUT_MS) != 0U)
        {
            return BSP_I2C_ERROR_TIMEOUT;
        }
    }
    return BSP_I2C_OK;
}

static int I2C_WaitBusIdle(void)
{
    return I2C_WaitFlag(I2C_FLAG_BUSY, RESET);
}

static void I2C_ClearStop(void)
{
    if (I2C_ReadStatusFlag(I2C1, I2C_FLAG_STOP) == SET)
    {
        I2C_ClearIntFlag(I2C1, I2C_INT_FLAG_STOP);
    }
}

void BSP_I2C1_Init(void)
{
    GPIO_Config_T gpio_config;
    I2C_Config_T i2c_config;

    g_i2c_error_count = 0U;

    RCM_EnableAHBPeriphClock(RCM_AHB_PERIPH_GPIOB);
    RCM_EnableAPB1PeriphClock(RCM_APB1_PERIPH_I2C1);
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_SYSCFG);

    GPIO_ConfigPinAF(BOARD_I2C1_SCL_PORT, BOARD_I2C1_SCL_SOURCE, BOARD_I2C1_SCL_AF);
    GPIO_ConfigPinAF(BOARD_I2C1_SDA_PORT, BOARD_I2C1_SDA_SOURCE, BOARD_I2C1_SDA_AF);

    GPIO_ConfigStructInit(&gpio_config);
    gpio_config.mode = GPIO_MODE_AF;
    gpio_config.speed = GPIO_SPEED_50MHz;
    gpio_config.outtype = GPIO_OUT_TYPE_OD;
    gpio_config.pupd = GPIO_PUPD_NO;
    gpio_config.pin = BOARD_I2C1_SCL_PIN;
    GPIO_Config(BOARD_I2C1_SCL_PORT, &gpio_config);
    gpio_config.pin = BOARD_I2C1_SDA_PIN;
    GPIO_Config(BOARD_I2C1_SDA_PORT, &gpio_config);

    I2C_Reset(I2C1);
    RCM_ConfigI2CCLK(RCM_I2C1CLK_SYSCLK);
    I2C_ConfigStructInit(&i2c_config);
    i2c_config.ack = I2C_ACK_ENABLE;
    i2c_config.ackaddress = I2C_ACK_ADDRESS_7BIT;
    i2c_config.address1 = 0U;
    i2c_config.analogfilter = I2C_ANALOG_FILTER_ENABLE;
    i2c_config.digitalfilter = I2C_DIGITAL_FILTER_0;
    i2c_config.mode = I2C_MODE_I2C;
    i2c_config.timing = 0xB0420F13U; /* 100 kHz with 48 MHz SYSCLK, SDK reference value. */
    I2C_Config(I2C1, &i2c_config);
    I2C_ClearErrorFlags();
    I2C_Enable(I2C1);
}

int BSP_I2C1_WriteRegister(uint8_t address7, uint8_t reg, uint8_t value)
{
    uint16_t address8;
    int status;

    status = I2C_WaitBusIdle();
    if (status != BSP_I2C_OK)
    {
        return I2C_RecordError(status);
    }
    I2C_ClearStop();
    address8 = (uint16_t)((uint16_t)address7 << 1U);

    I2C_HandlingTransfer(I2C1, address8, 2U, I2C_RELOAD_MODE_AUTOEND, I2C_GENERATE_START_WRITE);
    status = I2C_WaitFlag(I2C_FLAG_TXINT, SET);
    if (status != BSP_I2C_OK) return I2C_RecordError(status);
    I2C_TxData(I2C1, reg);
    status = I2C_WaitFlag(I2C_FLAG_TXINT, SET);
    if (status != BSP_I2C_OK) return I2C_RecordError(status);
    I2C_TxData(I2C1, value);
    status = I2C_WaitFlag(I2C_FLAG_STOP, SET);
    if (status != BSP_I2C_OK) return I2C_RecordError(status);
    I2C_ClearIntFlag(I2C1, I2C_INT_FLAG_STOP);
    return BSP_I2C_OK;
}

int BSP_I2C1_ReadRegisters(uint8_t address7, uint8_t reg, uint8_t *data, uint8_t length)
{
    uint16_t address8;
    uint8_t index;
    int status;

    if ((data == 0) || (length == 0U))
    {
        return BSP_I2C_ERROR_INVALID_ARG;
    }

    status = I2C_WaitBusIdle();
    if (status != BSP_I2C_OK)
    {
        return I2C_RecordError(status);
    }
    I2C_ClearStop();
    address8 = (uint16_t)((uint16_t)address7 << 1U);

    /* Register index write without STOP, followed by repeated START read. */
    I2C_HandlingTransfer(I2C1, address8, 1U, I2C_RELOAD_MODE_SOFTEND, I2C_GENERATE_START_WRITE);
    status = I2C_WaitFlag(I2C_FLAG_TXINT, SET);
    if (status != BSP_I2C_OK) return I2C_RecordError(status);
    I2C_TxData(I2C1, reg);
    status = I2C_WaitFlag(I2C_FLAG_TXCF, SET);
    if (status != BSP_I2C_OK) return I2C_RecordError(status);

    I2C_HandlingTransfer(I2C1, address8, length, I2C_RELOAD_MODE_AUTOEND, I2C_GENERATE_START_READ);
    for (index = 0U; index < length; index++)
    {
        status = I2C_WaitFlag(I2C_FLAG_RXBNE, SET);
        if (status != BSP_I2C_OK) return I2C_RecordError(status);
        data[index] = I2C_RxData(I2C1);
    }
    status = I2C_WaitFlag(I2C_FLAG_STOP, SET);
    if (status != BSP_I2C_OK) return I2C_RecordError(status);
    I2C_ClearIntFlag(I2C1, I2C_INT_FLAG_STOP);
    return BSP_I2C_OK;
}

int BSP_I2C1_ReadRegister(uint8_t address7, uint8_t reg, uint8_t *value)
{
    return BSP_I2C1_ReadRegisters(address7, reg, value, 1U);
}

uint32_t BSP_I2C1_ErrorCount(void)
{
    return g_i2c_error_count;
}

int BSP_Charger_WriteRegister(uint8_t reg, uint8_t value)
{
    return BSP_I2C1_WriteRegister(BOARD_CHARGER_I2C_ADDR_7BIT, reg, value);
}

int BSP_Charger_ReadRegister(uint8_t reg, uint8_t *value)
{
    return BSP_I2C1_ReadRegister(BOARD_CHARGER_I2C_ADDR_7BIT, reg, value);
}
