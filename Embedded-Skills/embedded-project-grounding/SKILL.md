---
name: embedded-project-grounding
description: Use when starting embedded firmware work that depends on local rules, hardware specs, pin maps, wiring docs, datasheets, reference drivers, IDE files, or Chinese prompts such as 先阅读资料, 先不要写代码, 总结后等待确认.
---

# Embedded Project Grounding

## Overview

Ground firmware work in repository truth before proposing or changing code. Produce a short implementation brief that separates confirmed facts, unresolved risks, protected files, module boundaries, and verification route.

## Hard Gate

If the user says not to write code, asks for a read-first summary, or requires confirmation before implementation, do only the grounding work and stop after the summary. Do not edit files, generate code, update project files, or continue into implementation until the user confirms.

## Required Process

1. Read local agent instructions first: `AGENTS.md`, `CODEX.md`, `skill.md`, or equivalents.
2. Identify authoritative specs: requirements, pin map, wiring design, bring-up checklist, user manuals, datasheets, reference code, and IDE project files.
3. Inspect existing source layout before asking where code belongs.
4. Resolve discoverable facts by reading files; ask only for product or hardware decisions that remain ambiguous.
5. Summarize before implementation:
   - Power rails, grounding, isolation, active levels, bus addresses, timing, and units.
   - Module boundaries, files to add/change, protected files, conflicts, and verification route.

## Grounding Checklist

| Area         | Confirm                                                               |
| ------------ | --------------------------------------------------------------------- |
| Toolchain    | Keil5, GCC, IAR, CMake, PlatformIO, vendor SDK, compiler dialect      |
| MCU/library  | Exact chip, SPL/HAL/LL/bare-metal choice, startup and system files    |
| Pin map      | Pins, alternate functions, active levels, reserved debug pins         |
| Electrical   | Voltage domains, isolation, pull-ups, common ground, signal inversion |
| Devices      | Datasheet commands, bus address format, timing, unit conversion       |
| Safety       | Stop paths, fault latch, manual override limits, safe defaults        |
| Verification | Build command, generated artifacts, bench bring-up sequence           |

## Output Pattern

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
- Asking for file locations that are discoverable by search.
- Starting driver code before address formats, units, active levels, and timeout policy are known.
- Claiming firmware is verified when only static inspection was done.

## Pressure Scenarios

- A user asks to "just add the driver" while also saying "先不要写代码".
- A datasheet and reference driver disagree on 7-bit versus 8-bit addresses.
- A pin map conflicts with a wiring design or reuses SWD/debug pins.
