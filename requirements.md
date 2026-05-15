# Aquarium Basket Lift Firmware Requirements

This file is a short implementation brief for Codex. The authoritative design is:

- `Auto-lift-wiring-design.md`
- `pinmap.md`
- `bringup-checklist.md`
- `AGENTS.md`

## Project Target

Build STM32F103C8T6 firmware for an automatic fish basket lift. The firmware controls basket height, not the whole aquarium water level. The target is to make the active water depth inside the basket slowly become shallower for juvenile bichirs.

## Hardware

- MCU: STM32F103C8T6 minimum system board.
- Toolchain: Keil5, STM32 Standard Peripheral Library.
- Display: 0.96 inch OLED on software I2C, fixed on `PB8/PB9`.
- Sensors: three WF5805F absolute pressure sensors.
- WF5805F module type: purchased 4-pin module with only `VDD/GND/SCL/SDA`.
- WF5805F address: fixed, official reference driver uses 8-bit write address `0xDA`, corresponding to 7-bit address `0x6D`.
- Because all three WF5805F modules have the same address, each sensor must be isolated on its own software I2C bus. Do not place two WF5805F modules on the same I2C bus.
- OLED-I2C: `PB8/PB9` for OLED only.
- I2C-A: `PA1/PA2` for `P_air`.
- I2C-B: `PB6/PB7` for `P_basket`.
- I2C-C: `PA8/PA9` for `P_tank`.
- Motor driver: one UM244 driver.
- Motors: two 42HSC1409-250NE2 captive linear stepper motors connected in parallel to the same driver output.
- UM244 `PU-/DR-/MF-` low-side signal conversion uses three independent single-channel NPN optocoupler modules from `Materials/npn型光耦隔离器-用于给步进电机驱动器的拉低信号转换.jpg`.
- The optocoupler input side is powered from STM32 3.3V; the optocoupler output side is powered from the 5V signal rail and pulls UM244 `PU-/DR-/MF-` low.
- During bring-up, each active UM244 minus input must measure `0-0.5V`; otherwise the optocoupler module must be replaced or followed by a transistor/MOSFET stage.
- Limits: four 24V NPN limit switches, isolated into STM32 through 24V NPN input optocoupler modules.
- The module shown in `Materials/npn型光耦隔离器-用于限位器信号输入.jpg` is acceptable for limit input when using the 24V-input variant shown in `Materials/npn型光耦隔离器-用于限位器信号输入（详细版）.jpg`; `Materials/光耦隔离器原理图.jpg` shows an MCU-side pull-up, so its MCU-side `VCC` and any pull-up must use 3.3V.
- Each limit switch must use an independent optocoupler input channel and an independent STM32 GPIO.
- Alarm: low-level-trigger active buzzer module on `PA0`.
- Status LEDs: LED1 on `PA6`, LED2 on `PA7`; LED anode through a current-limiting resistor to 3.3V, cathode to GPIO, low GPIO level turns LED on and high level turns LED off.
- Power: 220V AC to 24V 5A supply; 24V to 5V buck powers the STM32 minimum system board `5V` pin and UM244 `PU+/DR+/MF+` signal common positive terminals.
- 3.3V peripherals: OLED, WF5805F modules, and buzzer module must be powered from the minimum system board 3.3V rail or a dedicated 3.3V regulator, not from 5V.
- Grounding: 24V supply negative, 5V buck GND, STM32 GND, and UM244 signal-side reference GND must have a defined common reference. 24V limit switch outputs still enter STM32 only through optocoupler isolation.

## Control Requirements

- Calculate `basket_depth_mm` from `P_basket - P_air`.
- Calculate `tank_depth_mm` from `P_tank - P_air`.
- Default initial target depth: `100mm`.
- Default final target depth: `10mm`.
- Default shallowing rate: `1mm/day`.
- Maximum shallowing rate: `2mm/day`.
- Target setting range: `8-100mm`.
- Control tolerance: `±1mm`.
- No RTC in the current version. Running days and daily progress are based only on powered-on runtime.
- Do not compensate missed movement during power loss.

## Motion Requirements

- Leadscrew pitch: `2mm/rev`.
- UM244 microstep setting: `1600 pulse/rev`.
- Motion scale: `800 pulse/mm`.
- Default nap movement: `8 pulse = 0.01mm`.
- Maximum single nap movement: `16 pulse`.
- Default nap interval at `1mm/day` and `8 pulse`: about `14.4min`.
- Minimum nap interval: `5min`.
- Automatic nap pulse frequency: default `800Hz`, allow fallback to `400Hz`.
- Automatic nap bursts of `1-16 pulse` do not use acceleration or deceleration.
- `DIR` setup and hold time must be at least `5ms` around STEP output.
- `APP_NAP_MOVE` must use a busy lock so one nap burst cannot be triggered twice.
- Manual speed: only one speed, `1mm/s = 800 pulse/s`.
- Homing speed: default `0.5mm/s`, not higher than `1mm/s`.
- Automatic mode must not continuously run the motor at very low speed. Use nap-mode motion.
- Key scan period: `10ms`; key debounce stable time: `25ms`.
- Key pins: `PB11/PB10/PB1/PB0`, grouped on the minimum system board top header.
- Short key press: `25-1000ms`; normal long press: `>=1000ms`; maintenance entry: `PB10 >=3000ms`.
- Limit input sample period: `5-10ms`; trigger confirm `20ms`; release confirm `50ms`.
- During motion, a raw active limit in the current movement direction must stop motion immediately, then the filtered state is used for fault display.

## Safety Requirements

- Stop automatic motion on any severe fault.
- Stop upward motion on any upper limit trigger.
- Stop downward motion on any lower limit trigger.
- Stop all motion when left/right same-direction limits disagree.
- Stop automatic motion on repeated sensor read failure.
- Stop automatic motion on repeated I2C recovery failure.
- Stop automatic motion on tank low/high water, basket low/high water, water jump, pressure physical anomaly, or stall detection.
- Stop automatic motion and raise `E_DEPTH_TRACKING` when target tracking error exceeds `±1mm`, restart depth difference exceeds `3mm`, or automatic/recovery depth freshness waits time out.
- Buzzer silence must not clear fault state.
- Manual movement after alarm is only allowed inside maintenance mode.
- Limit protection must never be ignored, even in maintenance mode.
- Do not release the motor in automatic mode.

## Water Safety Thresholds

- `tank_min_depth_mm = 250`
- `tank_max_depth_mm = 450`
- `basket_min_safe_depth_mm = 5`
- `basket_max_safe_depth_mm = 120`
- Water jump threshold: `10mm/min`
- Sensor consecutive failure alarm: `5` failures
- I2C reinitialization failure alarm: `5` failures
- Restart depth difference threshold: `3mm`

## Required Firmware Modules

- Board configuration header for pins, levels, and constants.
- Software I2C for OLED plus three independent WF5805F sensor buses.
- WF5805F pressure sensor driver.
- Water depth calculation and filtering.
- Limit switch input and safety logic.
- UM244 stepper pulse control.
- Nap-mode scheduler.
- Mechanical homing and position tracking.
- Active buzzer driver.
- OLED pages.
- Key scanning and menu logic.
- Flash parameter storage with A/B backup.

## Reference Code Inputs

- Read `Reference/WF5805_2BAR官方驱动包` before implementing the WF5805F driver.
- Read `Reference/步进电机驱动示例` before implementing UM244 STEP/DIR/MF timing and timer setup.
- Reference code may guide Standard Peripheral Library calls and device command formats, but blocking `Delay` loops and busy waits must not be copied into new firmware modules.
- Error manager with fixed error codes.
- Main application state machine.

## State Machine

Required states:

- `APP_SELF_TEST`
- `APP_PAUSED`
- `APP_AUTO_RUN`
- `APP_NAP_WAIT`
- `APP_NAP_MOVE`
- `APP_MANUAL`
- `APP_MAINTENANCE`
- `APP_MOTOR_RELEASE`
- `APP_FAULT`

Main control logic must live in the state machine, not inside interrupts or display code.

## Flash Persistence

- Reserve the last two 1KB Flash pages.
- Page A: `0x0800F800`.
- Page B: `0x0800FC00`.
- Keil IROM should reserve only `0x08000000` size `0x0000F800` for code.
- Use `magic/version/seq/crc16`.
- Save parameter changes immediately.
- Save runtime state at most every 10 minutes, plus important transitions.
- Do not write Flash after every nap pulse group.

## Power Recovery

After reboot, run self-test first. If the previous state was automatic, all sensors and limits are normal, position is trusted, and depth difference is within `3mm`, automatically resume automatic running. Otherwise enter pause or fault and wait for human confirmation.

## Non-Goals For Current Version

- No RTC module.
- No 24V current detection module.
- No two-driver independent motor synchronization.
- No automatic movement during power loss.
- No automatic homing on every boot.


//   ${workspaceFolder}/**
//   "STM32F10X_MD",
