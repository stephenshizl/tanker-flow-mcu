#include "bsp_adc.h"

#include "board_pinmap.h"
#include "apm32f0xx_adc.h"
#include "apm32f0xx_gpio.h"
#include "apm32f0xx_rcm.h"

static uint32_t Adc_ChannelFromInput(BSP_AdcInput_T input)
{
    switch (input)
    {
        case BSP_ADC_INPUT6: return ADC_CHANNEL_6;
        case BSP_ADC_INPUT7: return ADC_CHANNEL_7;
        case BSP_ADC_INPUT8: return ADC_CHANNEL_8;
        case BSP_ADC_INPUT9: return ADC_CHANNEL_9;
        default:             return 0U;
    }
}

static void Adc_ConfigAnalogPin(GPIO_T *port, uint16_t pin)
{
    GPIO_Config_T config;

    GPIO_ConfigStructInit(&config);
    config.pin = pin;
    config.mode = GPIO_MODE_AN;
    config.pupd = GPIO_PUPD_NO;
    GPIO_Config(port, &config);
}

void BSP_Adc_Init(void)
{
    ADC_Config_T config;
    uint32_t timeout;

    RCM_EnableAHBPeriphClock(RCM_AHB_PERIPH_GPIOA | RCM_AHB_PERIPH_GPIOB);
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_ADC1 | RCM_APB2_PERIPH_SYSCFG);

    Adc_ConfigAnalogPin(BOARD_ADC_IN6_PORT, BOARD_ADC_IN6_PIN);
    Adc_ConfigAnalogPin(BOARD_ADC_IN7_PORT, BOARD_ADC_IN7_PIN);
    Adc_ConfigAnalogPin(BOARD_ADC_IN8_PORT, BOARD_ADC_IN8_PIN);
    Adc_ConfigAnalogPin(BOARD_ADC_IN9_PORT, BOARD_ADC_IN9_PIN);

    ADC_Reset();
    ADC_ConfigStructInit(&config);
    config.resolution = ADC_RESOLUTION_12B;
    config.dataAlign = ADC_DATA_ALIGN_RIGHT;
    config.scanDir = ADC_SCAN_DIR_UPWARD;
    config.convMode = ADC_CONVERSION_SINGLE;
    config.extTrigConv = ADC_EXT_TRIG_CONV_TRG0;
    config.extTrigEdge = ADC_EXT_TRIG_EDGE_NONE;
    ADC_Config(&config);

    (void)ADC_ReadCalibrationFactor();
    ADC_Enable();

    timeout = 100000U;
    while ((ADC_ReadStatusFlag(ADC_FLAG_ADRDY) == RESET) && (timeout > 0U))
    {
        timeout--;
    }
}

uint16_t BSP_Adc_ReadRaw(BSP_AdcInput_T input)
{
    uint32_t channel;
    uint32_t timeout;

    channel = Adc_ChannelFromInput(input);
    if (channel == 0U)
    {
        return 0U;
    }

    /* ADC_ConfigChannel ORs CHSEL in this SDK, so select one channel explicitly. */
    ADC->CHSEL = 0U;
    ADC->SMPTIM = 0U;
    ADC_ConfigChannel(channel, ADC_SAMPLE_TIME_239_5);
    ADC_ClearStatusFlag(ADC_FLAG_CC);
    ADC_StartConversion();

    timeout = 100000U;
    while ((ADC_ReadStatusFlag(ADC_FLAG_CC) == RESET) && (timeout > 0U))
    {
        timeout--;
    }

    if (timeout == 0U)
    {
        return 0U;
    }
    return (uint16_t)ADC_ReadConversionValue();
}

uint32_t BSP_Adc_ReadMillivolts(BSP_AdcInput_T input, uint32_t vdda_mv)
{
    uint32_t raw;

    raw = BSP_Adc_ReadRaw(input);
    return (raw * vdda_mv) / 4095U;
}
