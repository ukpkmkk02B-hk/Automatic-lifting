---
name: embedded-persistence-and-bringup
description: Use when adding Flash or EEPROM persistence, A/B records, 断电恢复, build verification, 构建验证, or hardware bring-up procedures for embedded firmware that controls real devices.
---

# Embedded Persistence And Bring-Up

## Overview

Persistence and bring-up are safety features. Store enough state to recover deliberately, throttle writes to protect memory, verify the build, and bring hardware up in an order that prevents damage.

## 中文简述

用于参数持久化、断电恢复和硬件上电调试：控制 Flash/EEPROM 写入频率，先构建验证，再按低风险顺序带硬件。

## Flash A/B Record Pattern

Use two reserved pages or sectors when the MCU allows it:

- Reserve storage outside the code region and confirm linker/IROM settings cannot overlap it.
- Each record includes `magic`, `version`, monotonically increasing `seq`, payload, and CRC.
- On boot, validate both pages and load the valid record with highest `seq`.
- If both records are invalid, load defaults and raise a warning.
- Write the inactive page, verify it, then let its higher `seq` make it current.
- Do not erase or write on high-frequency events such as each pulse, sample, loop tick, or display refresh.

## Write Policy

Save configuration immediately after explicit user confirmation. Save runtime state through the project-defined throttle interval plus forced saves before important transitions such as pause, fault, maintenance, motor release, or completed checkpoint events.

Document blocking behavior: Flash erase/program often stalls foreground code and may affect interrupt timing. Avoid starting Flash writes while motors or safety-critical timing are active unless the platform proves it safe.

## Power Recovery Rules

- Run self-test before considering automatic resume.
- Check sensors, limits, stored state validity, position trust, and freshness or plausibility of measured values.
- Do not chase missed movement after power loss unless explicitly required and risk-reviewed.
- If recovery evidence is weak, enter pause or fault and require human confirmation.

## Build And Delivery Verification

- Confirm every new source file is part of the build system or IDE project.
- Run the available build command and inspect the log.
- Report compiler, target, errors, warnings, and artifact path.
- If build cannot run, state why and do not claim verification.
- Keep generated outputs and IDE user-layout files out of intentional source edits.

## Bring-Up Order

1. Power rails and polarity, with actuators disabled.
2. MCU boot, display, keys, LEDs, buzzer, and debug link.
3. Sensors and communication buses.
4. Safety inputs and active levels.
5. Driver control signals without load.
6. Actuator direction and current at low risk.
7. Limit behavior, homing, and manual movement.
8. Dry run for the required duration before real use.

## Pressure Scenarios

- A user asks to save recovery state after every tiny movement.
- The build cannot run locally because Keil is missing.
- A reboot occurs after an automatic state, but position or sensor evidence is stale.
