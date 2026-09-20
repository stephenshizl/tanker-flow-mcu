#ifndef BSP_I2C_H
#define BSP_I2C_H

#include <stdint.h>

void BSP_I2C1_Init(void);
int BSP_I2C1_WriteRegister(uint8_t address7, uint8_t reg, uint8_t value);
int BSP_I2C1_ReadRegister(uint8_t address7, uint8_t reg, uint8_t *value);
int BSP_I2C1_ReadRegisters(uint8_t address7, uint8_t reg, uint8_t *data, uint8_t length);

int BSP_Charger_WriteRegister(uint8_t reg, uint8_t value);
int BSP_Charger_ReadRegister(uint8_t reg, uint8_t *value);

#endif /* BSP_I2C_H */
