#include "bsp_selftest.h"

#include "bsp_adc.h"
#include "bsp_debug_uart.h"
#include "bsp_gpio.h"
#include "bsp_i2c.h"
#include "bsp_uart.h"
#include "sgm41511.h"

#define SELFTEST_ADC_SAMPLES       (8U)
#define SELFTEST_ADC_VDDA_MV       (3300U)

static void SelfTest_PrintDec(uint32_t value)
{
    BSP_DebugUart_WriteUInt32(value);
}

static void SelfTest_PrintHex8(uint8_t value)
{
    BSP_DebugUart_WriteHex8(value);
}

static void SelfTest_PrintAdc(BSP_AdcInput_T input, const char *name, uint8_t *ok)
{
    uint16_t raw;
    uint32_t mv;
    int result;

    result = BSP_Adc_ReadAverageRaw(input, SELFTEST_ADC_SAMPLES, &raw);
    BSP_DebugUart_Write("[SELF][ADC ] ");
    BSP_DebugUart_Write(name);
    if (result != 0)
    {
        BSP_DebugUart_Write(" read=FAIL\r\n");
        *ok = 0U;
        return;
    }

    mv = BSP_Adc_RawToMillivolts(raw, SELFTEST_ADC_VDDA_MV);
    BSP_DebugUart_Write(" raw=");
    SelfTest_PrintDec(raw);
    BSP_DebugUart_Write(" pin_mv=");
    SelfTest_PrintDec(mv);
    BSP_DebugUart_Write("\r\n");
}

static void SelfTest_PrintOneUart(const char *name, BSP_UartPort_T port)
{
    BSP_UartStats_T stats;

    BSP_Uart_GetStats(port, &stats);
    BSP_DebugUart_Write("[SELF][UART] ");
    BSP_DebugUart_Write(name);
    BSP_DebugUart_Write(" rx=");
    SelfTest_PrintDec(stats.rx_bytes);
    BSP_DebugUart_Write(" idle=");
    SelfTest_PrintDec(stats.idle_events);
    BSP_DebugUart_Write(" dma_full=");
    SelfTest_PrintDec(stats.dma_full_events);
    BSP_DebugUart_Write(" ovf=");
    SelfTest_PrintDec(stats.ring_overflow);
    BSP_DebugUart_Write("\r\n");
}

void BSP_SelfTest_DumpUartStats(void)
{
    SelfTest_PrintOneUart("DBG ", BSP_UART_DEBUG);
    SelfTest_PrintOneUart("BT  ", BSP_UART_BLUETOOTH);
    SelfTest_PrintOneUart("4G  ", BSP_UART_4G);
    SelfTest_PrintOneUart("GNSS", BSP_UART_GNSS);
    SelfTest_PrintOneUart("FLOW", BSP_UART_FLOW);
}

BSP_SelfTestResult_T BSP_SelfTest_Run(void)
{
    BSP_SelfTestResult_T test;
    SGM41511_DeviceInfo_T info;
    SGM41511_Status_T status;
    SGM41511_Fault_T fault;
    uint16_t input_limit_ma;
    uint16_t charge_current_ma;
    uint8_t adc_ok;
    int result;

    test.charger_detected = 0U;
    test.charger_status_ok = 0U;
    test.adc_ok = 0U;
    test.failures = 0U;

    BSP_DebugUart_Write("[SELF] phase-3A peripheral self-test start\r\n");
    BSP_DebugUart_Write("[SELF][GPIO] CHG_INT level=");
    SelfTest_PrintDec(BSP_ChargerInterrupt_Read());
    BSP_DebugUart_Write(" irq_count=");
    SelfTest_PrintDec(BSP_ChargerInterrupt_Count());
    BSP_DebugUart_Write("; charger nCE remains disabled by BSP safe state\r\n");

    result = SGM41511_Probe(&info);
    if (result != BSP_I2C_OK)
    {
        BSP_DebugUart_Write("[SELF][CHG ] SGM41511 I2C probe=FAIL err=");
        SelfTest_PrintDec((uint32_t)(-result));
        BSP_DebugUart_Write("\r\n");
        test.failures++;
    }
    else
    {
        BSP_DebugUart_Write("[SELF][CHG ] REG0B=0x");
        SelfTest_PrintHex8(info.raw);
        BSP_DebugUart_Write(" PN=");
        SelfTest_PrintDec(info.part_number);
        BSP_DebugUart_Write(" REV=");
        SelfTest_PrintDec(info.revision);
        BSP_DebugUart_Write(" device=");
        if (info.expected_device != 0U)
        {
            BSP_DebugUart_Write("SGM41511 PASS\r\n");
            test.charger_detected = 1U;
        }
        else
        {
            BSP_DebugUart_Write("UNEXPECTED FAIL\r\n");
            test.failures++;
        }

        result = SGM41511_GetStatus(&status);
        if (result == BSP_I2C_OK)
        {
            test.charger_status_ok = 1U;
            BSP_DebugUart_Write("[SELF][CHG ] REG08=0x");
            SelfTest_PrintHex8(status.reg08);
            BSP_DebugUart_Write(" REG0A=0x");
            SelfTest_PrintHex8(status.reg0a);
            BSP_DebugUart_Write(" VBUS=");
            SelfTest_PrintDec(status.vbus_status);
            BSP_DebugUart_Write(" CHG=");
            SelfTest_PrintDec((uint32_t)status.charge_state);
            BSP_DebugUart_Write(" PG=");
            SelfTest_PrintDec(status.power_good);
            BSP_DebugUart_Write(" VBUS_GD=");
            SelfTest_PrintDec(status.vbus_good);
            BSP_DebugUart_Write("\r\n");
        }
        else
        {
            BSP_DebugUart_Write("[SELF][CHG ] status read=FAIL\r\n");
            test.failures++;
        }

        result = SGM41511_ReadFault(&fault);
        if (result == BSP_I2C_OK)
        {
            BSP_DebugUart_Write("[SELF][CHG ] FAULT latched=0x");
            SelfTest_PrintHex8(fault.latched);
            BSP_DebugUart_Write(" current=0x");
            SelfTest_PrintHex8(fault.current);
            BSP_DebugUart_Write("\r\n");
        }
        else
        {
            BSP_DebugUart_Write("[SELF][CHG ] fault read=FAIL\r\n");
            test.failures++;
        }

        result = SGM41511_GetInputCurrentLimitMa(&input_limit_ma);
        if (result == BSP_I2C_OK)
        {
            BSP_DebugUart_Write("[SELF][CHG ] IINDPM=");
            SelfTest_PrintDec(input_limit_ma);
            BSP_DebugUart_Write("mA");
        }
        else
        {
            BSP_DebugUart_Write("[SELF][CHG ] IINDPM=READ_FAIL");
            test.failures++;
        }

        result = SGM41511_GetChargeCurrentMa(&charge_current_ma);
        if (result == BSP_I2C_OK)
        {
            BSP_DebugUart_Write(" ICHG=");
            SelfTest_PrintDec(charge_current_ma);
            BSP_DebugUart_Write("mA\r\n");
        }
        else
        {
            BSP_DebugUart_Write(" ICHG=READ_FAIL\r\n");
            test.failures++;
        }
    }

    adc_ok = BSP_Adc_IsReady();
    if (adc_ok == 0U)
    {
        BSP_DebugUart_Write("[SELF][ADC ] ADC ready=FAIL\r\n");
        test.failures++;
    }
    else
    {
        SelfTest_PrintAdc(BSP_ADC_INPUT6, "ADC_IN6", &adc_ok);
        SelfTest_PrintAdc(BSP_ADC_INPUT7, "ADC_IN7", &adc_ok);
        SelfTest_PrintAdc(BSP_ADC_INPUT8, "ADC_IN8", &adc_ok);
        SelfTest_PrintAdc(BSP_ADC_INPUT9, "ADC_IN9", &adc_ok);
        if (adc_ok != 0U)
        {
            test.adc_ok = 1U;
        }
        else
        {
            test.failures++;
        }
    }

    BSP_DebugUart_Write("[SELF][ADC ] pin_mv uses nominal VDDA=3300mV; external divider scaling is not assumed\r\n");
    BSP_SelfTest_DumpUartStats();

    if (test.failures == 0U)
    {
        BSP_DebugUart_Write("[SELF] phase-3A result=PASS\r\n");
    }
    else
    {
        BSP_DebugUart_Write("[SELF] phase-3A result=FAIL count=");
        SelfTest_PrintDec(test.failures);
        BSP_DebugUart_Write(" (boot continues for bench diagnosis)\r\n");
    }
    return test;
}
