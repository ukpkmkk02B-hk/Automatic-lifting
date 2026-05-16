---
name: embedded-project-grounding
description: Use when starting work in an embedded firmware repository where local rules, hardware specs, pin maps, wiring docs, bring-up checklists, reference drivers, or generated IDE files must be understood before code changes.
---

# Embedded Project Grounding

## Overview

Ground firmware work in repository truth before proposing or changing code. The output is a short implementation brief: hardware assumptions, module boundaries, protected files, risks, and verification route.

## 中文简述

用于嵌入式固件项目开工前的上下文摸底：先读规则、需求、引脚、接线和参考代码，再决定模块划分与实现阶段。

## Required Process

1. Read local agent instructions first: `AGENTS.md`, `CODEX.md`, `skill.md`, or equivalent.
2. Identify authoritative specs: requirements, pin map, wiring design, bring-up checklist, user manuals, datasheets, reference code, and IDE project files.
3. Inspect existing source layout before asking where code belongs.
4. Separate facts from preferences:
   - Discoverable facts: resolve by reading files.
   - Product or hardware decisions: ask only after exploration.
5. Summarize before implementation:
   - Power rails, grounding, signal isolation, active levels, bus addresses, units.
   - Planned modules and ownership boundaries.
   - Files likely to be added or changed.
   - Protected directories and generated files.
   - Conflicts, missing datasheet details, or unsafe assumptions.
   - Build and hardware verification plan.

## Embedded Grounding Checklist

| Area             | What to Confirm                                                       |
| ---------------- | --------------------------------------------------------------------- |
| Toolchain        | Keil, GCC, IAR, CMake, PlatformIO, vendor SDK, compiler dialect       |
| MCU and library  | Exact chip, HAL/SPL/LL/bare-metal choice, startup files               |
| Pin map          | Pins, alternate functions, active levels, reserved debug pins         |
| Electrical       | Voltage domains, isolation, pull-ups, common ground, signal inversion |
| External devices | Datasheet commands, bus address format, timing, unit conversion       |
| Safety           | Stop paths, fault latching, manual override limits, safe defaults     |
| Persistence      | Flash pages, records, CRC, write throttling, recovery policy          |
| Verification     | Build command, generated artifacts, bench bring-up sequence           |

## Output Pattern

Use this compact format:

```text
Grounding summary:
- Hardware assumptions:
- Existing project shape:
- Proposed module boundaries:
- Files to add/change:
- Files/directories not to touch:
- Conflicts or missing facts:
- Implementation phases:
- Verification route:
```

## Stop Conditions

Stop and ask before implementation when pin assignments conflict, datasheet command formats are unclear, voltage domains are unsafe, protected files must be edited, or no build path can be identified.

## Common Mistakes

- Treating reference projects as copy-paste sources instead of behavior examples.
- Asking the user for file locations that are discoverable by search.
- Starting driver code before address formats, units, active levels, and timeout policy are known.
- Claiming firmware is verified when only static inspection was done.

## Pressure Scenarios

- A user asks to "just add the driver" but the datasheet and reference code disagree on address format.
- A pin map conflicts with a design document or reuses SWD pins.
- The repo has generated IDE output directories mixed with source directories.
