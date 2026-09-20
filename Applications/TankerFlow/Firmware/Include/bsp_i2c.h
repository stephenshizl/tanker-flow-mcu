#ifndef BSP_I2C_H
#define BSP_I2C_H

#include <stdint.h>

typedef enum
{
    BSP_I2C_OK = 0,
    BSP_I2C_ERROR_TIMEOUT = -1,
    BSP_I2C_ERROR_NACK = -2,
    BSP_I2C_ERROR_INVALID_ARG = -3,
    BSP_I2C_ERROR_BUS = -4,
    BSP_I2C_ERROR_ARBITRATION = -5,
    BSP_I2C_ERROR_OVERRUN = -6
} BSP_I2C_Result_T;

void BSP_I2C1_Init(void);
int BSP_I2C1_WriteRegister(uint8_t address7, uint8_t reg, uint8_t value);
int BSP_I2C1_ReadRegister(uint8_t address7, uint8_t reg, uint8_t *value);
int BSP_I2C1_ReadRegisters(uint8_t address7, uint8_t reg, uint8_t *data, uint8_t length);
uint32_t BSP_I2C1_ErrorCount(void);

/* Raw SGM41511 register wrappers retained for low-level bench access. */
int BSP_Charger_WriteRegister(uint8_t reg, uint8_t value);
int BSP_Charger_ReadRegister(uint8_t reg, uint8_t *value);

#endif /* BSP_I2C_H */
