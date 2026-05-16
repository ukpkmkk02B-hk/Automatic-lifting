---
name: embedded-nonblocking-safety-control
description: Use when designing embedded control loops, state machines, alarms, motion control, sensor supervision, or fault handling where blocking waits, long interrupts, or unsafe recovery could create hardware risk.
---

# Embedded Nonblocking Safety Control

## Overview

Safety behavior must not depend on slow UI, blocking drivers, or optimistic state machines. Separate emergency paths, driver services, and application policy so the system can stop first and explain later.

## 中文简述

用于安全相关控制逻辑：把急停、驱动服务和应用状态机分层，避免阻塞等待、长中断和不受控恢复。

## Three-Layer Pattern

| Layer                     | Owns                                                        | Must Not Do                                                    |
| ------------------------- | ----------------------------------------------------------- | -------------------------------------------------------------- |
| Safety bottom layer       | Raw emergency GPIO reads, timer stop, fault flag set        | Wait for display, bus I/O, Flash, menus                        |
| Driver/service layer      | GPIO, debounced inputs, bounded I/O, finite motor commands  | Hide errors, wait forever, decide product policy               |
| Application state machine | Self-test, pause, auto, manual, maintenance, fault recovery | Bypass driver safety checks or direct-register around services |

## Nonblocking Rules

- Do not use `Delay_ms`, `Delay_us`, or sleep loops for business waits.
- Schedule waits by timestamp difference, for example `if ((now - due) >= 0)`.
- Every peripheral wait loop must have a timeout and return a status.
- UI refresh, sensor stabilization, motion intervals, alarm cadence, and Flash throttling must advance from the main loop or a scheduler.
- Interrupt handlers may count, set flags, read raw emergency pins, and stop hardware. Defer classification, display, logging, and recovery to the main loop.

## Fault Policy

- Severe faults are latched until the root cause is gone and a controlled recovery path clears them.
- Alarm silence only changes sound output; it does not clear the fault.
- Limit protection and emergency stop rules remain active in manual and maintenance modes.
- Recovery from power loss must not "catch up" missed motion unless the specification explicitly requires it and proves it safe.
- Default output states should be safe: motors held or disabled as appropriate, actuators not moving, alarms not spuriously active.

## State Machine Review

For each state, confirm allowed commands, exit conditions, safety preconditions, and what happens if a severe fault appears mid-state. Motion states must issue finite work units and return to a wait/idle state.

## Common Mistakes

- Relying on debounced limit state for emergency stop instead of raw GPIO in the fast path.
- Allowing manual mode to bypass limits after an alarm.
- Clearing a fault when muting a buzzer.
- Writing a long self-test or sensor wait as a blocking `while`.

## Pressure Scenarios

- A timer ISR needs to stop motion on a raw limit while the display bus is busy.
- A sensor read repeatedly fails during automatic movement.
- A user wants maintenance movement after an alarm, but limit protection still applies.
