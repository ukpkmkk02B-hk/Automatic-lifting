# AGENTS.md

This is a Keil5 STM32F103C8T6 Standard Peripheral Library project.

## Required Reading Before Code Generation

Before writing or modifying code, read and follow:

- `skill.md`
- `codex-codegen-execution.md`
- `requirements.md`
- `pinmap.md`
- `bringup-checklist.md`
- `Auto-lift-wiring-design.md`
- `Materials/UM244 使用手册V1.1.pdf`
- `Materials/42HSC1409-250NE2.pdf`
- `Materials/WF5805F 2Bar Datasheet V1.0.pdf`
- `Reference/WF5805_2BAR官方驱动包`
- `Reference/步进电机驱动示例`
- `Materials/STM32F103C8T6核心板原理图.pdf`
- `Materials/STM32F103x8B_DS_CH_V10.pdf`
- `Materials/STM32F10xxx参考手册（英文）.pdf`
- `Materials/STM32F103xx固件函数库用户手册.pdf`
- `Materials/ST-LINK+V2使用说明.pdf`
- `Materials/STM32F103C8T6引脚定义.xlsx`
- `Materials/最小系统板.png`
- `Materials/限位器接线.png`
- `Materials/npn型光耦隔离器-用于限位器信号输入.jpg`
- `Materials/npn型光耦隔离器-用于限位器信号输入（详细版）.jpg`
- `Materials/光耦隔离器原理图.jpg`
- `Materials/npn型光耦隔离器-用于给步进电机驱动器的拉低信号转换.jpg`
- `Materials/0.96寸4针B版本结构图.pdf`
- `Materials/0.96寸OLED规格书.pdf`
- `Materials/4-1 OLED显示屏.jpg`
- `Materials/中景园电子0.96OLED显示屏IIC接口原理图.pdf.pdf`
- `Materials/中景园电子0.96OLED显示屏_驱动芯片手册.pdf`
- `Materials/3-有源蜂鸣器/有源蜂鸣器模块原理图.png`
- `Materials/3-有源蜂鸣器/有源蜂鸣器模块实物图.png`

`skill.md` is the required coding-behavior guide for this project. Read it before code work and apply its rules on assumptions, simplicity, surgical changes, and verification.

`Reference/` code is reference material only. Before generating firmware, inspect the relevant `hardware` and `main` files for Standard Peripheral Library usage, WF5805F command/data format, OLED routines, GPIO setup, key/buzzer examples, and stepper timing patterns. Do not copy blocking `Delay` loops, busy-wait key scans, or whole example modules directly into this project.

The design document is authoritative for wiring, pin allocation, nap-mode motion, power recovery behavior, alarm behavior, and safety behavior.

Before generating code, summarize the hardware assumptions and planned modules first. Do not start implementation until the summary matches the spec.

## Protected Files And Directories

Do not modify:

- `Objects/`
- `Listings/`
- `DebugConfig/`
- `Library/`
- `start/`
- `system/`
- `*.uvguix`

Prefer adding new code under:

- `Hardware/`
- `user/main.c`

Do not modify existing `Hardware/*.c` files unless needed to integrate the new modules or fix a concrete bug.

## Coding Rules

- Use STM32 Standard Peripheral Library, not HAL.
- Keep code compatible with Keil C compiler.
- The Keil project has C99 enabled. C99 syntax is allowed.
- Prefer conservative Keil-compatible C style for maintainability.
- Avoid advanced C99 features unless they clearly simplify the code and compile successfully in Keil.
- Do not use variable-length arrays or compound literals.
- Avoid dynamic memory allocation.
- Keep interrupt handlers short; do not put complex control logic inside interrupts.
- Use clear module boundaries: one `.c/.h` pair per module.
- Put hardware pin macros and constants in one board configuration header.
- Preserve SWD pins `PA13/PA14`.
- If any pin assignment conflicts with the design document, stop and ask before changing it.

## Expected Modules

Implement code as focused modules, for example:

- software I2C for configurable pins
- WF5805F pressure sensor driver
- water depth calculation
- UM244 stepper pulse control
- nap-mode motion scheduler
- limit switch safety input
- active buzzer alarm output
- OLED display pages
- key scanning/menu handling
- Flash parameter storage
- main application state machine

## Safety Requirements

Code must fail safe:

- stop motion on sensor read failure
- stop motion on I2C repeated failure
- stop upward motion on any upper limit trigger
- stop downward motion on any lower limit trigger
- stop all motion if left/right same-direction limits disagree
- never chase missed movement after power loss
- do not release the motor by default in automatic mode
- buzzer alarm silence must not clear the fault state

## Flash Persistence

Do not erase/write the same Flash page too frequently.

Persist configuration and recovery state with throttling or wear leveling. Avoid writing Flash on every single 1-pulse or 8-pulse nap movement unless a wear-leveling strategy is implemented.

## Verification

After code changes:

- ensure the Keil project file includes any new source files
- run the available build method if possible
- if build cannot be run locally, state that clearly
- do not claim firmware is verified unless a build completed successfully
