#ifndef BSP_RING_BUFFER_H
#define BSP_RING_BUFFER_H

#include <stdint.h>

typedef struct
{
    uint8_t *data;
    uint16_t size;
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t overflow_count;
} BSP_RingBuffer_T;

void BSP_RingBuffer_Init(BSP_RingBuffer_T *rb, uint8_t *storage, uint16_t size);
void BSP_RingBuffer_Reset(BSP_RingBuffer_T *rb);
uint8_t BSP_RingBuffer_Push(BSP_RingBuffer_T *rb, uint8_t byte);
uint16_t BSP_RingBuffer_PushBlock(BSP_RingBuffer_T *rb, const uint8_t *data, uint16_t length);
uint8_t BSP_RingBuffer_Pop(BSP_RingBuffer_T *rb, uint8_t *byte);
uint16_t BSP_RingBuffer_Read(BSP_RingBuffer_T *rb, uint8_t *data, uint16_t max_length);
uint16_t BSP_RingBuffer_Available(const BSP_RingBuffer_T *rb);
uint32_t BSP_RingBuffer_OverflowCount(const BSP_RingBuffer_T *rb);

#endif /* BSP_RING_BUFFER_H */
