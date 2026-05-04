# Pin Map

This file is the short pin reference for firmware generation. The authoritative behavior is in `docs/superpowers/specs/2026-05-04-aquarium-lift-wiring-design.md`.

## STM32F103C8T6 Pins

| Function | Pin | Direction | Electrical Interface | Active Level / Notes |
|---|---|---|---|---|
| I2C-A SCL | `PB8` | Output/input | Software I2C, open-drain style | OLED, `P_air`, `P_tank`; 4.7k pull-up to 3.3V |
| I2C-A SDA | `PB9` | Output/input | Software I2C, open-drain style | OLED, `P_air`, `P_tank`; 4.7k pull-up to 3.3V |
| I2C-B SCL | `PB6` | Output/input | Software I2C, open-drain style | `P_basket`; 4.7k pull-up to 3.3V |
| I2C-B SDA | `PB7` | Output/input | Software I2C, open-drain style | `P_basket`; 4.7k pull-up to 3.3V |
| UM244 STEP | `PA0 / TIM2_CH1` | Output | 3.3V GPIO drives NPN/level-shift input | Sends pulse to UM244 `PU-`; exact edge per UM244 manual |
| UM244 DIR | `PA3` | Output | 3.3V GPIO drives NPN/level-shift input | Direction must be verified during bring-up |
| UM244 MF/release | `PA4` | Output | 3.3V GPIO drives NPN/level-shift input | Motor release control; do not release in automatic mode |
| Left upper limit | `PB12` | Input | Optocoupler output, pull-up | Low = triggered |
| Left lower limit | `PB13` | Input | Optocoupler output, pull-up | Low = triggered |
| Right upper limit | `PB14` | Input | Optocoupler output, pull-up | Low = triggered |
| Right lower limit | `PB15` | Input | Optocoupler output, pull-up | Low = triggered |
| Key 1 | `PB1` | Input | Existing key | Menu/decrease/manual down |
| Key 2 | `PB11` | Input | Existing key | Menu/increase/manual up |
| Pause/confirm key | `PB10` | Input | New key | Pause/confirm/alarm silence/maintenance entry |
| Page/menu key | `PA7` | Input | New key | Page switch/cancel |
| Active buzzer | `PA5` | Output | Direct to low-level-trigger active buzzer module I/O | High = off, low = on |
| LED1 | `PA1` | Output | Existing LED | Status indication |
| LED2 | `PA2` | Output | Existing LED | Status indication |
| SWDIO | `PA13` | Debug | SWD | Reserved, do not reuse |
| SWCLK | `PA14` | Debug | SWD | Reserved, do not reuse |

## I2C Devices

| Bus | Pins | Device | Address Selection | 7-bit Address |
|---|---|---|---|---|
| I2C-A | `PB8/PB9` | OLED | Fixed | `0x3C` |
| I2C-A | `PB8/PB9` | `P_air` | `SDO/ADDR` to GND | `0x6C` |
| I2C-A | `PB8/PB9` | `P_tank` | `SDO/ADDR` to 3.3V | `0x6D` |
| I2C-B | `PB6/PB7` | `P_basket` | `SDO/ADDR` to GND | `0x6C` |

Code must use 7-bit addresses internally. If an existing OLED driver uses `0x78`, treat it as the 8-bit write address for OLED `0x3C`.

## UM244 Signal Wiring

| UM244 Terminal | Wiring |
|---|---|
| `PU+` | +5V |
| `DR+` | +5V |
| `MF+` | +5V |
| `PU-` | NPN collector or optocoupler output controlled by STM32 `PA0` |
| `DR-` | NPN collector or optocoupler output controlled by STM32 `PA3` |
| `MF-` | NPN collector or optocoupler output controlled by STM32 `PA4` |

UM244 input high level requires more than 4V, so STM32 3.3V GPIO must not directly drive `PU/DR/MF` input terminals.

## Limit Switch Logic

The four limit switches are 24V NPN sensors. Brown = +24V, blue = 24V 0V, black = NPN output. The black wire enters the 24V side of an optocoupler input circuit.

| Physical State | STM32 GPIO Read |
|---|---|
| Not triggered | High `1` |
| Triggered | Low `0` |

Safety rules:

- Any upper limit triggered: forbid upward movement.
- Any lower limit triggered: forbid downward movement.
- Left/right same-direction mismatch: stop all movement and raise `E_LIMIT_MISMATCH`.

## Buzzer

| Buzzer Module Pin | Connection |
|---|---|
| `VCC` | 3.3V |
| `GND` | GND |
| `I/O` | `PA5` |

`PA5` high means buzzer off. `PA5` low means buzzer on. Initialize `PA5` high before any self-test beep.
