#ifndef ZELZ_FLOW_METER_H
#define ZELZ_FLOW_METER_H

#include <stdint.h>

#define ZELZ_REG_BATCH_TOTAL       (0x0000U)
#define ZELZ_REG_TOTAL             (0x0002U)
#define ZELZ_REG_INSTANT_FLOW      (0x0004U)
#define ZELZ_REG_REALTIME_COUNT    (6U)
#define ZELZ_VALUE_REGISTER_COUNT  (2U)

typedef enum
{
    ZELZ_FLOW_OK = 0,
    ZELZ_FLOW_ERR_INVALID_ARG = -1,
    ZELZ_FLOW_ERR_BUFFER_TOO_SMALL = -2,
    ZELZ_FLOW_ERR_FRAME = -3,
    ZELZ_FLOW_ERR_EXCEPTION = -4
} ZelzFlow_Result_T;

typedef enum
{
    ZELZ_FLOW_ITEM_BATCH_TOTAL = 0,
    ZELZ_FLOW_ITEM_TOTAL,
    ZELZ_FLOW_ITEM_INSTANT_FLOW
} ZelzFlow_Item_T;

/*
 * Raw values preserve the meter's documented x1000 engineering-unit scaling:
 * batch_total_milli_l      -> 0.001 L
 * total_milli_m3           -> 0.001 m3
 * instant_milli_m3_per_h   -> 0.001 m3/h
 * No float/double is required in firmware.
 */
typedef struct
{
    uint32_t batch_total_milli_l;
    uint32_t total_milli_m3;
    uint32_t instant_milli_m3_per_h;
} ZelzFlow_Realtime_T;

int ZelzFlow_BuildReadItemRequest(uint8_t slave_address,
                                  ZelzFlow_Item_T item,
                                  uint8_t *frame,
                                  uint16_t capacity);

/* Read registers 0x0000..0x0005 in one function-03 request. */
int ZelzFlow_BuildReadRealtimeRequest(uint8_t slave_address,
                                      uint8_t *frame,
                                      uint16_t capacity);

int ZelzFlow_ParseReadItemResponse(uint8_t slave_address,
                                   ZelzFlow_Item_T item,
                                   const uint8_t *frame,
                                   uint16_t length,
                                   uint32_t *raw_value,
                                   uint8_t *exception_code);

int ZelzFlow_ParseReadRealtimeResponse(uint8_t slave_address,
                                       const uint8_t *frame,
                                       uint16_t length,
                                       ZelzFlow_Realtime_T *realtime,
                                       uint8_t *exception_code);

#endif /* ZELZ_FLOW_METER_H */
