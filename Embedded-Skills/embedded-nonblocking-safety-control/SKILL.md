---
name: embedded-nonblocking-safety-control
description: Use when designing embedded control loops, state machines, alarms, 打盹 motion, sensor supervision, 限位 safety, 蜂鸣器静音, 断电恢复, or fault handling where blocking waits or unsafe recovery create hardware risk.
---

# Embedded Nonblocking Safety Control

## Overview

Safety behavior must not depend on slow UI, blocking drivers, or optimistic state machines. Separate emergency paths, driver services, and application policy so the system can stop first and explain later.

## Three-Layer Pattern

| Layer | Owns | Must Not Do |
| --- | --- | --- |
| Safety bottom layer | Raw emergency GPIO reads, timer stop, fault flag set | Wait for display, bus I/O, Flash, menus |
| Driver/service layer | GPIO, debounced inputs, bounded I/O, finite motor commands | Hide errors, wait forever, decide product policy |
| Application state machine | Self-test, pause, auto, manual, maintenance, fault recovery | Bypass driver safety checks or direct-register around services |

## Nonblocking Rules

- Do not use `Delay_ms`, `Delay_us`, sleep loops, or long `while` waits for business timing.
- Schedule waits by timestamp difference, for example `if ((uint32_t)(now - due) < limit)`.
- Every peripheral wait loop must have a timeout and return a status.
- UI refresh, sensor stabilization, motion intervals, alarm cadence, and persistence throttling must advance from the main loop or a scheduler.
- Interrupt handlers may count, set flags, read raw emergency pins, and stop hardware. Defer classification, display, logging, and recovery to the main loop.

## Fault And Recovery Policy

- Severe faults are latched until the root cause is gone and a controlled recovery path clears them.
- Alarm silence only changes sound output; it does not clear the fault.
- Limit protection and emergency stop rules remain active in manual and maintenance modes.
- Recovery from power loss must not chase missed motion unless the specification explicitly requires it and proves it safe.
- Default output states should be safe: no unintended motion, motor hold/release behavior explicit, and alarms not spuriously active.

## Motion Safety Checklist

- Each motion state issues a finite work unit and returns to wait/idle.
- Each command checks safety preconditions before start.
- Fast paths read raw directional limits before effective motion edges.
- Stop reasons are visible to the application state machine.
- Manual and maintenance controls cannot bypass limits after an alarm.

## Common Mistakes

- Relying on debounced limit state for emergency stop instead of raw GPIO in the fast path.
- Allowing maintenance movement to ignore limits.
- Clearing a fault when muting a buzzer.
- Writing self-test, sensor stabilization, or nap scheduling as blocking waits.

## Pressure Scenarios

- A timer ISR needs to stop motion on a raw limit while the display bus is busy.
- A sensor read repeatedly fails during automatic movement.
- A user wants manual movement after an alarm, but limit protection still applies.
