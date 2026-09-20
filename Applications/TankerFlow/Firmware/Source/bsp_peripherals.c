#include "bsp_peripherals.h"

#include "bsp_adc.h"
#include "bsp_gpio.h"
#include "bsp_i2c.h"
#include "bsp_uart.h"

void BSP_Peripherals_Init(void)
{
    BSP_BoardGpio_Init();
    BSP_Uart_Init();
    BSP_I2C1_Init();
    BSP_Adc_Init();
}
