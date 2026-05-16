---
name: stm32-spl-keil-module-development
description: Use when implementing or modifying STM32 firmware in a Keil uVision project using the STM32 Standard Peripheral Library, especially when adding modules, updating uvprojx, or preserving Keil C compatibility.
---

# STM32 SPL Keil Module Development

## Overview

Build STM32 firmware in small modules that match Keil uVision and Standard Peripheral Library conventions. Keep source changes surgical, integrate new `.c` files into the project, and verify with a real Keil build when possible.

## 中文简述

用于 Keil5 + STM32 标准外设库项目的模块化开发：按板级配置、`.c/.h` 边界、Keil 工程集成和构建验证推进。

## Module Rules

- Use STM32 Standard Peripheral Library APIs, not HAL, unless the project already uses HAL.
- Put board pins, active levels, default constants, and unit macros in one board config header.
- Prefer one focused `.c/.h` pair per hardware or service module.
- Keep public headers self-contained and documented: purpose, key APIs, parameters, return values, units, blocking behavior, and safety assumptions.
- Use conservative Keil-compatible C:
  - No dynamic allocation.
  - No variable-length arrays.
  - No compound literals unless the project explicitly accepts them.
  - Keep interrupt-shared state `volatile` and narrow.
- Match local naming and include style; do not refactor adjacent code unless required for the task.

## Keil Integration

Before editing `*.uvprojx`, inspect the project XML pattern for existing groups and source entries. Add only new source files required by the current module. Do not edit user layout files such as `*.uvguix`, generated outputs, startup files, vendor libraries, or object/listing directories unless the user explicitly asks.

After adding sources, confirm:

- Include paths already cover the new headers or are intentionally extended.
- Preprocessor symbols still match the MCU family, for example `STM32F10X_MD`.
- Flash/IROM settings still reserve any parameter pages required by the project.
- New modules compile under the project's selected ARMCC/Keil C settings.

## Build Verification

Use the project's existing build path first. If Keil command-line build is available, run it and report compiler, target, errors, warnings, and output artifact. If build cannot run locally, state the exact missing tool or permission. Never say firmware is verified without a successful build.

## Reference Code Policy

Reference projects may guide SPL initialization, register usage, timing constants, and device command formats. Do not copy whole modules, blocking delay loops, busy-wait key scans, or demo-only infinite loops into production firmware.

## Common Mistakes

- Adding a `.c` file but not adding it to `uvprojx`.
- Editing `uvoptx` or `uvguix` because Keil happened to rewrite local UI state.
- Mixing HAL and SPL in one project.
- Fixing compile errors by deleting safety checks or replacing errors with fake success values.

## Pressure Scenarios

- A new source compiles manually but is missing from the Keil project.
- A user asks to "make it compile" after a safety module fails due to missing dependencies.
- The project has C99 enabled, but the local style is conservative C89-like embedded C.
