# Pin Map

This file is the short pin reference for firmware generation. The authoritative behavior is in `Auto-lift-wiring-design.md`.

The pin layout follows `Materials/最小系统板.png`: keep OLED on `PB8/PB9`, group each pressure-sensor I2C pair on adjacent pins, group UM244 STEP/DIR/MF on adjacent `PA3/PA4/PA5`, and keep the four limit inputs on adjacent `PB12-PB15`.

## STM32F103C8T6 Pins

| Function          | Pin              | Direction    | Electrical Interface                                 | Active Level / Notes                                  |
| ----------------- | ---------------- | ------------ | ---------------------------------------------------- | ----------------------------------------------------- |
| OLED-I2C SCL      | `PB8`            | Output/input | Software I2C, open-drain style                       | OLED only; 4.7k pull-up to 3.3V                       |
| OLED-I2C SDA      | `PB9`            | Output/input | Software I2C, open-drain style                       | OLED only; 4.7k pull-up to 3.3V                       |
| I2C-A SCL         | `PA6`            | Output/input | Software I2C, open-drain style                       | `P_air`; adjacent to `PA7`; 4.7k pull-up to 3.3V      |
| I2C-A SDA         | `PA7`            | Output/input | Software I2C, open-drain style                       | `P_air`; adjacent to `PA6`; 4.7k pull-up to 3.3V      |
| I2C-B SCL         | `PB6`            | Output/input | Software I2C, open-drain style                       | `P_basket`; 4.7k pull-up to 3.3V                      |
| I2C-B SDA         | `PB7`            | Output/input | Software I2C, open-drain style                       | `P_basket`; 4.7k pull-up to 3.3V                      |
| I2C-C SCL         | `PA8`            | Output/input | Software I2C, open-drain style                       | `P_tank`; adjacent to `PA9`; 4.7k pull-up to 3.3V     |
| I2C-C SDA         | `PA9`            | Output/input | Software I2C, open-drain style                       | `P_tank`; adjacent to `PA8`; 4.7k pull-up to 3.3V     |
| UM244 STEP        | `PA3 / TIM2_CH4` | Output       | 3.3V GPIO drives single-channel NPN optocoupler      | Sends active-low pulse to UM244 `PU-`; adjacent group |
| UM244 DIR         | `PA4`            | Output       | 3.3V GPIO drives single-channel NPN optocoupler      | Direction must be verified during bring-up            |
| UM244 MF/release  | `PA5`            | Output       | 3.3V GPIO drives single-channel NPN optocoupler      | Default high; do not release in automatic mode        |
| Left upper limit  | `PB12`           | Input        | Optocoupler output, pull-up                          | Low = triggered                                       |
| Left lower limit  | `PB13`           | Input        | Optocoupler output, pull-up                          | Low = triggered                                       |
| Right upper limit | `PB14`           | Input        | Optocoupler output, pull-up                          | Low = triggered                                       |
| Right lower limit | `PB15`           | Input        | Optocoupler output, pull-up                          | Low = triggered                                       |
| Key 1             | `PB1`            | Input        | Existing key                                         | Menu/decrease/manual down                             |
| Key 2             | `PB11`           | Input        | Existing key                                         | Menu/increase/manual up                               |
| Pause/confirm key | `PB10`           | Input        | New key                                              | Pause/confirm/alarm silence/maintenance entry         |
| Page/menu key     | `PB0`            | Input        | New key                                              | Page switch/cancel                                    |
| Active buzzer     | `PA0`            | Output       | Direct to low-level-trigger active buzzer module I/O | High = off, low = on                                  |
| LED1              | `PA1`            | Output       | Existing LED                                         | Status indication                                     |
| LED2              | `PA2`            | Output       | Existing LED                                         | Status indication                                     |
| SWDIO             | `PA13`           | Debug        | SWD                                                  | Reserved, do not reuse                                |
| SWCLK             | `PA14`           | Debug        | SWD                                                  | Reserved, do not reuse                                |

## I2C Devices

| Bus      | Pins      | Device     | Address Selection          | 7-bit Address |
| -------- | --------- | ---------- | -------------------------- | ------------- |
| OLED-I2C | `PB8/PB9` | OLED       | Fixed                      | `0x3C`        |
| I2C-A    | `PA6/PA7` | `P_air`    | 4-pin module fixed address | `0x6D`        |
| I2C-B    | `PB6/PB7` | `P_basket` | 4-pin module fixed address | `0x6D`        |
| I2C-C    | `PA8/PA9` | `P_tank`   | 4-pin module fixed address | `0x6D`        |

Code must use 7-bit addresses internally. If an existing OLED driver uses `0x78`, treat it as the 8-bit write address for OLED `0x3C`. The WF5805 official reference driver uses `WFSensorIICDevice 0XDA`; treat that as the 8-bit write address for WF5805F `0x6D`, with read address `0xDB`.

Do not place two WF5805F modules on the same I2C bus. The purchased 4-pin modules do not expose `SDO/ADDR`, so their address cannot be changed by wiring. OLED stays on `PB8/PB9` and does not share an I2C bus with any WF5805F module.

## UM244 Signal Wiring

| UM244 Terminal | Wiring                                            |
| -------------- | ------------------------------------------------- |
| `PU+`          | +5V from the 24V-to-5V buck module                |
| `DR+`          | +5V from the 24V-to-5V buck module                |
| `MF+`          | +5V from the 24V-to-5V buck module                |
| `PU-`          | STEP optocoupler `OUT`, controlled by STM32 `PA3` |
| `DR-`          | DIR optocoupler `OUT`, controlled by STM32 `PA4`  |
| `MF-`          | MF optocoupler `OUT`, controlled by STM32 `PA5`   |

The same 24V-to-5V buck output also powers the STM32 minimum system board through its `5V` pin. UM244 input high level requires more than 4V, so STM32 3.3V GPIO must not directly drive `PU/DR/MF` input terminals.

Use three independent modules from `Materials/npn型光耦隔离器-用于给步进电机驱动器的拉低信号转换.jpg`. On each module, MCU-side `VCC` goes to 3.3V and `IO` goes to the STM32 pin. Output-side signal power goes to +5V, output-side GND goes to 5V/STM32 GND, and `OUT` goes to the matching UM244 minus terminal. The module inverts the signal: STM32 low = UM244 minus pulled low.

During bring-up, verify each active UM244 minus terminal measures `0-0.5V`. If not, do not use the optocoupler module directly for UM244 control.

## Limit Switch Logic

The four limit switches are 24V NPN sensors. Brown = +24V, blue = 24V 0V, black = NPN output. The black wire must enter the low-level signal/input-negative terminal of the 24V NPN input optocoupler module.

The module shown in `Materials/npn型光耦隔离器-用于限位器信号输入.jpg` matches this use case because it is an NPN input optocoupler module for external signals entering an MCU. Use the 24V-input variant from `Materials/npn型光耦隔离器-用于限位器信号输入（详细版）.jpg`. `Materials/光耦隔离器原理图.jpg` shows a pull-up resistor on the MCU side, so connect the MCU-side `VCC` and any pull-up to 3.3V.

Use one independent optocoupler channel per limit switch. A single-channel module requires four modules; a multi-channel module must expose four independent inputs and four independent `OUT-IO` outputs.

| Physical State | STM32 GPIO Read |
| -------------- | --------------- |
| Not triggered  | High `1`        |
| Triggered      | Low `0`         |

Safety rules:

- Any upper limit triggered: forbid upward movement.
- Any lower limit triggered: forbid downward movement.
- Left/right same-direction mismatch: stop all movement and raise `E_LIMIT_MISMATCH`.

## Buzzer

| Buzzer Module Pin | Connection |
| ----------------- | ---------- |
| `VCC`             | 3.3V       |
| `GND`             | GND        |
| `I/O`             | `PA0`      |

`PA0` high means buzzer off. `PA0` low means buzzer on. Initialize `PA0` high before any self-test beep. If reset-time silence is required, add an external pull-up on the buzzer `I/O` signal.
