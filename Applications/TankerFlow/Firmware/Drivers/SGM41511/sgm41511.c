#include "sgm41511.h"

#include "bsp_gpio.h"
#include "bsp_i2c.h"

#define SGM41511_REG00_IINDPM_MASK      (0x1FU)
#define SGM41511_REG01_CHG_CONFIG       (0x10U)
#define SGM41511_REG02_ICHG_MASK        (0x3FU)

#define SGM41511_REG08_VBUS_MASK        (0xE0U)
#define SGM41511_REG08_VBUS_SHIFT       (5U)
#define SGM41511_REG08_CHARGE_MASK      (0x18U)
#define SGM41511_REG08_CHARGE_SHIFT     (3U)
#define SGM41511_REG08_POWER_GOOD       (0x04U)
#define SGM41511_REG08_THERMAL          (0x02U)
#define SGM41511_REG08_VSYS             (0x01U)

#define SGM41511_REG0A_VBUS_GOOD        (0x80U)
#define SGM41511_REG0A_VINDPM           (0x40U)
#define SGM41511_REG0A_IINDPM           (0x20U)
#define SGM41511_REG0A_ACOV             (0x04U)

#define SGM41511_REG0B_PN_MASK          (0x78U)
#define SGM41511_REG0B_PN_SHIFT         (3U)
#define SGM41511_REG0B_SGMPART          (0x04U)
#define SGM41511_REG0B_REV_MASK         (0x03U)
#define SGM41511_EXPECTED_PN            (0x02U)

#define SGM41511_IINDPM_MIN_MA          (100U)
#define SGM41511_IINDPM_MAX_MA          (3200U)
#define SGM41511_IINDPM_STEP_MA         (100U)
#define SGM41511_ICHG_MAX_MA            (3000U)
#define SGM41511_ICHG_STEP_MA           (60U)
#define SGM41511_ICHG_MAX_CODE          (50U)

static int SGM41511_UpdateBits(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current;
    int result;

    result = SGM41511_ReadRegister(reg, &current);
    if (result != BSP_I2C_OK)
    {
        return result;
    }

    current = (uint8_t)((current & (uint8_t)(~mask)) | (value & mask));
    return SGM41511_WriteRegister(reg, current);
}

int SGM41511_ReadRegister(uint8_t reg, uint8_t *value)
{
    if (value == 0)
    {
        return BSP_I2C_ERROR_INVALID_ARG;
    }
    return BSP_I2C1_ReadRegister(SGM41511_I2C_ADDRESS_7BIT, reg, value);
}

int SGM41511_WriteRegister(uint8_t reg, uint8_t value)
{
    return BSP_I2C1_WriteRegister(SGM41511_I2C_ADDRESS_7BIT, reg, value);
}

int SGM41511_Probe(SGM41511_DeviceInfo_T *info)
{
    uint8_t value;
    int result;

    if (info == 0)
    {
        return BSP_I2C_ERROR_INVALID_ARG;
    }

    result = SGM41511_ReadRegister(SGM41511_REG_PART_INFO, &value);
    if (result != BSP_I2C_OK)
    {
        info->raw = 0U;
        info->part_number = 0U;
        info->revision = 0U;
        info->sgmpart = 0U;
        info->expected_device = 0U;
        return result;
    }

    info->raw = value;
    info->part_number = (uint8_t)((value & SGM41511_REG0B_PN_MASK) >> SGM41511_REG0B_PN_SHIFT);
    info->revision = (uint8_t)(value & SGM41511_REG0B_REV_MASK);
    info->sgmpart = ((value & SGM41511_REG0B_SGMPART) != 0U) ? 1U : 0U;
    info->expected_device = ((info->part_number == SGM41511_EXPECTED_PN) &&
                             (info->sgmpart != 0U)) ? 1U : 0U;
    return BSP_I2C_OK;
}

int SGM41511_SetChargeEnable(uint8_t enable)
{
    int result;

    if (enable == 0U)
    {
        /* Disable at the hardware nCE pin before changing the I2C bit. */
        BSP_Charger_SetEnable(0U);
        return SGM41511_UpdateBits(SGM41511_REG_POWER_ON_CONFIG,
                                   SGM41511_REG01_CHG_CONFIG,
                                   0U);
    }

    /* Keep nCE inactive until the charger accepts CHG_CONFIG. */
    BSP_Charger_SetEnable(0U);
    result = SGM41511_UpdateBits(SGM41511_REG_POWER_ON_CONFIG,
                                 SGM41511_REG01_CHG_CONFIG,
                                 SGM41511_REG01_CHG_CONFIG);
    if (result == BSP_I2C_OK)
    {
        BSP_Charger_SetEnable(1U);
    }
    return result;
}

int SGM41511_SetInputCurrentLimitMa(uint16_t requested_ma, uint16_t *applied_ma)
{
    uint16_t limited_ma;
    uint8_t code;
    int result;

    limited_ma = requested_ma;
    if (limited_ma < SGM41511_IINDPM_MIN_MA)
    {
        limited_ma = SGM41511_IINDPM_MIN_MA;
    }
    if (limited_ma > SGM41511_IINDPM_MAX_MA)
    {
        limited_ma = SGM41511_IINDPM_MAX_MA;
    }

    /* Round down so the programmed input limit never exceeds the request. */
    code = (uint8_t)((limited_ma - SGM41511_IINDPM_MIN_MA) / SGM41511_IINDPM_STEP_MA);
    limited_ma = (uint16_t)(SGM41511_IINDPM_MIN_MA +
                            ((uint16_t)code * SGM41511_IINDPM_STEP_MA));

    result = SGM41511_UpdateBits(SGM41511_REG_INPUT_CTRL,
                                 SGM41511_REG00_IINDPM_MASK,
                                 code);
    if ((result == BSP_I2C_OK) && (applied_ma != 0))
    {
        *applied_ma = limited_ma;
    }
    return result;
}

int SGM41511_GetInputCurrentLimitMa(uint16_t *current_ma)
{
    uint8_t value;
    int result;

    if (current_ma == 0)
    {
        return BSP_I2C_ERROR_INVALID_ARG;
    }

    result = SGM41511_ReadRegister(SGM41511_REG_INPUT_CTRL, &value);
    if (result != BSP_I2C_OK)
    {
        return result;
    }

    value &= SGM41511_REG00_IINDPM_MASK;
    *current_ma = (uint16_t)(SGM41511_IINDPM_MIN_MA +
                             ((uint16_t)value * SGM41511_IINDPM_STEP_MA));
    return BSP_I2C_OK;
}

int SGM41511_SetChargeCurrentMa(uint16_t requested_ma, uint16_t *applied_ma)
{
    uint16_t limited_ma;
    uint8_t code;
    int result;

    limited_ma = requested_ma;
    if (limited_ma > SGM41511_ICHG_MAX_MA)
    {
        limited_ma = SGM41511_ICHG_MAX_MA;
    }

    code = (uint8_t)(limited_ma / SGM41511_ICHG_STEP_MA);
    if (code > SGM41511_ICHG_MAX_CODE)
    {
        code = SGM41511_ICHG_MAX_CODE;
    }
    limited_ma = (uint16_t)((uint16_t)code * SGM41511_ICHG_STEP_MA);

    result = SGM41511_UpdateBits(SGM41511_REG_CHARGE_CURRENT,
                                 SGM41511_REG02_ICHG_MASK,
                                 code);
    if ((result == BSP_I2C_OK) && (applied_ma != 0))
    {
        *applied_ma = limited_ma;
    }
    return result;
}

int SGM41511_GetChargeCurrentMa(uint16_t *current_ma)
{
    uint8_t value;
    uint8_t code;
    int result;

    if (current_ma == 0)
    {
        return BSP_I2C_ERROR_INVALID_ARG;
    }

    result = SGM41511_ReadRegister(SGM41511_REG_CHARGE_CURRENT, &value);
    if (result != BSP_I2C_OK)
    {
        return result;
    }

    code = (uint8_t)(value & SGM41511_REG02_ICHG_MASK);
    if (code > SGM41511_ICHG_MAX_CODE)
    {
        code = SGM41511_ICHG_MAX_CODE;
    }
    *current_ma = (uint16_t)((uint16_t)code * SGM41511_ICHG_STEP_MA);
    return BSP_I2C_OK;
}

int SGM41511_GetStatus(SGM41511_Status_T *status)
{
    uint8_t reg08;
    uint8_t reg0a;
    int result;

    if (status == 0)
    {
        return BSP_I2C_ERROR_INVALID_ARG;
    }

    result = SGM41511_ReadRegister(SGM41511_REG_STATUS, &reg08);
    if (result != BSP_I2C_OK)
    {
        return result;
    }
    result = SGM41511_ReadRegister(SGM41511_REG_STATUS2, &reg0a);
    if (result != BSP_I2C_OK)
    {
        return result;
    }

    status->reg08 = reg08;
    status->reg0a = reg0a;
    status->vbus_status = (uint8_t)((reg08 & SGM41511_REG08_VBUS_MASK) >> SGM41511_REG08_VBUS_SHIFT);
    status->charge_state = (SGM41511_ChargeState_T)((reg08 & SGM41511_REG08_CHARGE_MASK) >>
                                                    SGM41511_REG08_CHARGE_SHIFT);
    status->power_good = ((reg08 & SGM41511_REG08_POWER_GOOD) != 0U) ? 1U : 0U;
    status->thermal_regulation = ((reg08 & SGM41511_REG08_THERMAL) != 0U) ? 1U : 0U;
    status->vsys_regulation = ((reg08 & SGM41511_REG08_VSYS) != 0U) ? 1U : 0U;
    status->vbus_good = ((reg0a & SGM41511_REG0A_VBUS_GOOD) != 0U) ? 1U : 0U;
    status->vindpm_active = ((reg0a & SGM41511_REG0A_VINDPM) != 0U) ? 1U : 0U;
    status->iindpm_active = ((reg0a & SGM41511_REG0A_IINDPM) != 0U) ? 1U : 0U;
    status->acov_active = ((reg0a & SGM41511_REG0A_ACOV) != 0U) ? 1U : 0U;
    return BSP_I2C_OK;
}

int SGM41511_ReadFault(SGM41511_Fault_T *fault)
{
    int result;

    if (fault == 0)
    {
        return BSP_I2C_ERROR_INVALID_ARG;
    }

    /* REG09 is special: first read is latched history, second read is current fault. */
    result = SGM41511_ReadRegister(SGM41511_REG_FAULT, &fault->latched);
    if (result != BSP_I2C_OK)
    {
        return result;
    }
    result = SGM41511_ReadRegister(SGM41511_REG_FAULT, &fault->current);
    return result;
}
