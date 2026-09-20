#ifndef BSP_SELFTEST_H
#define BSP_SELFTEST_H

#include <stdint.h>

typedef struct
{
    uint8_t charger_detected;
    uint8_t charger_status_ok;
    uint8_t adc_ok;
    uint8_t failures;
} BSP_SelfTestResult_T;

BSP_SelfTestResult_T BSP_SelfTest_Run(void);
void BSP_SelfTest_DumpUartStats(void);

#endif /* BSP_SELFTEST_H */
