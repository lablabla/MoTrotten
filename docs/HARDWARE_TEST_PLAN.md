# MoTrotten Hardware Test Plan

**Purpose:** Verify each physical component in isolation before running the full application firmware.
**Note:** UI board (display) is not connected during these tests.

---

## Setup

### Tools needed
- USB-C cable (programming/serial)
- Serial terminal (idf.py monitor or any 115200-baud terminal)
- Multimeter (voltage/continuity)
- Oscilloscope (optional but useful for Tests 1–2)
- 24V PSU connected to motor board

### Flash test firmware

In `main/CMakeLists.txt`, swap `app_main.cpp` for `hw_test.cpp`:

```cmake
# SRCS "app_main.cpp"    ← comment out
SRCS "hw_test.cpp"       ← enable
```

Rebuild and flash:
```powershell
& $idf $idfpy -p COM<N> flash monitor
```

After boot you will see the test menu. Type a number key to run each test.

---

## Test 1 — EN GPIO Toggle

**Component:** BTS7960 enable lines
**GPIOs:** `EN_R` = GPIO17, `EN_L` = GPIO18
**Key:** `1`

### Procedure
1. Probe GPIO17 and GPIO18 with a multimeter (or scope).
2. Press `1` in the terminal.
3. Watch 3 alternating pulses, 500ms each.

### Expected output
```
[0] EN_R=HIGH  EN_L=LOW
[0] EN_R=LOW   EN_L=HIGH
...
```

### Pass criteria
- GPIO17 measures 3.3V when HIGH, 0V when LOW.
- GPIO18 is always the inverse of GPIO17 during the test.
- Both settle at 0V when test ends.

---

## Test 2 — PWM Output

**Component:** LEDC PWM channels (BTS7960 inputs)
**GPIOs:** `PWM_R` = GPIO15 (UP), `PWM_L` = GPIO16 (DOWN)
**Key:** `2`

### Procedure
1. Probe GPIO15, then GPIO16 with an oscilloscope.
2. Press `2`.

### Expected output
Each channel sweeps 0 → 100% → 0% duty cycle:
- Frequency: **5 kHz**
- Resolution: 10-bit (0–1023 counts)
- No motor movement occurs (EN pins stay LOW).

### Pass criteria
- Both channels show 5 kHz square wave.
- Duty cycle visibly ramps up and back down.
- No noise/glitching between channels.

---

## Test 3 — Motor Run UP

**Component:** Motor, BTS7960 H-bridge, wiring
**Key:** `3`

### Safety
- Desk must have clearance above the tabletop.
- Stand clear of the desk during movement.
- Press ENTER in the terminal to confirm before movement starts.

### Procedure
1. Ensure 24V PSU is connected.
2. Press `3`, read the warning, press ENTER.
3. Desk ramps up over 500ms, runs at full speed for 2s, ramps back down.

### Expected output
```
Full speed. Running 2s...
PASS if desk moved up smoothly without noise or stall.
```

### Pass criteria
- Desk moves upward.
- No excessive noise, no binding, no overcurrent trip.
- Motor stops cleanly after ramp-down.

---

## Test 4 — Motor Run DOWN

**Component:** Motor, BTS7960 H-bridge
**Key:** `4`

Same as Test 3, desk moves **downward**.

### Pass criteria
- Desk moves downward.
- Clean start and stop, no binding.

---

## Test 5 — Current Sense ADC

**Component:** BTS7960 IS pins, 2.2kΩ drain resistors, ADC1
**GPIOs:** `IS_R` = GPIO1 (ADC1_CH0), `IS_L` = GPIO2 (ADC1_CH1)
**Key:** `5`

### Background
BTS7960 IS current ratio kILIS = 8500. With 2.2kΩ:

| Condition     | I_load | V_IS   | ADC raw |
|---------------|--------|--------|---------|
| Motor off     | 0 A    | 0 V    | ~0      |
| Light running | 1 A    | 0.26 V | ~321    |
| Normal load   | 2 A    | 0.52 V | ~643    |
| Heavy load    | 3 A    | 0.78 V | ~964    |
| Near stall    | 4 A    | 1.04 V | ~1285   |
| Stall (4.4A)  | 4.4 A  | 1.14 V | ~1413   |
| **Threshold** |        |        | **1300**|

### Procedure
**Part A — At rest:**
1. Motor off. Press `5`.
2. Observe 15 readings.

**Part B — During movement:**
1. Open two terminal windows (or take note of readings).
2. Run motor UP (test `3`), immediately press `5` in a second session — or run test 5 first, then test 3 while watching IS output.
   *(Alternatively: modify `hw_test.cpp` to run motor + IS simultaneously for tuning.)*

### Expected output (at rest)
```
[0] IS_R: raw=   3 0.002V 0.01A | IS_L: raw=   2 0.002V 0.01A
```

### Pass criteria
- At rest: all readings < 50 raw (< 0.04V, < 0.17A) — confirms no ADC offset issue and no leakage.
- During motor run: IS_R (UP) rises to 300–900 raw (normal moving load). IS_L near 0 while moving UP.
- Stall threshold (1300 raw) is safely above normal running readings. If not, adjust `MOTOR_STALL_THRESHOLD_RAW` in `desk_config.h`.

> **Tuning note:** Log the peak raw value during a full-speed, loaded run. Set the threshold ~30% above that peak. Typical desk load at 24V: 1–2A (raw 320–640). Threshold of 1300 raw ≈ 4A gives a large safety margin.

---

## Test 6 — Button ADC Ladder

**Component:** 4-button resistor ladder, ADC1_CH4
**GPIO:** `BTN` = GPIO5
**Key:** `6`

### Circuit (10kΩ pull-up, 3.3V)

| Button  | Series R | Expected V  | Expected raw |
|---------|----------|-------------|--------------|
| UP      | 0 Ω      | 0 V         | < 100        |
| DOWN    | 1 kΩ     | 0.28 V      | 250–500      |
| PRESET1 | 2.2 kΩ   | 0.54 V      | 600–900      |
| PRESET2 | 4.7 kΩ   | 0.95 V      | 1150–1450    |
| None    | ∞        | 3.3 V       | > 1600       |

### Procedure
1. Press `6` (30 readings, 300ms apart).
2. During the readings, press and hold each button for several cycles in order: UP → DOWN → PRESET1 → PRESET2 → release.

### Expected output
```
[ 0] raw=4095 => NONE
[ 3] raw=  18 => UP
[ 8] raw= 360 => DOWN
[14] raw= 720 => PRESET1
[20] raw=1230 => PRESET2
```

### Pass criteria
- Each physical button maps to its label with raw values inside the specified windows.
- No button reads as a neighbour.
- Released state consistently reads > 1600 (NONE).

---

## Test 7 — VL53L0X I2C

**Component:** VL53L0X time-of-flight sensor
**GPIOs:** `SDA` = GPIO8, `SCL` = GPIO9
**I2C address:** 0x29
**Key:** `7`

### Prerequisites
- VL53L0X powered (2.8V or 3.3V — check your module).
- External pull-ups on SDA/SCL (typically 4.7kΩ to 3.3V, or on the sensor module itself).

### Procedure
1. Press `7`.
2. The test scans the full I2C address space, then reads the model ID register.

### Expected output
```
Scanning I2C bus (0x01–0x7E)...
Found: 0x29  <-- VL53L0X
Model ID (reg 0xC0) = 0xEE  (expect 0xEE) OK
PASS if device found at 0x29 and model ID = 0xEE.
```

### Pass criteria
- Device found at 0x29.
- Model ID register returns 0xEE.

### Fail diagnosis
| Symptom | Likely cause |
|---------|-------------|
| No devices found | Power missing, wrong GPIO, missing pull-ups |
| Wrong address | Different sensor variant |
| Model ID mismatch | Not a VL53L0X; check part |
| Bus init failed | GPIO conflict, wrong pin numbers |

---

## Test 8 — Limit Switch

**Component:** Mechanical limit switch
**GPIO:** GPIO4, active-low, external 10kΩ pull-up to 3.3V
**Key:** `8`

### Procedure
1. Press `8` (20 readings, 500ms apart).
2. At around reading 8, manually activate the limit switch. Release before reading 16.

### Expected output
```
[ 0] GPIO4 = 1  open
...
[ 8] GPIO4 = 0  <-- TRIGGERED
...
[16] GPIO4 = 1  open
```

### Pass criteria
- Open state: GPIO reads 1 (pulled high via external resistor).
- Activated state: GPIO reads 0 (switch pulls to GND).
- Transition is clean (no bouncing over multiple readings at 500ms apart).

---

## After all tests pass

1. Restore `app_main.cpp` in `CMakeLists.txt`.
2. Rebuild with the full application firmware.
3. Note the measured IS ADC baseline during movement and confirm `MOTOR_STALL_THRESHOLD_RAW` (currently 1300) has adequate margin. Adjust in `desk_config.h` if needed.

---

## Stall threshold tuning worksheet

Fill in after running Tests 3 and 5 together:

| Measurement                          | Value |
|--------------------------------------|-------|
| IS_R raw at rest                     |       |
| IS_R raw during light movement (no load) |  |
| IS_R raw during normal desk movement |       |
| IS_R raw when manually blocking desk |       |
| Chosen threshold (30% above max normal) |    |
