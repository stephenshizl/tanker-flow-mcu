#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include <stdint.h>

void BSP_BoardGpio_Init(void);

void BSP_4G_ResetAssert(void);
void BSP_4G_ResetRelease(void);
void BSP_4G_PowerKeyAssert(void);
void BSP_4G_PowerKeyRelease(void);
uint8_t BSP_4G_WakeupIn_Read(void);
void BSP_4G_WakeupOut_Write(uint8_t high);

void BSP_Gnss_SetPower(uint8_t enable);
void BSP_Gnss_ResetAssert(void);
void BSP_Gnss_ResetRelease(void);
void BSP_Bluetooth_SetPower(uint8_t enable);
void BSP_Flow_SetPower(uint8_t enable);

/* Raw level setters: names intentionally describe MCU pins, not /RE/DE semantics. */
void BSP_RS485_TxEnablePin_Write(uint8_t high);
void BSP_RS485_RxEnablePin_Write(uint8_t high);

uint8_t BSP_FlowDirection_Read(void);
uint8_t BSP_InsertDetect_Read(void);

void BSP_Charger_SetEnable(uint8_t enable);
uint8_t BSP_ChargerInterrupt_Read(void);
uint32_t BSP_ChargerInterrupt_Count(void);
uint8_t BSP_ChargerInterrupt_Consume(void);
void BSP_ChargerInterrupt_IRQHandler(void);

#endif /* BSP_GPIO_H */
