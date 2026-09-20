#ifndef BSP_ADC_H
#define BSP_ADC_H

#include <stdint.h>

typedef enum
{
    BSP_ADC_INPUT6 = 6,
    BSP_ADC_INPUT7 = 7,
    BSP_ADC_INPUT8 = 8,
    BSP_ADC_INPUT9 = 9
} BSP_AdcInput_T;

void BSP_Adc_Init(void);
uint8_t BSP_Adc_IsReady(void);
int BSP_Adc_ReadRawChecked(BSP_AdcInput_T input, uint16_t *raw);
int BSP_Adc_ReadAverageRaw(BSP_AdcInput_T input, uint8_t samples, uint16_t *raw);
uint16_t BSP_Adc_ReadRaw(BSP_AdcInput_T input);
uint32_t BSP_Adc_RawToMillivolts(uint16_t raw, uint32_t vdda_mv);
uint32_t BSP_Adc_ReadMillivolts(BSP_AdcInput_T input, uint32_t vdda_mv);

#endif /* BSP_ADC_H */
