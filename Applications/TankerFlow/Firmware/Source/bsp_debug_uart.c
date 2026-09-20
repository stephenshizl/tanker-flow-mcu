#include "bsp_debug_uart.h"

#include "apm32f0xx_gpio.h"
#include "apm32f0xx_rcm.h"
#include "apm32f0xx_usart.h"

#define DEBUG_USART             USART1
#define DEBUG_USART_BAUDRATE    (115200U)
#define DEBUG_TX_PORT           GPIOA
#define DEBUG_TX_PIN            GPIO_PIN_9
#define DEBUG_TX_PIN_SOURCE     GPIO_PIN_SOURCE_9
#define DEBUG_TX_AF             GPIO_AF_PIN1
#define DEBUG_RX_PORT           GPIOA
#define DEBUG_RX_PIN            GPIO_PIN_10
#define DEBUG_RX_PIN_SOURCE     GPIO_PIN_SOURCE_10
#define DEBUG_RX_AF             GPIO_AF_PIN1

void BSP_DebugUart_Init(void)
{
    GPIO_Config_T gpio_config;
    USART_Config_T usart_config;

    RCM_EnableAHBPeriphClock(RCM_AHB_PERIPH_GPIOA);
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_USART1);

    GPIO_ConfigPinAF(DEBUG_TX_PORT, DEBUG_TX_PIN_SOURCE, DEBUG_TX_AF);
    GPIO_ConfigPinAF(DEBUG_RX_PORT, DEBUG_RX_PIN_SOURCE, DEBUG_RX_AF);

    gpio_config.pin = DEBUG_TX_PIN;
    gpio_config.mode = GPIO_MODE_AF;
    gpio_config.outtype = GPIO_OUT_TYPE_PP;
    gpio_config.speed = GPIO_SPEED_50MHz;
    gpio_config.pupd = GPIO_PUPD_PU;
    GPIO_Config(DEBUG_TX_PORT, &gpio_config);

    gpio_config.pin = DEBUG_RX_PIN;
    GPIO_Config(DEBUG_RX_PORT, &gpio_config);

    usart_config.baudRate = DEBUG_USART_BAUDRATE;
    usart_config.hardwareFlowCtrl = USART_FLOW_CTRL_NONE;
    usart_config.mode = USART_MODE_TX_RX;
    usart_config.parity = USART_PARITY_NONE;
    usart_config.stopBits = USART_STOP_BIT_1;
    usart_config.wordLength = USART_WORD_LEN_8B;
    USART_Config(DEBUG_USART, &usart_config);
    USART_Enable(DEBUG_USART);
}

void BSP_DebugUart_WriteByte(uint8_t data)
{
    while (USART_ReadStatusFlag(DEBUG_USART, USART_FLAG_TXBE) == RESET)
    {
    }

    USART_TxData(DEBUG_USART, data);
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
