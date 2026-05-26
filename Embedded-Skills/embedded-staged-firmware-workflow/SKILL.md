---
name: embedded-staged-firmware-workflow
description: Use when embedded firmware work requires read-first grounding, no-code summaries, user confirmation gates, staged implementation, phase reports, Keil5, 标准外设库, 总结后等待确认, or 每阶段停下等待.
---

# Embedded Staged Firmware Workflow

## Overview

Use a phase-gated workflow for embedded firmware that controls real hardware. Read and summarize first, implement only after confirmation, and stop after each phase with verification evidence and unresolved risks.

## 中文简述

用于“先阅读资料、总结、等待确认，再分阶段生成固件代码”的嵌入式工作流，尤其适合 Keil5、STM32 标准外设库、安全运动控制和硬件上电调试任务。

## Required Sub-Skills

- **REQUIRED SUB-SKILL:** Use `embedded-project-grounding` for the initial read-only summary.
- Use `stm32-spl-keil-module-development` when adding STM32/Keil modules or editing project files.
- Use `embedded-driver-hardening` for buses, sensors, optocouplers, limit inputs, timers, and motor drivers.
- Use `embedded-nonblocking-safety-control` for state machines, alarms, motion, faults, and recovery policy.
- Use `embedded-persistence-and-bringup` for Flash/EEPROM, power recovery, build verification, and bench bring-up.

## Phase 0 Gate

If the user asks to read first, says "请先不要写代码", or asks for a summary before implementation:

1. Read local rules and authoritative specs.
2. Inspect existing source and project layout.
3. Summarize hardware assumptions, module plan, files to add/change, protected files, conflicts, phases, and verification route.
4. Stop. Do not edit files until the user confirms.

## Implementation Phase Rules

For each phase after confirmation:

1. Restate the phase goal, input documents, source files to add/change, protected files, and verification command.
2. Implement only that phase. Do not prebuild large future-phase logic.
3. Update build/IDE project files only when needed to include new sources or required memory layout.
4. Build if possible. If not possible, report the exact missing tool, path, license, or permission.
5. Report changed files, main interfaces, pin/spec mapping, verification result, hardware items not yet tested, and next-phase risks.
6. Stop and wait for user confirmation before the next phase.

## Phase Report Pattern

```text
Phase N report:
- Goal:
- Files added/changed:
- Main interfaces:
- Spec/pin mapping:
- Protected files checked:
- Build result:
- Hardware not verified:
- Risks before next phase:
- Waiting for confirmation:
```

## Red Flags

- Writing code during a read-only summary phase.
- Continuing into the next phase without user confirmation.
- Fixing build errors by deleting safety behavior.
- Claiming firmware is verified without a successful build.
- Treating reference code as production code.

## Pressure Scenarios

- The user pastes a long "confirm, generate all code" request after an earlier no-code summary requirement.
- A phase needs a driver detail that the datasheet or reference code does not prove.
- A project file update is needed, but generated user/session files also changed.
