#include "platform_port.h"

#include "bsp_gpio.h"
#include "bsp_tick.h"
#include "bsp_uart.h"

uint32_t PlatformPort_GetMs(void)
{
    return BSP_Tick_GetMs();
}

uint16_t PlatformPort_ModemRead(uint8_t *data, uint16_t max_length)
{
    return BSP_Uart_Read(BSP_UART_4G, data, max_length);
}

uint16_t PlatformPort_ModemWrite(const uint8_t *data, uint16_t length)
{
    BSP_Uart_Write(BSP_UART_4G, data, length);
    return length;
}

void PlatformPort_ModemResetAssert(void)
{
    BSP_4G_ResetAssert();
}

void PlatformPort_ModemResetRelease(void)
{
    BSP_4G_ResetRelease();
}

void PlatformPort_ModemPowerKeyAssert(void)
{
    BSP_4G_PowerKeyAssert();
}

void PlatformPort_ModemPowerKeyRelease(void)
{
    BSP_4G_PowerKeyRelease();
}

uint16_t PlatformPort_GnssRead(uint8_t *data, uint16_t max_length)
{
    return BSP_Uart_Read(BSP_UART_GNSS, data, max_length);
}

int PlatformPort_FlowReadFrame(uint8_t *data, uint16_t capacity)
{
    return BSP_Flow_ReadFrame(data, capacity);
}

uint16_t PlatformPort_FlowWrite(const uint8_t *data, uint16_t length)
{
    BSP_Uart_Write(BSP_UART_FLOW, data, length);
    return length;
}

void PlatformPort_FlowSetPower(uint8_t enable)
{
    BSP_Flow_SetPower(enable);
}

void PlatformPort_FlowTxEnablePinWrite(uint8_t high)
{
    BSP_RS485_TxEnablePin_Write(high);
}

void PlatformPort_FlowRxEnablePinWrite(uint8_t high)
{
    BSP_RS485_RxEnablePin_Write(high);
}

uint8_t PlatformPort_FlowDirectionRead(void)
{
    return BSP_FlowDirection_Read();
}

uint8_t PlatformPort_FlowDirectionEventConsume(PlatformPort_FlowDirectionEvent_T *event)
{
    if (event == 0)
    {
        return 0U;
    }

    return BSP_FlowDirectionEvent_Consume(&event->sequence,
                                          &event->timestamp_ms,
                                          &event->level);
}

uint8_t PlatformPort_FlowInsertDetectRead(void)
{
    return BSP_InsertDetect_Read();
}
