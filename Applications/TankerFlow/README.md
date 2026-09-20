# Tanker Flow MCU firmware

Target: APM32F030RC (APM32F030xC), bare-metal firmware.

## Phase 1A scope

- 48 MHz system clock using the SDK system clock implementation.
- 1 ms SysTick time base.
- USART1 debug console on PA9/PA10, 115200 8N1, polling TX.
- Boot banner and one-second heartbeat for board-level verification.

Bootloader, OTA, modem/GNSS parsers, RS485/Modbus, Bluetooth protocol,
charger control, battery ADC, and product business logic are intentionally
outside this first bring-up commit.

## Build and board check

Open `Firmware/Project/MDK/TankerFlow.uvprojx`, select target
`APM32F030RC`, build, flash, and connect a 3.3 V UART adapter to USART1.

Expected output:

```text
[BOOT] tanker-flow-mcu 0.1.0-bringup
[BSP ] APM32F030xC clock=48MHz tick=1ms
[UART] USART1 PA9/PA10 115200 8N1
[PASS] phase-1 minimal bring-up started
[ALIVE] 1s
```

The PA9/PA10 assignment follows the current USART1 debug baseline and must be
checked against the final product schematic before flashing production boards.
