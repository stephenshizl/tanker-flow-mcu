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
uint16_t BSP_Adc_ReadRaw(BSP_AdcInput_T input);
uint32_t BSP_Adc_ReadMillivolts(BSP_AdcInput_T input, uint32_t vdda_mv);

#endif /* BSP_ADC_H */
