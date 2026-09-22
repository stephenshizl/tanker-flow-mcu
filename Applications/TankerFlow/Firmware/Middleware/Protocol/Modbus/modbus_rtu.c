#include "modbus_rtu.h"

static uint8_t ModbusRtu_FrameCrcValid(const uint8_t *frame, uint16_t length)
{
    uint16_t crc;

    if ((frame == 0) || (length < 4U))
    {
        return 0U;
    }

    crc = ModbusRtu_Crc16(frame, (uint16_t)(length - 2U));
    return (uint8_t)(((uint8_t)(crc & 0xFFU) == frame[length - 2U]) &&
                     ((uint8_t)((crc >> 8U) & 0xFFU) == frame[length - 1U]));
}

uint16_t ModbusRtu_Crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t bit;

    if ((data == 0) && (length != 0U))
    {
        return 0U;
    }

    for (i = 0U; i < length; i++)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x0001U) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

int ModbusRtu_BuildReadHoldingRegisters(uint8_t slave_address,
                                        uint16_t start_register,
                                        uint16_t register_count,
                                        uint8_t *frame,
                                        uint16_t capacity)
{
    uint16_t crc;

    if ((frame == 0) || (slave_address == 0U) || (register_count == 0U))
    {
        return MODBUS_RTU_ERR_INVALID_ARG;
    }

    if (capacity < MODBUS_RTU_READ_REQUEST_SIZE)
    {
        return MODBUS_RTU_ERR_BUFFER_TOO_SMALL;
    }

    frame[0] = slave_address;
    frame[1] = MODBUS_RTU_FUNC_READ_HOLDING_REGISTERS;
    frame[2] = (uint8_t)(start_register >> 8U);
    frame[3] = (uint8_t)(start_register & 0xFFU);
    frame[4] = (uint8_t)(register_count >> 8U);
    frame[5] = (uint8_t)(register_count & 0xFFU);

    crc = ModbusRtu_Crc16(frame, 6U);
    frame[6] = (uint8_t)(crc & 0xFFU);
    frame[7] = (uint8_t)((crc >> 8U) & 0xFFU);

    return (int)MODBUS_RTU_READ_REQUEST_SIZE;
}

int ModbusRtu_ParseReadHoldingRegistersResponse(uint8_t expected_slave_address,
                                                uint16_t expected_register_count,
                                                const uint8_t *frame,
                                                uint16_t length,
                                                const uint8_t **data,
                                                uint8_t *byte_count,
                                                uint8_t *exception_code)
{
    uint16_t expected_byte_count;
    uint16_t expected_length;

    if ((frame == 0) || (data == 0) || (byte_count == 0) ||
        (expected_slave_address == 0U) || (expected_register_count == 0U))
    {
        return MODBUS_RTU_ERR_INVALID_ARG;
    }

    *data = 0;
    *byte_count = 0U;
    if (exception_code != 0)
    {
        *exception_code = 0U;
    }

    if (length < 5U)
    {
        return MODBUS_RTU_ERR_FRAME_LENGTH;
    }

    if (ModbusRtu_FrameCrcValid(frame, length) == 0U)
    {
        return MODBUS_RTU_ERR_CRC;
    }

    if (frame[0] != expected_slave_address)
    {
        return MODBUS_RTU_ERR_ADDRESS;
    }

    if (frame[1] == (uint8_t)(MODBUS_RTU_FUNC_READ_HOLDING_REGISTERS | MODBUS_RTU_EXCEPTION_FLAG))
    {
        if (length != 5U)
        {
            return MODBUS_RTU_ERR_FRAME_LENGTH;
        }
        if (exception_code != 0)
        {
            *exception_code = frame[2];
        }
        return MODBUS_RTU_ERR_EXCEPTION;
    }

    if (frame[1] != MODBUS_RTU_FUNC_READ_HOLDING_REGISTERS)
    {
        return MODBUS_RTU_ERR_FUNCTION;
    }

    if (expected_register_count > 127U)
    {
        return MODBUS_RTU_ERR_INVALID_ARG;
    }

    expected_byte_count = (uint16_t)(expected_register_count * 2U);
    if ((uint16_t)frame[2] != expected_byte_count)
    {
        return MODBUS_RTU_ERR_BYTE_COUNT;
    }

    expected_length = (uint16_t)(3U + expected_byte_count + 2U);
    if (length != expected_length)
    {
        return MODBUS_RTU_ERR_FRAME_LENGTH;
    }

    *data = &frame[3];
    *byte_count = frame[2];
    return MODBUS_RTU_OK;
}
