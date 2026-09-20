#include "bsp_uart.h"

#include "board_pinmap.h"
#include "bsp_ring_buffer.h"
#include "apm32f0xx_dma.h"
#include "apm32f0xx_gpio.h"
#include "apm32f0xx_misc.h"
#include "apm32f0xx_rcm.h"
#include "apm32f0xx_tmr.h"
#include "apm32f0xx_usart.h"

#define DEBUG_RX_RING_SIZE       (128U)
#define BT_RX_RING_SIZE          (512U)
#define MODEM_RX_RING_SIZE       (2048U)
#define MODEM_DMA_SIZE           (512U)
#define GNSS_RX_RING_SIZE        (512U)
#define GNSS_DMA_SIZE            (256U)
#define FLOW_RX_RING_SIZE        (512U)
#define FLOW_FRAME_QUEUE_SIZE    (8U)
#define FLOW_T35_TIMER_US        (4100U)
#define UART_STATS_PORT_COUNT     (6U)

static uint8_t g_debug_storage[DEBUG_RX_RING_SIZE];
static uint8_t g_bt_storage[BT_RX_RING_SIZE];
static uint8_t g_modem_storage[MODEM_RX_RING_SIZE];
static uint8_t g_modem_dma[MODEM_DMA_SIZE];
static uint8_t g_gnss_storage[GNSS_RX_RING_SIZE];
static uint8_t g_gnss_dma[GNSS_DMA_SIZE];
static uint8_t g_flow_storage[FLOW_RX_RING_SIZE];

static BSP_RingBuffer_T g_debug_rb;
static BSP_RingBuffer_T g_bt_rb;
static BSP_RingBuffer_T g_modem_rb;
static BSP_RingBuffer_T g_gnss_rb;
static BSP_RingBuffer_T g_flow_rb;

static volatile uint16_t g_flow_current_length;
static volatile uint8_t g_flow_current_valid;
static volatile uint16_t g_flow_frame_lengths[FLOW_FRAME_QUEUE_SIZE];
static volatile uint8_t g_flow_frame_head;
static volatile uint8_t g_flow_frame_tail;
static volatile uint32_t g_flow_frame_overflow;

static volatile uint32_t g_uart_rx_bytes[UART_STATS_PORT_COUNT];
static volatile uint32_t g_uart_idle_events[UART_STATS_PORT_COUNT];
static volatile uint32_t g_uart_dma_full_events[UART_STATS_PORT_COUNT];

static void Uart_ConfigPins(GPIO_T *tx_port,
                            uint16_t tx_pin,
                            GPIO_PIN_SOURCE_T tx_source,
                            GPIO_AF_T tx_af,
                            GPIO_T *rx_port,
                            uint16_t rx_pin,
                            GPIO_PIN_SOURCE_T rx_source,
                            GPIO_AF_T rx_af)
{
    GPIO_Config_T config;

    GPIO_ConfigPinAF(tx_port, tx_source, tx_af);
    GPIO_ConfigPinAF(rx_port, rx_source, rx_af);

    GPIO_ConfigStructInit(&config);
    config.mode = GPIO_MODE_AF;
    config.outtype = GPIO_OUT_TYPE_PP;
    config.speed = GPIO_SPEED_50MHz;
    config.pupd = GPIO_PUPD_PU;
    config.pin = tx_pin;
    GPIO_Config(tx_port, &config);

    config.pin = rx_pin;
    GPIO_Config(rx_port, &config);
}

static void Uart_Config(USART_T *usart, uint32_t baud, USART_WORD_LEN_T word_length, USART_PARITY_T parity)
{
    USART_Config_T config;

    USART_ConfigStructInit(&config);
    config.baudRate = baud;
    config.hardwareFlowCtrl = USART_FLOW_CTRL_NONE;
    config.mode = USART_MODE_TX_RX;
    config.parity = parity;
    config.stopBits = USART_STOP_BIT_1;
    config.wordLength = word_length;
    USART_Config(usart, &config);
    USART_Enable(usart);
}

static void Dma_ConfigRx(DMA_CHANNEL_T *channel,
                         DMA_CHANNEL_REMAP_T remap,
                         USART_T *usart,
                         uint8_t *buffer,
                         uint16_t length,
                         uint32_t transfer_flag)
{
    DMA_Config_T config;

    DMA_Disable(channel);
    DMA_Reset(channel);
    DMA_ConfigRemap(DMA1, remap);

    DMA_ConfigStructInit(&config);
    config.bufferSize = length;
    config.memoryDataSize = DMA_MEMORY_DATASIZE_BYTE;
    config.peripheralDataSize = DMA_PERIPHERAL_DATASIZE_BYTE;
    config.memoryInc = DMA_MEMORY_INC_ENABLE;
    config.peripheralInc = DMA_PERIPHERAL_INC_DISABLE;
    config.circular = DMA_CIRCULAR_DISABLE;
    config.memoryTomemory = DMA_M2M_DISABLE;
    config.priority = DMA_PRIORITY_LEVEL_HIGHT;
    config.direction = DMA_DIR_PERIPHERAL;
    config.memoryAddress = (uint32_t)buffer;
    config.peripheralAddress = (uint32_t)&usart->RXDATA;
    DMA_Config(channel, &config);
    DMA_ClearIntFlag(transfer_flag);
    DMA_EnableInterrupt(channel, DMA_INT_TFIE);
    DMA_Enable(channel);
    USART_EnableDMA(usart, USART_DMA_REQUEST_RX);
}

static uint16_t Dma_FlushToRing(DMA_CHANNEL_T *channel,
                            uint8_t *dma_buffer,
                            uint16_t dma_size,
                            uint32_t transfer_flag,
                            BSP_RingBuffer_T *ring)
{
    uint32_t remaining;
    uint16_t received;

    DMA_Disable(channel);
    remaining = DMA_ReadDataNumber(channel);
    if (remaining > dma_size)
    {
        remaining = dma_size;
    }
    received = (uint16_t)(dma_size - remaining);
    if (received > 0U)
    {
        (void)BSP_RingBuffer_PushBlock(ring, dma_buffer, received);
    }
    DMA_ClearIntFlag(transfer_flag);
    DMA_SetDataNumber(channel, dma_size);
    DMA_Enable(channel);
    return received;
}

static void Flow_RestartGapTimer(void)
{
    TMR_Disable(TMR16);
    TMR_SetCounter(TMR16, 0U);
    TMR_ClearStatusFlag(TMR16, TMR_FLAG_UPDATE);
    TMR_Enable(TMR16);
}

static void Flow_QueueFrameLength(uint16_t length)
{
    uint8_t next;

    next = (uint8_t)(g_flow_frame_head + 1U);
    if (next >= FLOW_FRAME_QUEUE_SIZE)
    {
        next = 0U;
    }

    if (next == g_flow_frame_tail)
    {
        g_flow_frame_overflow++;
        BSP_RingBuffer_Reset(&g_flow_rb);
        g_flow_frame_head = 0U;
        g_flow_frame_tail = 0U;
        return;
    }

    g_flow_frame_lengths[g_flow_frame_head] = length;
    g_flow_frame_head = next;
}

static BSP_RingBuffer_T *Uart_GetRing(BSP_UartPort_T port)
{
    switch (port)
    {
        case BSP_UART_DEBUG:     return &g_debug_rb;
        case BSP_UART_BLUETOOTH: return &g_bt_rb;
        case BSP_UART_4G:        return &g_modem_rb;
        case BSP_UART_GNSS:      return &g_gnss_rb;
        case BSP_UART_FLOW:      return &g_flow_rb;
        default:                 return 0;
    }
}

static USART_T *Uart_GetPeripheral(BSP_UartPort_T port)
{
    switch (port)
    {
        case BSP_UART_DEBUG:     return USART1;
        case BSP_UART_BLUETOOTH: return USART2;
        case BSP_UART_4G:        return USART3;
        case BSP_UART_GNSS:      return USART4;
        case BSP_UART_FLOW:      return USART5;
        default:                 return 0;
    }
}

void BSP_Uart_Init(void)
{
    TMR_TimeBase_T timer_config;
    uint8_t port_index;

    BSP_RingBuffer_Init(&g_debug_rb, g_debug_storage, DEBUG_RX_RING_SIZE);
    BSP_RingBuffer_Init(&g_bt_rb, g_bt_storage, BT_RX_RING_SIZE);
    BSP_RingBuffer_Init(&g_modem_rb, g_modem_storage, MODEM_RX_RING_SIZE);
    BSP_RingBuffer_Init(&g_gnss_rb, g_gnss_storage, GNSS_RX_RING_SIZE);
    BSP_RingBuffer_Init(&g_flow_rb, g_flow_storage, FLOW_RX_RING_SIZE);

    g_flow_current_length = 0U;
    g_flow_current_valid = 1U;
    g_flow_frame_head = 0U;
    g_flow_frame_tail = 0U;
    g_flow_frame_overflow = 0U;

    for (port_index = 0U; port_index < UART_STATS_PORT_COUNT; port_index++)
    {
        g_uart_rx_bytes[port_index] = 0U;
        g_uart_idle_events[port_index] = 0U;
        g_uart_dma_full_events[port_index] = 0U;
    }

    RCM_EnableAHBPeriphClock(RCM_AHB_PERIPH_GPIOA | RCM_AHB_PERIPH_GPIOB | RCM_AHB_PERIPH_DMA1);
    RCM_EnableAPB1PeriphClock(RCM_APB1_PERIPH_USART2 |
                              RCM_APB1_PERIPH_USART3 |
                              RCM_APB1_PERIPH_USART4 |
                              RCM_APB1_PERIPH_USART5);
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_TMR16);

    Uart_ConfigPins(BOARD_BT_TX_PORT, BOARD_BT_TX_PIN, BOARD_BT_TX_SOURCE, BOARD_BT_TX_AF,
                    BOARD_BT_RX_PORT, BOARD_BT_RX_PIN, BOARD_BT_RX_SOURCE, BOARD_BT_RX_AF);
    Uart_ConfigPins(BOARD_4G_TX_PORT, BOARD_4G_TX_PIN, BOARD_4G_TX_SOURCE, BOARD_4G_TX_AF,
                    BOARD_4G_RX_PORT, BOARD_4G_RX_PIN, BOARD_4G_RX_SOURCE, BOARD_4G_RX_AF);
    Uart_ConfigPins(BOARD_GNSS_TX_PORT, BOARD_GNSS_TX_PIN, BOARD_GNSS_TX_SOURCE, BOARD_GNSS_TX_AF,
                    BOARD_GNSS_RX_PORT, BOARD_GNSS_RX_PIN, BOARD_GNSS_RX_SOURCE, BOARD_GNSS_RX_AF);
    Uart_ConfigPins(BOARD_FLOW_TX_PORT, BOARD_FLOW_TX_PIN, BOARD_FLOW_TX_SOURCE, BOARD_FLOW_TX_AF,
                    BOARD_FLOW_RX_PORT, BOARD_FLOW_RX_PIN, BOARD_FLOW_RX_SOURCE, BOARD_FLOW_RX_AF);

    Uart_Config(USART2, 115200U, USART_WORD_LEN_8B, USART_PARITY_NONE);
    Uart_Config(USART3, 115200U, USART_WORD_LEN_8B, USART_PARITY_NONE);
    Uart_Config(USART4, 9600U, USART_WORD_LEN_8B, USART_PARITY_NONE);
    /* 8 data + even parity requires 9-bit word length on this USART peripheral. */
    Uart_Config(USART5, 9600U, USART_WORD_LEN_9B, USART_PARITY_EVEN);

    USART_EnableInterrupt(USART1, USART_INT_RXBNEIE);
    USART_EnableInterrupt(USART2, USART_INT_RXBNEIE);
    USART_EnableInterrupt(USART3, USART_INT_IDLEIE);
    USART_EnableInterrupt(USART4, USART_INT_IDLEIE);
    USART_EnableInterrupt(USART5, USART_INT_RXBNEIE);

    Dma_ConfigRx(DMA1_CHANNEL_3, DMA1_CHANNEL3_USART3_RX, USART3,
                 g_modem_dma, MODEM_DMA_SIZE, DMA1_INT_FLAG_TF3);
    Dma_ConfigRx(DMA1_CHANNEL_5, DMA1_CHANNEL5_USART4_RX, USART4,
                 g_gnss_dma, GNSS_DMA_SIZE, DMA1_INT_FLAG_TF5);

    TMR_ConfigTimeBaseStruct(&timer_config);
    timer_config.div = 47U; /* 48 MHz / (47+1) = 1 MHz */
    timer_config.counterMode = TMR_COUNTER_MODE_UP;
    timer_config.period = FLOW_T35_TIMER_US - 1U;
    timer_config.clockDivision = TMR_CKD_DIV1;
    timer_config.repetitionCounter = 0U;
    TMR_ConfigTimeBase(TMR16, &timer_config);
    TMR_ClearStatusFlag(TMR16, TMR_FLAG_UPDATE);
    TMR_EnableInterrupt(TMR16, TMR_INT_UPDATE);
    TMR_Disable(TMR16);

    NVIC_EnableIRQRequest(USART1_IRQn, 2U);
    NVIC_EnableIRQRequest(USART2_IRQn, 2U);
    NVIC_EnableIRQRequest(USART3_6_IRQn, 2U);
    NVIC_EnableIRQRequest(DMA1_CH2_3_IRQn, 1U);
    NVIC_EnableIRQRequest(DMA1_CH4_5_IRQn, 1U);
    NVIC_EnableIRQRequest(TMR16_IRQn, 2U);
}

uint16_t BSP_Uart_Available(BSP_UartPort_T port)
{
    BSP_RingBuffer_T *ring;

    ring = Uart_GetRing(port);
    if (ring == 0)
    {
        return 0U;
    }
    return BSP_RingBuffer_Available(ring);
}

uint16_t BSP_Uart_Read(BSP_UartPort_T port, uint8_t *data, uint16_t max_length)
{
    BSP_RingBuffer_T *ring;

    if ((data == 0) || (max_length == 0U))
    {
        return 0U;
    }
    ring = Uart_GetRing(port);
    if (ring == 0)
    {
        return 0U;
    }
    return BSP_RingBuffer_Read(ring, data, max_length);
}

void BSP_Uart_Write(BSP_UartPort_T port, const uint8_t *data, uint16_t length)
{
    USART_T *usart;
    uint16_t index;

    if ((data == 0) || (length == 0U))
    {
        return;
    }
    usart = Uart_GetPeripheral(port);
    if (usart == 0)
    {
        return;
    }

    for (index = 0U; index < length; index++)
    {
        while (USART_ReadStatusFlag(usart, USART_FLAG_TXBE) == RESET)
        {
        }
        USART_TxData(usart, data[index]);
    }
    while (USART_ReadStatusFlag(usart, USART_FLAG_TXC) == RESET)
    {
    }
}

uint32_t BSP_Uart_OverflowCount(BSP_UartPort_T port)
{
    BSP_RingBuffer_T *ring;

    ring = Uart_GetRing(port);
    if (ring == 0)
    {
        return 0U;
    }
    return BSP_RingBuffer_OverflowCount(ring);
}

void BSP_Uart_GetStats(BSP_UartPort_T port, BSP_UartStats_T *stats)
{
    uint8_t index;

    if (stats == 0)
    {
        return;
    }

    stats->rx_bytes = 0U;
    stats->idle_events = 0U;
    stats->dma_full_events = 0U;
    stats->ring_overflow = 0U;

    index = (uint8_t)port;
    if (index >= UART_STATS_PORT_COUNT)
    {
        return;
    }

    stats->rx_bytes = g_uart_rx_bytes[index];
    stats->idle_events = g_uart_idle_events[index];
    stats->dma_full_events = g_uart_dma_full_events[index];
    stats->ring_overflow = BSP_Uart_OverflowCount(port);
}

int BSP_Flow_ReadFrame(uint8_t *data, uint16_t capacity)
{
    uint16_t frame_length;
    uint16_t count;
    uint8_t next;

    if (g_flow_frame_tail == g_flow_frame_head)
    {
        return 0;
    }

    frame_length = g_flow_frame_lengths[g_flow_frame_tail];
    if ((data == 0) || (capacity < frame_length))
    {
        return -1;
    }

    count = BSP_RingBuffer_Read(&g_flow_rb, data, frame_length);
    if (count != frame_length)
    {
        BSP_RingBuffer_Reset(&g_flow_rb);
        g_flow_frame_head = 0U;
        g_flow_frame_tail = 0U;
        return -2;
    }

    next = (uint8_t)(g_flow_frame_tail + 1U);
    if (next >= FLOW_FRAME_QUEUE_SIZE)
    {
        next = 0U;
    }
    g_flow_frame_tail = next;
    return (int)frame_length;
}

void BSP_Uart_Usart1IRQHandler(void)
{
    if (USART_ReadIntFlag(USART1, USART_INT_FLAG_RXBNE) == SET)
    {
        g_uart_rx_bytes[BSP_UART_DEBUG]++;
        (void)BSP_RingBuffer_Push(&g_debug_rb, (uint8_t)USART_RxData(USART1));
    }
}

void BSP_Uart_Usart2IRQHandler(void)
{
    if (USART_ReadIntFlag(USART2, USART_INT_FLAG_RXBNE) == SET)
    {
        g_uart_rx_bytes[BSP_UART_BLUETOOTH]++;
        (void)BSP_RingBuffer_Push(&g_bt_rb, (uint8_t)USART_RxData(USART2));
    }
}

void BSP_Uart_Usart3To6IRQHandler(void)
{
    uint8_t byte;
    uint16_t received;

    if (USART_ReadIntFlag(USART3, USART_INT_FLAG_IDLE) == SET)
    {
        USART_ClearStatusFlag(USART3, USART_FLAG_IDLEF);
        received = Dma_FlushToRing(DMA1_CHANNEL_3, g_modem_dma, MODEM_DMA_SIZE,
                                   DMA1_INT_FLAG_TF3, &g_modem_rb);
        g_uart_idle_events[BSP_UART_4G]++;
        g_uart_rx_bytes[BSP_UART_4G] += received;
    }

    if (USART_ReadIntFlag(USART4, USART_INT_FLAG_IDLE) == SET)
    {
        USART_ClearStatusFlag(USART4, USART_FLAG_IDLEF);
        received = Dma_FlushToRing(DMA1_CHANNEL_5, g_gnss_dma, GNSS_DMA_SIZE,
                                   DMA1_INT_FLAG_TF5, &g_gnss_rb);
        g_uart_idle_events[BSP_UART_GNSS]++;
        g_uart_rx_bytes[BSP_UART_GNSS] += received;
    }

    if (USART_ReadIntFlag(USART5, USART_INT_FLAG_RXBNE) == SET)
    {
        byte = (uint8_t)USART_RxData(USART5);
        g_uart_rx_bytes[BSP_UART_FLOW]++;
        if (BSP_RingBuffer_Push(&g_flow_rb, byte) != 0U)
        {
            if (g_flow_current_length < 0xFFFFU)
            {
                g_flow_current_length++;
            }
        }
        else
        {
            g_flow_current_valid = 0U;
        }
        Flow_RestartGapTimer();
    }
}

void BSP_Uart_DmaCh2To3IRQHandler(void)
{
    uint16_t received;

    if (DMA_ReadIntFlag(DMA1_INT_FLAG_TF3) == SET)
    {
        received = Dma_FlushToRing(DMA1_CHANNEL_3, g_modem_dma, MODEM_DMA_SIZE,
                                   DMA1_INT_FLAG_TF3, &g_modem_rb);
        g_uart_dma_full_events[BSP_UART_4G]++;
        g_uart_rx_bytes[BSP_UART_4G] += received;
    }
}

void BSP_Uart_DmaCh4To5IRQHandler(void)
{
    uint16_t received;

    if (DMA_ReadIntFlag(DMA1_INT_FLAG_TF5) == SET)
    {
        received = Dma_FlushToRing(DMA1_CHANNEL_5, g_gnss_dma, GNSS_DMA_SIZE,
                                   DMA1_INT_FLAG_TF5, &g_gnss_rb);
        g_uart_dma_full_events[BSP_UART_GNSS]++;
        g_uart_rx_bytes[BSP_UART_GNSS] += received;
    }
}

void BSP_Uart_Tmr16IRQHandler(void)
{
    if (TMR_ReadStatusFlag(TMR16, TMR_FLAG_UPDATE) == SET)
    {
        TMR_ClearStatusFlag(TMR16, TMR_FLAG_UPDATE);
        TMR_Disable(TMR16);

        if (g_flow_current_length > 0U)
        {
            if (g_flow_current_valid != 0U)
            {
                Flow_QueueFrameLength(g_flow_current_length);
            }
            else
            {
                BSP_RingBuffer_Reset(&g_flow_rb);
                g_flow_frame_head = 0U;
                g_flow_frame_tail = 0U;
            }
        }
        g_flow_current_length = 0U;
        g_flow_current_valid = 1U;
    }
}
