#include "bsp_debug_uart.h"

#include "board_pinmap.h"
#include "apm32f0xx_gpio.h"
#include "apm32f0xx_rcm.h"
#include "apm32f0xx_usart.h"

#define DEBUG_USART_BAUDRATE    (115200U)

void BSP_DebugUart_Init(void)
{
    GPIO_Config_T gpio_config;
    USART_Config_T usart_config;

    RCM_EnableAHBPeriphClock(RCM_AHB_PERIPH_GPIOA);
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_USART1);

    GPIO_ConfigPinAF(BOARD_DBG_TX_PORT, BOARD_DBG_TX_SOURCE, BOARD_DBG_TX_AF);
    GPIO_ConfigPinAF(BOARD_DBG_RX_PORT, BOARD_DBG_RX_SOURCE, BOARD_DBG_RX_AF);

    GPIO_ConfigStructInit(&gpio_config);
    gpio_config.pin = BOARD_DBG_TX_PIN;
    gpio_config.mode = GPIO_MODE_AF;
    gpio_config.outtype = GPIO_OUT_TYPE_PP;
    gpio_config.speed = GPIO_SPEED_50MHz;
    gpio_config.pupd = GPIO_PUPD_PU;
    GPIO_Config(BOARD_DBG_TX_PORT, &gpio_config);

    gpio_config.pin = BOARD_DBG_RX_PIN;
    GPIO_Config(BOARD_DBG_RX_PORT, &gpio_config);

    USART_ConfigStructInit(&usart_config);
    usart_config.baudRate = DEBUG_USART_BAUDRATE;
    usart_config.hardwareFlowCtrl = USART_FLOW_CTRL_NONE;
    usart_config.mode = USART_MODE_TX_RX;
    usart_config.parity = USART_PARITY_NONE;
    usart_config.stopBits = USART_STOP_BIT_1;
    usart_config.wordLength = USART_WORD_LEN_8B;
    USART_Config(BOARD_DBG_USART, &usart_config);
    USART_Enable(BOARD_DBG_USART);
}

void BSP_DebugUart_WriteByte(uint8_t data)
{
    while (USART_ReadStatusFlag(BOARD_DBG_USART, USART_FLAG_TXBE) == RESET)
    {
    }
    USART_TxData(BOARD_DBG_USART, data);
}

void BSP_DebugUart_Write(const char *text)
{
    if (text == 0)
    {
        return;
    }

    while (*text != '\0')
    {
        BSP_DebugUart_WriteByte((uint8_t)*text);
        ++text;
    }
}

void BSP_DebugUart_WriteUInt32(uint32_t value)
{
    char buffer[10];
    uint8_t length;

    if (value == 0U)
    {
        BSP_DebugUart_WriteByte((uint8_t)'0');
        return;
    }

    length = 0U;
    while ((value > 0U) && (length < (uint8_t)sizeof(buffer)))
    {
        buffer[length] = (char)('0' + (value % 10U));
        value /= 10U;
        length++;
    }

    while (length > 0U)
    {
        length--;
        BSP_DebugUart_WriteByte((uint8_t)buffer[length]);
    }
}

void BSP_DebugUart_WriteHex8(uint8_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    BSP_DebugUart_WriteByte((uint8_t)hex[(value >> 4U) & 0x0FU]);
    BSP_DebugUart_WriteByte((uint8_t)hex[value & 0x0FU]);
}
