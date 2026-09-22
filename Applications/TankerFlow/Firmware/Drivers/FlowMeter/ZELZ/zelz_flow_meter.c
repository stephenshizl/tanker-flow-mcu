#include "zelz_flow_meter.h"

#include "modbus_rtu.h"

static int ZelzFlow_ItemRegister(ZelzFlow_Item_T item, uint16_t *register_address)
{
    if (register_address == 0)
    {
        return ZELZ_FLOW_ERR_INVALID_ARG;
    }

    switch (item)
    {
        case ZELZ_FLOW_ITEM_BATCH_TOTAL:
            *register_address = ZELZ_REG_BATCH_TOTAL;
            return ZELZ_FLOW_OK;

        case ZELZ_FLOW_ITEM_TOTAL:
            *register_address = ZELZ_REG_TOTAL;
            return ZELZ_FLOW_OK;

        case ZELZ_FLOW_ITEM_INSTANT_FLOW:
            *register_address = ZELZ_REG_INSTANT_FLOW;
            return ZELZ_FLOW_OK;

        default:
            return ZELZ_FLOW_ERR_INVALID_ARG;
    }
}

/*
 * ZELZ 32-bit values use low 16-bit word first, while bytes inside each
 * 16-bit word are high byte first. Example: 61 4E 00 BC -> 0x00BC614E.
 */
static uint32_t ZelzFlow_DecodeU32(const uint8_t *data)
{
    return ((uint32_t)data[2] << 24U) |
           ((uint32_t)data[3] << 16U) |
           ((uint32_t)data[0] << 8U) |
           (uint32_t)data[1];
}

static int ZelzFlow_MapModbusResult(int result)
{
    if (result == MODBUS_RTU_ERR_INVALID_ARG)
    {
        return ZELZ_FLOW_ERR_INVALID_ARG;
    }
    if (result == MODBUS_RTU_ERR_BUFFER_TOO_SMALL)
    {
        return ZELZ_FLOW_ERR_BUFFER_TOO_SMALL;
    }
    if (result == MODBUS_RTU_ERR_EXCEPTION)
    {
        return ZELZ_FLOW_ERR_EXCEPTION;
    }
    if (result < 0)
    {
        return ZELZ_FLOW_ERR_FRAME;
    }
    return result;
}

int ZelzFlow_BuildReadItemRequest(uint8_t slave_address,
                                  ZelzFlow_Item_T item,
                                  uint8_t *frame,
                                  uint16_t capacity)
{
    uint16_t register_address;
    int result;

    result = ZelzFlow_ItemRegister(item, &register_address);
    if (result != ZELZ_FLOW_OK)
    {
        return result;
    }

    result = ModbusRtu_BuildReadHoldingRegisters(slave_address,
                                                 register_address,
                                                 ZELZ_VALUE_REGISTER_COUNT,
                                                 frame,
                                                 capacity);
    return ZelzFlow_MapModbusResult(result);
}

int ZelzFlow_BuildReadRealtimeRequest(uint8_t slave_address,
                                      uint8_t *frame,
                                      uint16_t capacity)
{
    int result;

    result = ModbusRtu_BuildReadHoldingRegisters(slave_address,
                                                 ZELZ_REG_BATCH_TOTAL,
                                                 ZELZ_REG_REALTIME_COUNT,
                                                 frame,
                                                 capacity);
    return ZelzFlow_MapModbusResult(result);
}

int ZelzFlow_ParseReadItemResponse(uint8_t slave_address,
                                   ZelzFlow_Item_T item,
                                   const uint8_t *frame,
                                   uint16_t length,
                                   uint32_t *raw_value,
                                   uint8_t *exception_code)
{
    const uint8_t *data;
    uint8_t byte_count;
    uint16_t unused_register;
    int result;

    if (raw_value == 0)
    {
        return ZELZ_FLOW_ERR_INVALID_ARG;
    }

    result = ZelzFlow_ItemRegister(item, &unused_register);
    if (result != ZELZ_FLOW_OK)
    {
        return result;
    }

    result = ModbusRtu_ParseReadHoldingRegistersResponse(slave_address,
                                                         ZELZ_VALUE_REGISTER_COUNT,
                                                         frame,
                                                         length,
                                                         &data,
                                                         &byte_count,
                                                         exception_code);
    if (result != MODBUS_RTU_OK)
    {
        return ZelzFlow_MapModbusResult(result);
    }

    if (byte_count != 4U)
    {
        return ZELZ_FLOW_ERR_FRAME;
    }

    *raw_value = ZelzFlow_DecodeU32(data);
    return ZELZ_FLOW_OK;
}

int ZelzFlow_ParseReadRealtimeResponse(uint8_t slave_address,
                                       const uint8_t *frame,
                                       uint16_t length,
                                       ZelzFlow_Realtime_T *realtime,
                                       uint8_t *exception_code)
{
    const uint8_t *data;
    uint8_t byte_count;
    int result;

    if (realtime == 0)
    {
        return ZELZ_FLOW_ERR_INVALID_ARG;
    }

    result = ModbusRtu_ParseReadHoldingRegistersResponse(slave_address,
                                                         ZELZ_REG_REALTIME_COUNT,
                                                         frame,
                                                         length,
                                                         &data,
                                                         &byte_count,
                                                         exception_code);
    if (result != MODBUS_RTU_OK)
    {
        return ZelzFlow_MapModbusResult(result);
    }

    if (byte_count != 12U)
    {
        return ZELZ_FLOW_ERR_FRAME;
    }

    realtime->batch_total_milli_l = ZelzFlow_DecodeU32(&data[0]);
    realtime->total_milli_m3 = ZelzFlow_DecodeU32(&data[4]);
    realtime->instant_milli_m3_per_h = ZelzFlow_DecodeU32(&data[8]);
    return ZELZ_FLOW_OK;
}
