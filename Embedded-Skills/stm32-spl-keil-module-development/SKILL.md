---
name: stm32-spl-keil-module-development
description: Use when implementing or modifying STM32 firmware in a Keil5 uVision project using STM32 Standard Peripheral Library, 标准外设库, uvprojx integration, Keil C compatibility, Chinese comments, or 构建验证.
---

# STM32 SPL Keil Module Development

## Overview

Build STM32 firmware in small modules that match Keil uVision and Standard Peripheral Library conventions. Keep edits surgical, integrate new `.c` files intentionally, and verify with a real Keil build when possible.

## Module Rules

- Use STM32 Standard Peripheral Library APIs, not HAL, unless the project already uses HAL.
- Put board pins, active levels, default constants, and unit macros in one board config header.
- Prefer one focused `.c/.h` pair per hardware or service module.
- Keep public headers self-contained and documented: purpose, APIs, parameters, return values, units, blocking behavior, and safety assumptions.
- Follow the repository's required comment language and style. If local rules require Simplified Chinese comments, document public interfaces, state transitions, safety logic, units, and timing in Chinese.
- Use conservative Keil-compatible C:
  - No dynamic allocation.
  - No variable-length arrays.
  - No compound literals unless local rules explicitly allow them.
  - Keep interrupt-shared state `volatile` and narrow.
- Match local naming and include style; do not refactor adjacent code unless required for the task.

## Keil Integration

Before editing `*.uvprojx`, inspect the existing XML group and source-entry pattern. Add only new source files required by the current module. Do not touch `*.uvguix`, generated outputs, startup files, system files, vendor libraries, object directories, or listing directories unless the user explicitly asks and the local rules allow it.

After adding sources, confirm:

- Include paths already cover the new headers or are intentionally extended.
- Preprocessor symbols still match the MCU family, for example `STM32F10X_MD`.
- Flash/IROM settings still reserve any parameter pages required by the project.
- New modules compile under the selected ARMCC/Keil C settings.

## Build Verification

Use the project's existing build path first. If Keil command-line build is available, run it and report compiler, target, errors, warnings, and output artifact. If build cannot run locally, state the exact missing tool, path, license, or permission. Never say firmware is verified without a successful build.

## Reference Code Policy

Reference projects may guide SPL initialization, register use, timer setup, GPIO patterns, timing constants, and device command formats. Do not copy whole modules, blocking delay loops, busy-wait key scans, or demo-only infinite loops into production firmware.

## Common Mistakes

- Adding a `.c` file but not adding it to `uvprojx`.
- Editing `uvoptx` or `uvguix` because Keil rewrote local UI state.
- Mixing HAL and SPL in one project.
- Deleting safety checks or returning fake success values to silence compiler errors.

## Pressure Scenarios

- A new source compiles manually but is missing from the Keil project.
- A user asks to "make it compile" after a safety module fails due to missing dependencies.
- The project has C99 enabled, but local style remains conservative embedded C.
