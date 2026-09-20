# TankerFlow APM32F030RC Firmware

## Phase-2 baseline: peripheral BSP

This project is the second bring-up step for the tanker loading/unloading flow terminal.  The scope is deliberately limited to MCU BSP and peripheral access.  Bootloader, OTA/FOTA, 4G AT state machine, NMEA parser, Bluetooth business protocol, Modbus register parser, server protocol and application state machine are **not** included in this step.

### Build target

- MCU: APM32F030CCT6 / MDK target `APM32F030RC`
- SYSCLK: 48 MHz
- SysTick: 1 ms
- Debug: USART1 PA9/PA10, 115200 8N1
- App version: `0.2.0-bsp`

### Frozen board mapping

| Function | MCU resource | RX method / note |
|---|---|---|
| Debug | USART1 PA9/PA10 | RXNE IRQ |
| AC6323A Bluetooth | USART2 PA2/PA3, EN PA8 | 115200 8N1, RXNE IRQ |
| MC610 4G | USART3 PB10/PB11 | 115200 8N1, DMA1_CH3 + IDLE, 2048-byte ring |
| 4G control | RST PA4, PWRKEY PA5, WAKE_IN PB12, WAKE_OUT PB13 | GPIO |
| ATGM336H GNSS | USART4 PA0/PA1, EN PB7, RST PB6 | 9600 8N1, DMA1_CH5 + IDLE |
| Flowmeter RS485 | USART5 PB3/PB4 | 9600 8E1, RXNE IRQ + TMR16 Modbus T3.5 |
| RS485 / flow control | TXEN PB5, RXEN PA15, FLOW_EN PB2 | raw GPIO APIs |
| Flow direction / connector | DIR_FLOW PB14, DET_INSERT PB15 | inputs |
| SGM41511 | I2C1 PB8/PB9, CHARGE_INT PA11, CHARGE_EN PA12 | 100 kHz; nCE active low |
| ADC | PA6=ADC_IN6, PA7=ADC_IN7, PB0=ADC_IN8, PB1=ADC_IN9 | 12-bit raw read |

`Firmware/Include/board_pinmap.h` is the only source of truth for physical pin assignments.

### BSP modules

- `bsp_gpio.*`: external power/reset/wakeup/RS485/charger control and input GPIO.
- `bsp_uart.*`: USART1..5, IRQ RX, DMA+IDLE RX, Modbus T3.5 frame boundary.
- `bsp_ring_buffer.*`: ISR producer / foreground consumer byte rings.
- `bsp_i2c.*`: I2C1 blocking register access and SGM41511 raw register wrappers.
- `bsp_adc.*`: ADC_IN6..ADC_IN9 raw 12-bit access.
- `bsp_peripherals.*`: single BSP initialization entry point.
- `apm32f0xx_int.c`: IRQ routing only; protocol parsing stays out of ISR/BSP.

### Power-up safety policy

`BSP_BoardGpio_Init()` leaves externally powered modules in conservative states: Bluetooth/GNSS/flow power off, 4G reset and PWRKEY controls inactive, charger disabled (`nCE` high), RS485 control pins low.  Higher-level bring-up code can then enable one peripheral at a time.

### Expected boot log

```text
[BOOT] tanker-flow-mcu 0.2.0-bsp
[BSP ] APM32F030xC clock=48MHz tick=1ms
[UART] USART1 debug  PA9/PA10   115200 8N1 IRQ
[UART] USART2 BT     PA2/PA3     115200 8N1 IRQ
[UART] USART3 4G     PB10/PB11   115200 8N1 DMA+IDLE
[UART] USART4 GNSS   PA0/PA1       9600 8N1 DMA+IDLE
[UART] USART5 FLOW   PB3/PB4       9600 8E1 IRQ+TMR16
[I2C ] I2C1 PB8/PB9 100kHz, SGM41511 raw register API
[ADC ] ADC_IN6..9 raw 12-bit channels initialized
[GPIO] external power/control pins initialized to safe states
[PASS] phase-2 peripheral BSP initialized
```

### Recommended bench order

1. Verify the boot log and 1-second heartbeat on USART1.
2. Scope every control GPIO in its safe state, then toggle one peripheral at a time through the BSP API.
3. Bluetooth: enable PA8 and verify USART2 RX/TX at 115200.
4. 4G: verify PA4/PA5/PB12/PB13 and USART3 DMA+IDLE reception before adding the AT state machine.
5. GNSS: enable PB7, release PB6 reset and confirm NMEA bytes enter the USART4 ring.
6. Flowmeter: enable the flow supply, confirm RS485 control polarity on the real board, then verify USART5 8E1 and T3.5 frame boundaries.  The BSP intentionally exposes TXEN/RXEN as raw pin-level APIs until that polarity is bench-confirmed.
7. Read SGM41511 registers through the I2C raw API and read ADC_IN6..9 raw values.

Do not add Bootloader/OTA or business protocols until this BSP baseline is reviewed and the board-level tests above pass.
