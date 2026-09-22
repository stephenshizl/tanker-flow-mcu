#include <stdio.h>
#include <string.h>

#include "modbus_rtu.h"
#include "zelz_flow_meter.h"

static int Expect(int condition, const char *message)
{
    if (!condition)
    {
        printf("FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

static uint16_t AppendCrc(uint8_t *frame, uint16_t payload_length)
{
    uint16_t crc = ModbusRtu_Crc16(frame, payload_length);
    frame[payload_length] = (uint8_t)(crc & 0xFFU);
    frame[payload_length + 1U] = (uint8_t)(crc >> 8U);
    return (uint16_t)(payload_length + 2U);
}

static void EncodeZelzU32(uint32_t value, uint8_t *data)
{
    data[0] = (uint8_t)((value >> 8U) & 0xFFU);
    data[1] = (uint8_t)(value & 0xFFU);
    data[2] = (uint8_t)((value >> 24U) & 0xFFU);
    data[3] = (uint8_t)((value >> 16U) & 0xFFU);
}

int main(void)
{
    static const uint8_t expected_batch_request[] = {0x01U, 0x03U, 0x00U, 0x00U, 0x00U, 0x02U, 0xC4U, 0x0BU};
    static const uint8_t expected_total_request[] = {0x01U, 0x03U, 0x00U, 0x02U, 0x00U, 0x02U, 0x65U, 0xCBU};
    static const uint8_t expected_instant_request[] = {0x01U, 0x03U, 0x00U, 0x04U, 0x00U, 0x02U, 0x85U, 0xCAU};
    static const uint8_t documented_response[] = {0x01U, 0x03U, 0x04U, 0x61U, 0x4EU, 0x00U, 0xBCU, 0x84U, 0x69U};
    uint8_t frame[32];
    uint8_t exception_code = 0U;
    uint32_t value = 0U;
    ZelzFlow_Realtime_T realtime;
    int length;
    int result;
    int ok = 1;

    length = ZelzFlow_BuildReadItemRequest(1U, ZELZ_FLOW_ITEM_BATCH_TOTAL, frame, sizeof(frame));
    ok &= Expect(length == 8, "batch-total request length");
    ok &= Expect(memcmp(frame, expected_batch_request, sizeof(expected_batch_request)) == 0,
                 "batch-total request matches protocol example");

    length = ZelzFlow_BuildReadItemRequest(1U, ZELZ_FLOW_ITEM_TOTAL, frame, sizeof(frame));
    ok &= Expect(length == 8, "total request length");
    ok &= Expect(memcmp(frame, expected_total_request, sizeof(expected_total_request)) == 0,
                 "total request matches protocol example");

    length = ZelzFlow_BuildReadItemRequest(1U, ZELZ_FLOW_ITEM_INSTANT_FLOW, frame, sizeof(frame));
    ok &= Expect(length == 8, "instant-flow request length");
    ok &= Expect(memcmp(frame, expected_instant_request, sizeof(expected_instant_request)) == 0,
                 "instant-flow request matches protocol example");

    result = ZelzFlow_ParseReadItemResponse(1U,
                                            ZELZ_FLOW_ITEM_BATCH_TOTAL,
                                            documented_response,
                                            sizeof(documented_response),
                                            &value,
                                            &exception_code);
    ok &= Expect(result == ZELZ_FLOW_OK, "documented item response parses");
    ok &= Expect(value == 12345678UL, "documented word order decodes to 12345.678 scaled units");

    length = ZelzFlow_BuildReadRealtimeRequest(1U, frame, sizeof(frame));
    ok &= Expect(length == 8, "all-realtime request length");
    ok &= Expect((frame[0] == 0x01U) && (frame[1] == 0x03U) &&
                 (frame[2] == 0x00U) && (frame[3] == 0x00U) &&
                 (frame[4] == 0x00U) && (frame[5] == 0x06U),
                 "all-realtime request reads registers 0..5");

    frame[0] = 0x01U;
    frame[1] = 0x03U;
    frame[2] = 12U;
    EncodeZelzU32(12345678UL, &frame[3]);
    EncodeZelzU32(87654321UL, &frame[7]);
    EncodeZelzU32(345678UL, &frame[11]);
    length = (int)AppendCrc(frame, 15U);
    result = ZelzFlow_ParseReadRealtimeResponse(1U, frame, (uint16_t)length, &realtime, &exception_code);
    ok &= Expect(result == ZELZ_FLOW_OK, "all-realtime response parses");
    ok &= Expect(realtime.batch_total_milli_l == 12345678UL, "batch total parsed");
    ok &= Expect(realtime.total_milli_m3 == 87654321UL, "total parsed");
    ok &= Expect(realtime.instant_milli_m3_per_h == 345678UL, "instant flow parsed");

    frame[0] = 0x01U;
    frame[1] = 0x83U;
    frame[2] = 0x02U;
    length = (int)AppendCrc(frame, 3U);
    exception_code = 0U;
    result = ZelzFlow_ParseReadItemResponse(1U,
                                            ZELZ_FLOW_ITEM_TOTAL,
                                            frame,
                                            (uint16_t)length,
                                            &value,
                                            &exception_code);
    ok &= Expect(result == ZELZ_FLOW_ERR_EXCEPTION, "Modbus exception propagated");
    ok &= Expect(exception_code == 0x02U, "Modbus exception code preserved");

    memcpy(frame, documented_response, sizeof(documented_response));
    frame[8] ^= 0x01U;
    result = ZelzFlow_ParseReadItemResponse(1U,
                                            ZELZ_FLOW_ITEM_TOTAL,
                                            frame,
                                            sizeof(documented_response),
                                            &value,
                                            &exception_code);
    ok &= Expect(result == ZELZ_FLOW_ERR_FRAME, "bad CRC rejected");

    result = ZelzFlow_BuildReadRealtimeRequest(0U, frame, sizeof(frame));
    ok &= Expect(result == ZELZ_FLOW_ERR_INVALID_ARG, "slave address zero rejected");

    if (!ok)
    {
        return 1;
    }

    printf("ZELZ flow-meter protocol host tests: PASS\n");
    return 0;
}
