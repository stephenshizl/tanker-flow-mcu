#include "bsp_ring_buffer.h"

void BSP_RingBuffer_Init(BSP_RingBuffer_T *rb, uint8_t *storage, uint16_t size)
{
    rb->data = storage;
    rb->size = size;
    rb->head = 0U;
    rb->tail = 0U;
    rb->overflow_count = 0U;
}

void BSP_RingBuffer_Reset(BSP_RingBuffer_T *rb)
{
    rb->head = 0U;
    rb->tail = 0U;
}

uint8_t BSP_RingBuffer_Push(BSP_RingBuffer_T *rb, uint8_t byte)
{
    uint16_t next;

    next = (uint16_t)(rb->head + 1U);
    if (next >= rb->size)
    {
        next = 0U;
    }

    if (next == rb->tail)
    {
        rb->overflow_count++;
        return 0U;
    }

    rb->data[rb->head] = byte;
    rb->head = next;
    return 1U;
}

uint16_t BSP_RingBuffer_PushBlock(BSP_RingBuffer_T *rb, const uint8_t *data, uint16_t length)
{
    uint16_t count;

    count = 0U;
    while ((count < length) && (BSP_RingBuffer_Push(rb, data[count]) != 0U))
    {
        count++;
    }
    return count;
}

uint8_t BSP_RingBuffer_Pop(BSP_RingBuffer_T *rb, uint8_t *byte)
{
    uint16_t next;

    if (rb->tail == rb->head)
    {
        return 0U;
    }

    *byte = rb->data[rb->tail];
    next = (uint16_t)(rb->tail + 1U);
    if (next >= rb->size)
    {
        next = 0U;
    }
    rb->tail = next;
    return 1U;
}

uint16_t BSP_RingBuffer_Read(BSP_RingBuffer_T *rb, uint8_t *data, uint16_t max_length)
{
    uint16_t count;

    count = 0U;
    while ((count < max_length) && (BSP_RingBuffer_Pop(rb, &data[count]) != 0U))
    {
        count++;
    }
    return count;
}

uint16_t BSP_RingBuffer_Available(const BSP_RingBuffer_T *rb)
{
    uint16_t head;
    uint16_t tail;

    head = rb->head;
    tail = rb->tail;
    if (head >= tail)
    {
        return (uint16_t)(head - tail);
    }
    return (uint16_t)(rb->size - tail + head);
}

uint32_t BSP_RingBuffer_OverflowCount(const BSP_RingBuffer_T *rb)
{
    return rb->overflow_count;
}
