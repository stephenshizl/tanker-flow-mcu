#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stdint.h>

#define MODBUS_RTU_FUNC_READ_HOLDING_REGISTERS  (0x03U)
#define MODBUS_RTU_EXCEPTION_FLAG               (0x80U)
#define MODBUS_RTU_READ_REQUEST_SIZE             (8U)

typedef enum
{
    MODBUS_RTU_OK = 0,
    MODBUS_RTU_ERR_INVALID_ARG = -1,
    MODBUS_RTU_ERR_BUFFER_TOO_SMALL = -2,
    MODBUS_RTU_ERR_FRAME_LENGTH = -3,
    MODBUS_RTU_ERR_CRC = -4,
    MODBUS_RTU_ERR_ADDRESS = -5,
    MODBUS_RTU_ERR_FUNCTION = -6,
    MODBUS_RTU_ERR_BYTE_COUNT = -7,
    MODBUS_RTU_ERR_EXCEPTION = -8
} ModbusRtu_Result_T;

/* Standard Modbus RTU CRC16 (polynomial 0xA001, initial value 0xFFFF). */
uint16_t ModbusRtu_Crc16(const uint8_t *data, uint16_t length);

/*
 * Build function-03 Read Holding Registers request.
 * Returns the 8-byte frame length or a negative ModbusRtu_Result_T.
 * CRC is serialized low byte first as required by Modbus RTU.
 */
int ModbusRtu_BuildReadHoldingRegisters(uint8_t slave_address,
                                        uint16_t start_register,
                                        uint16_t register_count,
                                        uint8_t *frame,
                                        uint16_t capacity);

/*
 * Validate a function-03 response and expose the response data field.
 * expected_register_count is the number requested by the master.
 * On MODBUS_RTU_ERR_EXCEPTION, exception_code receives the slave exception
 * code when the pointer is non-NULL.
 */
int ModbusRtu_ParseReadHoldingRegistersResponse(uint8_t expected_slave_address,
                                                uint16_t expected_register_count,
                                                const uint8_t *frame,
                                                uint16_t length,
                                                const uint8_t **data,
                                                uint8_t *byte_count,
                                                uint8_t *exception_code);

#endif /* MODBUS_RTU_H */
