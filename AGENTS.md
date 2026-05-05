# AGENTS.md

This is a Keil5 STM32F103C8T6 Standard Peripheral Library project.

## Required Reading Before Code Generation

Before writing or modifying code, read and follow:

- `codex-codegen-execution.md`
- `requirements.md`
- `pinmap.md`
- `bringup-checklist.md`
- `docs/superpowers/specs/2026-05-04-aquarium-lift-wiring-design.md`
- `Materials/UM244 使用手册V1.1.pdf`
- `Materials/42HSC1409-250NE2.pdf`
- `Materials/WF5805F 2Bar Datasheet V1.0.pdf`
- `Materials/限位器接线.png`
- `Materials/4-1 OLED显示屏.jpg`
- `Materials/3-有源蜂鸣器/有源蜂鸣器模块原理图.png`

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
