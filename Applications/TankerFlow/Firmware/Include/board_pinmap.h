#ifndef BOARD_PINMAP_H
#define BOARD_PINMAP_H

#include "apm32f0xx.h"
#include "apm32f0xx_gpio.h"

/*
 * TankerFlow APM32F030CCT6 board pin truth table.
 * Keep all physical pin assignments in this file only.
 */

/* USART1 - debug console, 115200 8N1 */
#define BOARD_DBG_USART              USART1
#define BOARD_DBG_TX_PORT            GPIOA
#define BOARD_DBG_TX_PIN             GPIO_PIN_9
#define BOARD_DBG_TX_SOURCE          GPIO_PIN_SOURCE_9
#define BOARD_DBG_TX_AF              GPIO_AF_PIN1
#define BOARD_DBG_RX_PORT            GPIOA
#define BOARD_DBG_RX_PIN             GPIO_PIN_10
#define BOARD_DBG_RX_SOURCE          GPIO_PIN_SOURCE_10
#define BOARD_DBG_RX_AF              GPIO_AF_PIN1

/* USART2 - AC6323A Bluetooth, 115200 8N1 */
#define BOARD_BT_USART               USART2
#define BOARD_BT_TX_PORT             GPIOA
#define BOARD_BT_TX_PIN              GPIO_PIN_2
#define BOARD_BT_TX_SOURCE           GPIO_PIN_SOURCE_2
#define BOARD_BT_TX_AF               GPIO_AF_PIN1
#define BOARD_BT_RX_PORT             GPIOA
#define BOARD_BT_RX_PIN              GPIO_PIN_3
#define BOARD_BT_RX_SOURCE           GPIO_PIN_SOURCE_3
#define BOARD_BT_RX_AF               GPIO_AF_PIN1
#define BOARD_BT_EN_PORT             GPIOA
#define BOARD_BT_EN_PIN              GPIO_PIN_8

/* USART3 - MC610 4G, 115200 8N1, RX DMA1 channel 3 + IDLE */
#define BOARD_4G_USART               USART3
#define BOARD_4G_TX_PORT             GPIOB
#define BOARD_4G_TX_PIN              GPIO_PIN_10
#define BOARD_4G_TX_SOURCE           GPIO_PIN_SOURCE_10
#define BOARD_4G_TX_AF               GPIO_AF_PIN4
#define BOARD_4G_RX_PORT             GPIOB
#define BOARD_4G_RX_PIN              GPIO_PIN_11
#define BOARD_4G_RX_SOURCE           GPIO_PIN_SOURCE_11
#define BOARD_4G_RX_AF               GPIO_AF_PIN4
#define BOARD_4G_RST_PORT            GPIOA
#define BOARD_4G_RST_PIN             GPIO_PIN_4
#define BOARD_4G_PWRKEY_PORT         GPIOA
#define BOARD_4G_PWRKEY_PIN          GPIO_PIN_5
#define BOARD_4G_WAKE_IN_PORT        GPIOB
#define BOARD_4G_WAKE_IN_PIN         GPIO_PIN_12
#define BOARD_4G_WAKE_OUT_PORT       GPIOB
#define BOARD_4G_WAKE_OUT_PIN        GPIO_PIN_13

/* USART4 - ATGM336H GNSS, 9600 8N1, RX DMA1 channel 5 + IDLE */
#define BOARD_GNSS_USART             USART4
#define BOARD_GNSS_TX_PORT           GPIOA
#define BOARD_GNSS_TX_PIN            GPIO_PIN_0
#define BOARD_GNSS_TX_SOURCE         GPIO_PIN_SOURCE_0
#define BOARD_GNSS_TX_AF             GPIO_AF_PIN4
#define BOARD_GNSS_RX_PORT           GPIOA
#define BOARD_GNSS_RX_PIN            GPIO_PIN_1
#define BOARD_GNSS_RX_SOURCE         GPIO_PIN_SOURCE_1
#define BOARD_GNSS_RX_AF             GPIO_AF_PIN4
#define BOARD_GNSS_RST_PORT          GPIOB
#define BOARD_GNSS_RST_PIN           GPIO_PIN_6
#define BOARD_GNSS_EN_PORT           GPIOB
#define BOARD_GNSS_EN_PIN            GPIO_PIN_7

/* USART5 - flowmeter RS485, 9600 8E1, RXNE IRQ + TMR16 T3.5 */
#define BOARD_FLOW_USART             USART5
#define BOARD_FLOW_TX_PORT           GPIOB
#define BOARD_FLOW_TX_PIN            GPIO_PIN_3
#define BOARD_FLOW_TX_SOURCE         GPIO_PIN_SOURCE_3
#define BOARD_FLOW_TX_AF             GPIO_AF_PIN4
#define BOARD_FLOW_RX_PORT           GPIOB
#define BOARD_FLOW_RX_PIN            GPIO_PIN_4
#define BOARD_FLOW_RX_SOURCE         GPIO_PIN_SOURCE_4
#define BOARD_FLOW_RX_AF             GPIO_AF_PIN4
#define BOARD_RS485_TXEN_PORT        GPIOB
#define BOARD_RS485_TXEN_PIN         GPIO_PIN_5
#define BOARD_RS485_RXEN_PORT        GPIOA
#define BOARD_RS485_RXEN_PIN         GPIO_PIN_15
#define BOARD_FLOW_EN_PORT           GPIOB
#define BOARD_FLOW_EN_PIN            GPIO_PIN_2
#define BOARD_FLOW_DIR_PORT          GPIOB
#define BOARD_FLOW_DIR_PIN           GPIO_PIN_14
#define BOARD_INSERT_PORT            GPIOB
#define BOARD_INSERT_PIN             GPIO_PIN_15

/* SGM41511 charger / I2C1 */
#define BOARD_CHARGE_INT_PORT        GPIOA
#define BOARD_CHARGE_INT_PIN         GPIO_PIN_11
#define BOARD_CHARGE_EN_PORT         GPIOA
#define BOARD_CHARGE_EN_PIN          GPIO_PIN_12
#define BOARD_I2C1_SCL_PORT          GPIOB
#define BOARD_I2C1_SCL_PIN           GPIO_PIN_8
#define BOARD_I2C1_SCL_SOURCE        GPIO_PIN_SOURCE_8
#define BOARD_I2C1_SCL_AF            GPIO_AF_PIN1
#define BOARD_I2C1_SDA_PORT          GPIOB
#define BOARD_I2C1_SDA_PIN           GPIO_PIN_9
#define BOARD_I2C1_SDA_SOURCE        GPIO_PIN_SOURCE_9
#define BOARD_I2C1_SDA_AF            GPIO_AF_PIN1
#define BOARD_CHARGER_I2C_ADDR_7BIT  (0x6BU)

/* ADC raw inputs from schematic labels ADC_IN6..ADC_IN9. */
#define BOARD_ADC_IN6_PORT           GPIOA
#define BOARD_ADC_IN6_PIN            GPIO_PIN_6
#define BOARD_ADC_IN7_PORT           GPIOA
#define BOARD_ADC_IN7_PIN            GPIO_PIN_7
#define BOARD_ADC_IN8_PORT           GPIOB
#define BOARD_ADC_IN8_PIN            GPIO_PIN_0
#define BOARD_ADC_IN9_PORT           GPIOB
#define BOARD_ADC_IN9_PIN            GPIO_PIN_1

#endif /* BOARD_PINMAP_H */
