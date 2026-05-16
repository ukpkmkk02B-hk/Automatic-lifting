---
name: embedded-driver-hardening
description: Use when writing embedded peripheral drivers for unreliable buses, sensors, optocouplers, limit inputs, timers, stepper drivers, or devices with ambiguous datasheet/reference-code behavior.
---

# Embedded Driver Hardening

## Overview

Drivers should expose bounded operations, explicit units, and status codes. They must turn electrical and protocol uncertainty into visible errors instead of blocking the system or fabricating values.

## 中文简述

用于外设驱动加固：把总线异常、时序限制、有效电平、单位和失败计数做成显式接口，禁止死等和伪造有效数据。

## Datasheet First

Before coding, confirm command sequence, register addresses, return byte layout, status bits, timing, electrical levels, and whether addresses are 7-bit or 8-bit. If the datasheet and reference code disagree, stop and summarize the conflict.

## Bus Driver Rules

- Use explicit status returns for parameter errors, timeout, NACK, bus busy, and device status errors.
- Every wait for SCL/SDA/MISO/ready/status bits must have a timeout.
- For software I2C, implement bus-idle checks, START/STOP, ACK/NACK handling, and 9-clock recovery for SDA stuck low.
- Store bus recovery failure counts separately from sensor read failure counts when the application needs different alarms.
- Use internal units in names: `hpa_x100`, `mm_x10`, `pulse`, `Hz`, `ms`.
- Do not return stale readings as valid after a failed transaction.

## GPIO and Isolation Rules

- Put active-high/active-low and inversion caused by optocouplers into board macros.
- Document which voltage domain owns each side of an isolation circuit.
- Treat raw emergency inputs separately from debounced user-facing state.
- Do not assume a level-shifter or optocoupler works for a driver input until bring-up measurements confirm the required voltage range.

## Timer and Stepper Rules

- Generate STEP or similar pulses with a timer or equivalent bounded timing mechanism, not delay-loop bit banging.
- Every motion command is finite: count pulses, stop automatically, and expose busy/stop reason.
- Check directional limits before starting and in the fast path before effective pulse edges.
- Respect direction setup and hold times; encode them as named `ms` constants.
- Keep motor release/enable behavior explicit and safe by default.

## API Shape

Prefer:

```c
DriverStatus_t Device_Read(DeviceId_t id, DeviceReading_t *out);
uint16_t Device_GetFailureCount(DeviceId_t id);
DriverStatus_t Device_RecoverBus(DeviceId_t id);
```

Avoid APIs that block indefinitely, hide failure, or make callers inspect global state to know whether output data is valid.

## Pressure Scenarios

- A sensor has fixed address and three copies are needed in one system.
- A reference driver uses blocking waits without timeout.
- A stepper driver must stop within an interrupt when a direction limit becomes active.
