# TankerFlow APM32F030RC Firmware

## Phase-3B baseline: GNSS NMEA stream parser on the Phase-3A BSP

Phase-3B builds directly on the committed Phase-3A BSP.  The scope is deliberately limited to the GNSS software receive path: USART4 DMA+IDLE ring-buffer data is consumed in foreground context, framed into NMEA sentences, checksum-validated, and minimally decoded for RMC/GGA.  Bootloader, OTA/FOTA, 4G AT state machine, Bluetooth business protocol, flowmeter Modbus register parsing, server protocol and application state machine remain out of scope.

### Build target

- MCU: APM32F030CCT6 / MDK target `APM32F030RC`
- SYSCLK: 48 MHz
- SysTick: 1 ms
- Debug: USART1 PA9/PA10, 115200 8N1
- App version: `0.3.1-phase3b`

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
| SGM41511 | I2C1 PB8/PB9, CHARGE_INT PA11, CHARGE_EN PA12 | 100 kHz; nCE active low; nINT falling-edge IRQ |
| ADC | PA6=ADC_IN6, PA7=ADC_IN7, PB0=ADC_IN8, PB1=ADC_IN9 | 12-bit checked/average/raw read |

`Firmware/Include/board_pinmap.h` remains the only source of truth for physical pin assignments.


### Phase-3B GNSS parser

- `Drivers/GNSS/gnss.*`: stream framing/integration layer.
  - foreground-only consumption from `BSP_UART_GNSS`; ISR/DMA code stays protocol-free;
  - accepts arbitrary chunks, including split sentences, concatenated sentences and garbage prefixes;
  - resynchronizes on `$` and drops overlength lines safely;
  - exposes `GNSS_Info_T`, `GNSS_Stats_T` and `GNSS_IsFresh()`;
  - uses fixed static storage only.
- `Drivers/GNSS/nmea_parser.*`: pure NMEA sentence decoder.
  - validates XOR checksum before parsing;
  - supports RMC and GGA from GN/GP and other standard two-character talker IDs;
  - converts latitude/longitude to signed degrees x 1e7 without floating point;
  - converts RMC speed from knots to integer cm/s;
  - records UTC/date/fix quality/satellite count as available.
- `Firmware/Tests/gnss_host_test.c`: host-side regression vectors for normal/invalid RMC, GGA, bad checksum, split/concatenated packets, garbage-prefix resync, overlength recovery, hemisphere conversion and stale-fix logic.

The production GNSS path intentionally does not use `malloc`, `sscanf`, `atof`, `float` or `double`.  NMEA parsing is never performed from USART/DMA interrupt context.

`last_update_ms` is refreshed only when a sentence carries a valid position.  Invalid-fix sentences can mark `valid=0` without erasing the last coordinates, allowing the application to distinguish last-known position from a current fresh fix.

### Phase-3A modules

- `bsp_selftest.*`: one-shot startup diagnostics for charger, ADC and UART receive counters.
- `Drivers/SGM41511/sgm41511.*`: minimal charger register driver.
  - probe REG0B and verify SGM41511 part ID;
  - enable/disable charging through both CHG_CONFIG and board nCE;
  - set/read input current limit (IINDPM);
  - set/read fast-charge current (ICHG);
  - read REG08/REG0A status;
  - read REG09 twice so latched-history and current fault are separated.
- `bsp_gpio.*`: PA11 charger nINT falling-edge interrupt only sets foreground flags/counters; no I2C operation is done in ISR context.
- `bsp_adc.*`: readiness state, checked raw read, N-sample average and ADC-pin millivolt conversion.
- `bsp_uart.*`: RX byte, DMA-IDLE, DMA-full and ring-overflow diagnostics for each UART.
- `bsp_i2c.*`: explicit error codes/error counter and transfer-abort cleanup on NACK/timeout/bus errors.

### ADC voltage rule

Phase-3A reports **ADC pin voltage only**:

```text
pin_mV = raw * VDDA_mV / 4095
```

The startup self-test uses nominal `VDDA=3300mV`.  It intentionally does **not** convert ADC_IN6..ADC_IN9 into battery/input/system voltages because the final external resistor-divider ratios have not been frozen in software.  Do not add guessed divider coefficients.  When the schematic ratios are confirmed, board-level engineering-unit conversion will be added as a separate reviewed change.

### SGM41511 safety rule

The BSP power-up state keeps `CHARGE_EN/nCE` high, so charging remains disabled during bring-up.  The startup self-test only reads charger identification/status/current settings and does not silently enable charging or overwrite charge parameters.

The dedicated SGM41511 APIs exist for later controlled bench tests.  Charging is enabled only if both the IC `CHG_CONFIG` bit is set and the board nCE pin is driven low.

### Expected Phase-3A boot diagnostics

Values depend on the real board; an attached charger should produce a REG0B part number of `2` with the SGMPART bit set.

```text
[BOOT] tanker-flow-mcu 0.3.0-phase3a
...
[PASS] phase-3A BSP/driver initialization complete
[SELF] phase-3A peripheral self-test start
[SELF][GPIO] CHG_INT level=... irq_count=...; charger nCE remains disabled by BSP safe state
[SELF][CHG ] REG0B=0x.. PN=2 REV=.. device=SGM41511 PASS
[SELF][CHG ] REG08=0x.. REG0A=0x.. VBUS=.. CHG=.. PG=.. VBUS_GD=..
[SELF][CHG ] FAULT latched=0x.. current=0x..
[SELF][CHG ] IINDPM=...mA ICHG=...mA
[SELF][ADC ] ADC_IN6 raw=... pin_mv=...
[SELF][ADC ] ADC_IN7 raw=... pin_mv=...
[SELF][ADC ] ADC_IN8 raw=... pin_mv=...
[SELF][ADC ] ADC_IN9 raw=... pin_mv=...
[SELF][UART] ...
[SELF] phase-3A result=PASS
```

If SGM41511 is absent, I2C wiring/power is wrong, or ADC initialization fails, self-test reports `FAIL` but deliberately continues into the 1-second heartbeat loop so the board remains observable for bench diagnosis.

### Recommended Phase-3A bench order

1. Clean/Rebuild the `APM32F030RC` target and verify the Phase-3A boot log on USART1.
2. Confirm PA12 (`CHARGE_EN/nCE`) stays high after reset; Phase-3A must not automatically start charging.
3. Confirm I2C1 PB8/PB9 waveforms at 100 kHz while the startup self-test reads SGM41511.
4. Verify REG0B identifies the SGM41511.  Then check REG08/REG0A and the two REG09 reads against the charger/input state.
5. Trigger/remove charger input and confirm PA11 nINT count changes; ISR must remain short and no I2C transaction may occur inside the interrupt handler.
6. Measure PA6/PA7/PB0/PB1 with a multimeter and compare with reported `pin_mv` before adding any divider scaling.
7. Feed known serial data to each UART and use `BSP_Uart_GetStats()` to distinguish no-data, IDLE/DMA behavior and ring overflow.
8. Phase-3B can be host-tested without hardware.  When a board is available, feed/observe real ATGM336H NMEA on USART4 and compare `GNSS_Stats_T` with the UART DMA/IDLE counters.

### Phase-3B host validation

From `Applications/TankerFlow/Firmware` on a host with GCC:

```text
gcc -std=c99 -Wall -Wextra -Werror -IInclude -IDrivers/GNSS Tests/gnss_host_test.c Drivers/GNSS/gnss.c Drivers/GNSS/nmea_parser.c -o gnss_host_test
./gnss_host_test
```

Expected final line: `GNSS host tests: PASS`.

Do not add Bootloader/OTA, GNSS vendor configuration commands, coordinate-map conversion, 4G upload or other business protocols in this phase.
