#ifndef SGM41511_H
#define SGM41511_H

#include <stdint.h>

#define SGM41511_I2C_ADDRESS_7BIT       (0x6BU)

#define SGM41511_REG_INPUT_CTRL         (0x00U)
#define SGM41511_REG_POWER_ON_CONFIG    (0x01U)
#define SGM41511_REG_CHARGE_CURRENT     (0x02U)
#define SGM41511_REG_STATUS             (0x08U)
#define SGM41511_REG_FAULT              (0x09U)
#define SGM41511_REG_STATUS2            (0x0AU)
#define SGM41511_REG_PART_INFO          (0x0BU)

typedef enum
{
    SGM41511_CHARGE_DISABLED = 0,
    SGM41511_CHARGE_PRECHARGE = 1,
    SGM41511_CHARGE_FAST = 2,
    SGM41511_CHARGE_DONE = 3
} SGM41511_ChargeState_T;

typedef struct
{
    uint8_t raw;
    uint8_t part_number;
    uint8_t revision;
    uint8_t sgmpart;
    uint8_t expected_device;
} SGM41511_DeviceInfo_T;

typedef struct
{
    uint8_t reg08;
    uint8_t reg0a;
    uint8_t vbus_status;
    SGM41511_ChargeState_T charge_state;
    uint8_t power_good;
    uint8_t thermal_regulation;
    uint8_t vsys_regulation;
    uint8_t vbus_good;
    uint8_t vindpm_active;
    uint8_t iindpm_active;
    uint8_t acov_active;
} SGM41511_Status_T;

typedef struct
{
    uint8_t latched;
    uint8_t current;
} SGM41511_Fault_T;

int SGM41511_ReadRegister(uint8_t reg, uint8_t *value);
int SGM41511_WriteRegister(uint8_t reg, uint8_t value);
int SGM41511_Probe(SGM41511_DeviceInfo_T *info);

int SGM41511_SetChargeEnable(uint8_t enable);
int SGM41511_SetInputCurrentLimitMa(uint16_t requested_ma, uint16_t *applied_ma);
int SGM41511_GetInputCurrentLimitMa(uint16_t *current_ma);
int SGM41511_SetChargeCurrentMa(uint16_t requested_ma, uint16_t *applied_ma);
int SGM41511_GetChargeCurrentMa(uint16_t *current_ma);

int SGM41511_GetStatus(SGM41511_Status_T *status);
int SGM41511_ReadFault(SGM41511_Fault_T *fault);

#endif /* SGM41511_H */
