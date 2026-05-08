# Hardware Bring-Up Checklist

Use this checklist after wiring the hardware and before putting fish into the basket. Do not skip directly to automatic mode.

## 0. Preparation

- Keep the motor driver disabled or motor wires disconnected for the first electrical checks.
- Keep the basket mechanically supported during early motor tests.
- Confirm 24V wiring cannot touch STM32, OLED, keys, or WF5805F signal wires.
- Keep a physical way to cut 24V power quickly.

## 1. Power Checks

- Measure 24V supply output before connecting UM244.
- Adjust and measure the 24V-to-5V buck output before connecting it to the minimum system board or UM244 signal terminals.
- Connect the 5V buck output to the minimum system board `5V` pin, not to the `3.3V` pin.
- Confirm the same 5V buck output feeds UM244 `PU+`, `DR+`, and `MF+`.
- After applying 5V to the minimum system board, measure the 3.3V rail before powering OLED, buzzer, and WF5805F boards.
- Confirm STM32 GND, 5V buck GND, 24V supply negative, and UM244 signal reference GND are connected as designed.
- Confirm no OLED, WF5805F, buzzer, or STM32 GPIO pin is connected directly to 5V.
- Confirm 24V limit switch signals enter STM32 only through optocoupler isolation.

Pass condition: all supply voltages are correct; only the minimum system board `5V` power pin receives 5V, and no STM32 GPIO or 3.3V peripheral pin sees 5V or 24V directly.

## 2. STM32, OLED, Keys, Buzzer

- Flash a minimal firmware or the project firmware with motor output disabled.
- Confirm OLED shows the self-test page.
- Confirm buzzer short-beeps once at startup, then stays off.
- Press `PB1`, `PB11`, `PB10`, and `PB0`; confirm OLED shows the expected key events.
- Confirm `PA0` high turns buzzer off and `PA0` low turns buzzer on.

Pass condition: user interface works without motor power.

## 3. WF5805F Sensors

- Confirm all three sensors respond on I2C.
- Confirm OLED is alone on OLED-I2C `PB8/PB9`.
- Confirm I2C-A `PA6/PA7` has `P_air`.
- Confirm I2C-B `PB6/PB7` has `P_basket`.
- Confirm I2C-C `PA8/PA9` has `P_tank`.
- Confirm no I2C bus has more than one WF5805F module, and OLED does not share a bus with any WF5805F module.
- Keep `P_air` in air and confirm it is stable.
- Put `P_tank` and `P_basket` into water and confirm calculated depth increases with immersion depth.
- Confirm `tank_depth_mm` and `basket_depth_mm` are plausible and stable after the 10 second startup wait.

Pass condition: no repeated I2C failures and depth direction is correct.

## 4. Limit Switches

- With 24V limit switch power on, manually trigger each limit switch one at a time.
- Confirm the isolation module is a 24V NPN input optocoupler module: sensor brown/blue/black stay on the 24V input side, and STM32 GPIO sees only the 3.3V output side.
- The module shown in `Materials/npn型光耦隔离器-用于限位器信号输入.jpg` can be used here only as the 24V-input variant shown in `Materials/npn型光耦隔离器-用于限位器信号输入（详细版）.jpg`.
- Confirm the four limit switches use four independent optocoupler input channels and four independent STM32 GPIO outputs.
- Confirm the module output side `VCC` is connected to 3.3V, not 5V or 24V.
- Before connecting STM32, power the module output side from 3.3V and verify `OUT` is never above 3.3V.
- Confirm OLED shows:
  - left upper
  - left lower
  - right upper
  - right lower
- Confirm untriggered state reads high and triggered state reads low.
- Trigger left/right same-direction limits inconsistently and confirm firmware raises mismatch fault.

Pass condition: every limit input matches the physical switch and mismatch detection works.

## 5. UM244 Control Signals Without Motor Movement

- Keep motors disconnected or driver disabled.
- Confirm `STEP`, `DIR`, and `MF` outputs reach the level-shift circuit.
- Confirm STEP/DIR/MF are wired from `PA3/PA4/PA5` respectively.
- Confirm UM244 `PU+`, `DR+`, and `MF+` are tied to +5V.
- Confirm STM32 GPIO is not directly connected to UM244 input plus terminals.
- Confirm three independent single-channel optocoupler modules are used for STEP, DIR, and MF.
- Confirm each module MCU-side `VCC` is 3.3V and each module output-side signal power is +5V.
- Confirm each module output-side `OUT` goes to the matching UM244 minus terminal: STEP to `PU-`, DIR to `DR-`, MF to `MF-`.
- Confirm STM32 low level pulls the matching UM244 minus terminal low, and STM32 high level releases it high.
- With UM244 connected, measure each active-low minus terminal and confirm the low level is `0-0.5V`.
- Confirm `MF` default state does not release the motor.

Pass condition: control signals are electrically correct before motor power tests.

## 6. Motor Direction And Current

- Set UM244 current to the recommended starting value, 2.5A.
- Set UM244 microstep to 1600 pulse/rev.
- Confirm automatic nap STEP frequency is 800Hz by default, or 400Hz if configured as the fallback.
- Test one short manual movement at low speed.
- Confirm both motors move in the same direction.
- If one motor direction is reversed, swap the two wires inside one winding on that motor only, for example red/yellow or green/blue.
- Confirm software "up" makes the basket move up and basket water depth become shallower.
- Confirm software "down" makes the basket move down and basket water depth become deeper.

Pass condition: both motors move synchronously and direction matches firmware labels.

## 7. Mechanical Limit Verification

- Install the basket without fish.
- Enter maintenance mode.
- Long-press manual up and confirm upper limits stop upward movement.
- Long-press manual down and confirm lower limits stop downward movement.
- Confirm releasing the key immediately stops manual movement.
- Confirm left/right mismatch stops all movement.

Pass condition: no movement command can bypass limit protection.

## 8. Homing

- Enter maintenance mode.
- Run the mechanical homing procedure.
- Confirm lower limit is detected.
- Confirm the basket backs off and re-approaches the lower limit.
- Confirm position becomes `basket_position_mm = 0`.
- Confirm position trusted flag is set.

Pass condition: firmware has a trusted mechanical zero.

## 9. Dry Run Without Fish

- Fill the aquarium and basket to a safe test water level.
- Run automatic mode without fish for at least 24 hours.
- Confirm nap movement occurs at the expected interval.
- Confirm motor and UM244 temperature remain reasonable.
- Confirm I2C does not show repeated failures.
- Confirm `basket_depth_mm` trend matches planned movement.
- Confirm Flash recovery works after a controlled power cycle.

Pass condition: system can run automatically without faults for 24 hours.

## 10. Before Using With Fish

- Confirm final target depth and daily shallowing rate are correct.
- Confirm low water alarm triggers if water is deliberately lowered below threshold.
- Confirm buzzer silence does not clear the fault.
- Confirm maintenance mode cannot ignore limit faults.
- Confirm automatic mode resumes safely after a normal power cycle.

Pass condition: automatic operation, safety alarms, and recovery behavior match the specification.
