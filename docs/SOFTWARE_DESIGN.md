# MoTrotten — Software Design Document

**Project:** Motorized IKEA Trotten Standing Desk Controller
**MCU:** ESP32-S3 (N16R8)
**Framework:** ESP-IDF (native C/C++), FreeRTOS
**Date:** 2026-03-13
**Status:** Design — Pre-implementation (Rev 2 — design flaws corrected)

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [File Structure](#2-file-structure)
3. [Build System](#3-build-system)
4. [Hardware Reference](#4-hardware-reference)
5. [Module: desk_config.h](#5-module-desk_configh)
6. [Module: motor_driver](#6-module-motor_driver)
7. [Module: button_reader](#7-module-button_reader)
8. [Module: height_sensor (VL53L0X)](#8-module-height_sensor-vl53l0x)
9. [Module: nvs_manager](#9-module-nvs_manager)
10. [Module: height_controller](#10-module-height_controller)
11. [Module: display_manager](#11-module-display_manager)
12. [Module: ui_manager](#12-module-ui_manager)
13. [Application: app_main](#13-application-app_main)
14. [FreeRTOS Design](#14-freertos-design)
15. [State Machine](#15-state-machine)
16. [Safety Architecture](#16-safety-architecture)
17. [Open Questions](#17-open-questions)
18. [Phase 1 Pre-work: VL53L0X ESP-IDF Rewrite](#18-phase-1-pre-work-vl53l0x-esp-idf-rewrite)

---

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                      app_main.cpp                       │
│  - Creates all tasks                                    │
│  - Owns desk_state (global state machine)               │
│  - Passes module handles to tasks via task args         │
└──────────────┬───────────────────┬─────────────────────┘
               │                   │
    ┌──────────▼──────┐   ┌────────▼────────┐
    │   app_task      │   │   lvgl_task      │
    │  (Core 0)       │   │  (Core 1)        │
    │                 │   │                  │
    │  button_reader  │   │  display_manager │
    │  height_sensor  │   │  ui_manager      │
    │  nvs_manager    │   │                  │
    │  height_ctrl    │   └──────────────────┘
    │  motor_driver   │
    └──────────┬──────┘
               │ (spawned by motor_driver.init())
    ┌──────────▼──────┐
    │  motor_mon_task │
    │  (Core 0)       │
    │  soft start/stop│
    │  current sense  │
    │  stall detect   │
    └─────────────────┘
```

### Design Principles

- **No global mutable state** beyond the single `DeskState` struct, accessed under a mutex.
- **Modules are pure C++ classes** with explicit `init()` and no hidden dependencies.
- **All inter-task communication** uses FreeRTOS queues or event groups — no direct task-to-task calls.
- **Motor is always stopped** if any safety condition triggers, regardless of state.
- **LVGL runs exclusively on Core 1** in its own task. All UI updates from Core 0 use `lv_async_call()`.
- **Motor commands are non-blocking.** `move_up()`, `move_down()`, and `stop()` return immediately. `motor_mon_task` owns all ramp and stall logic.

---

## 2. File Structure

```
main/
├── app_main.cpp              # Entry point, task creation, global state
├── desk_config.h             # All pin definitions and tunable constants
│
├── motor_driver.hpp/.cpp     # LEDC PWM motor control (non-blocking commands)
├── button_reader.hpp/.cpp    # ADC oneshot, analog ladder decode, debounce
├── height_sensor.hpp/.cpp    # VL53L0X I2C wrapper
├── nvs_manager.hpp/.cpp      # NVS read/write for sit/stand presets
├── height_controller.hpp/.cpp# Goto-preset logic (hysteresis)
│
├── display_manager.hpp/.cpp  # esp_lcd SPI init, LVGL driver (flush CB, tick)
├── ui_manager.hpp/.cpp       # LVGL screen layout, animations, state updates
│
├── VL53L0X/
│   ├── VL53L0X.h
│   └── VL53L0X.cpp           # NOTE: Requires ESP-IDF rewrite — see §18
│
├── CMakeLists.txt
├── idf_component.yml
└── Kconfig.projbuild         # (optional) menuconfig entries
```

---

## 3. Build System

### `CMakeLists.txt`

```cmake
idf_component_register(
    SRCS
        "app_main.cpp"
        "motor_driver.cpp"
        "button_reader.cpp"
        "height_sensor.cpp"
        "nvs_manager.cpp"
        "height_controller.cpp"
        "display_manager.cpp"
        "ui_manager.cpp"
        "VL53L0X/VL53L0X.cpp"
    INCLUDE_DIRS
        "."
        "VL53L0X"
    REQUIRES
        nvs_flash
        driver
        esp_lcd
        esp_adc
        lvgl
        esp_timer
        freertos
        log
        esp_task_wdt
)
```

### `idf_component.yml`

```yaml
dependencies:
  lvgl/lvgl: "^8.3"
  # espp/logger removed — using esp_log directly
```

> **Note:** `espp/logger` is removed. All logging uses `ESP_LOGI/W/E` macros with a per-file `TAG`.

---

## 4. Hardware Reference

Quick reference for all GPIO assignments. Source of truth is `desk_config.h`.

| Signal | GPIO | ADC Channel | Direction | Notes |
|--------|------|-------------|-----------|-------|
| DISP_MOSI | 11 | — | OUT | SPI2, 33Ω series |
| DISP_CLK | 12 | — | OUT | SPI2, 33Ω series |
| DISP_CS | 10 | — | OUT | SPI2, 33Ω series |
| DISP_DC | 13 | — | OUT | SPI2, 33Ω series |
| DISP_RST | 14 | — | OUT | Active low |
| MOTOR_PWM_R | 15 | — | OUT | LEDC CH0 — UP drive |
| MOTOR_PWM_L | 16 | — | OUT | LEDC CH1 — DOWN drive |
| MOTOR_EN_R | 17 | — | OUT | BTS7960 UP enable |
| MOTOR_EN_L | 18 | — | OUT | BTS7960 DOWN enable |
| MOTOR_IS_R | 1 | ADC1_CH0 | IN | Current sense UP (4.7kΩ→GND) |
| MOTOR_IS_L | 2 | ADC1_CH1 | IN | Current sense DOWN (4.7kΩ→GND) |
| I2C_SDA | 8 | — | I/O | VL53L0X |
| I2C_SCL | 9 | — | OUT | VL53L0X |
| LIMIT_SW | 4 | — | IN | Active low, 10kΩ pull-up, HW debounce |
| ADC_BTN | 5 | ADC1_CH4 | IN | Analog button ladder |

**Motor driver — BTS7960 direction truth table:**

| EN_R | EN_L | PWM_R | PWM_L | Result |
|------|------|-------|-------|--------|
| 1 | 0 | PWM | 0 | Motor UP |
| 0 | 1 | 0 | PWM | Motor DOWN |
| 0 | 0 | 0 | 0 | Coast (motor off) |

> The worm gear motor is **self-locking**. Coasting (EN=0) is sufficient to hold position. No active braking needed.

---

## 5. Module: `desk_config.h`

Central configuration header. Every tunable value lives here — no magic numbers in `.cpp` files.

```c
#ifndef DESK_CONFIG_H
#define DESK_CONFIG_H

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"

// ─── DISPLAY ────────────────────────────────────────────
#define PIN_DISP_MOSI       GPIO_NUM_11
#define PIN_DISP_CLK        GPIO_NUM_12
#define PIN_DISP_CS         GPIO_NUM_10
#define PIN_DISP_DC         GPIO_NUM_13
#define PIN_DISP_RST        GPIO_NUM_14
#define DISP_SPI_HOST       SPI2_HOST
#define DISP_SPI_FREQ_HZ    (40 * 1000 * 1000)  // 40 MHz
#define DISP_WIDTH          240
#define DISP_HEIGHT         240

// ─── MOTOR ──────────────────────────────────────────────
#define PIN_MOTOR_PWM_R     GPIO_NUM_15   // LEDC CH0 — UP drive
#define PIN_MOTOR_PWM_L     GPIO_NUM_16   // LEDC CH1 — DOWN drive
#define PIN_MOTOR_EN_R      GPIO_NUM_17   // UP half-bridge enable
#define PIN_MOTOR_EN_L      GPIO_NUM_18   // DOWN half-bridge enable

#define MOTOR_LEDC_TIMER    LEDC_TIMER_0
#define MOTOR_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define MOTOR_LEDC_FREQ_HZ  5000           // 5 kHz — silent, within BTS7960 limits
#define MOTOR_LEDC_RES      LEDC_TIMER_10_BIT  // 0–1023
#define MOTOR_LEDC_CH_R     LEDC_CHANNEL_0
#define MOTOR_LEDC_CH_L     LEDC_CHANNEL_1
#define MOTOR_MAX_DUTY      1023           // 100% on 24V supply
#define MOTOR_RAMP_MS       500            // Soft start/stop ramp duration

// ─── CURRENT SENSE ──────────────────────────────────────
// Hardware: 4.7kΩ drain resistor on BTS7960 IS pins.
// At 4.4A stall: V_IS = (4.4 / 8500) * 4700 ≈ 2.43V
// ADC (12-bit, 3.3V ref): 2.43 / 3.3 * 4095 ≈ 3017 raw
// Threshold set below stall with headroom for noise.
#define PIN_MOTOR_IS_R      GPIO_NUM_1    // ADC1_CH0
#define PIN_MOTOR_IS_L      GPIO_NUM_2    // ADC1_CH1
#define MOTOR_IS_ADC_UNIT   ADC_UNIT_1
#define MOTOR_IS_CH_R       ADC_CHANNEL_0
#define MOTOR_IS_CH_L       ADC_CHANNEL_1
#define MOTOR_STALL_THRESHOLD_RAW  2800   // ~2.25V — tune during testing
#define MOTOR_STALL_CONFIRM_COUNT  5      // 5 × 50ms = 250ms confirmation
#define MOTOR_MON_TASK_MS          50     // motor_mon_task tick period
#define MOTOR_INRUSH_IGNORE_MS     500    // Ignore first 500ms of move

// ─── I2C / VL53L0X ──────────────────────────────────────
#define PIN_I2C_SDA         GPIO_NUM_8
#define PIN_I2C_SCL         GPIO_NUM_9
#define I2C_PORT            I2C_NUM_0
#define I2C_FREQ_HZ         400000        // 400 kHz fast mode
#define VL53L0X_ADDR        0x29

// ─── LIMIT SWITCH ───────────────────────────────────────
#define PIN_LIMIT_SW        GPIO_NUM_4    // Active low, ext 10kΩ pull-up

// ─── ANALOG BUTTON LADDER ───────────────────────────────
// 10kΩ pull-up on main board. Button connects ADC pin to GND via resistor.
// ADC_ATTEN_DB_12: 0–3.3V → 0–4095 (12-bit)
#define PIN_ADC_BTN         GPIO_NUM_5
#define BTN_ADC_CHANNEL     ADC_CHANNEL_4
#define BTN_NONE_MIN        1600          // No button: line pulled high
#define BTN_UP_MAX          100           // SW1: 0Ω direct GND
#define BTN_DOWN_MIN        250
#define BTN_DOWN_MAX        500           // SW2: 1kΩ
#define BTN_PRESET1_MIN     600
#define BTN_PRESET1_MAX     900           // SW3: 2.2kΩ
#define BTN_PRESET2_MIN     1150
#define BTN_PRESET2_MAX     1450          // SW4: 4.7kΩ
#define BTN_DEBOUNCE_MS     50
#define BTN_HOLD_SAVE_MS    3000          // Hold preset button 3s to save

// ─── DESK LIMITS ────────────────────────────────────────
#define DESK_MIN_HEIGHT_MM  650
#define DESK_MAX_HEIGHT_MM  1200
#define DESK_GOTO_TOLERANCE_MM  5         // ±5mm hysteresis band
#define DESK_DEFAULT_SIT_MM    730
#define DESK_DEFAULT_STAND_MM  1100

// ─── NVS ────────────────────────────────────────────────
#define NVS_NAMESPACE       "desk_mem"
#define NVS_KEY_SIT         "h_sit"
#define NVS_KEY_STAND       "h_stand"

// ─── CALIBRATION ────────────────────────────────────────
#define CALIB_TIMEOUT_MS    30000         // Max calibration move time (30s)

// ─── TASK CONFIG ────────────────────────────────────────
#define TASK_STACK_APP      6144          // Increased: goto_height nests I2C + motor calls
#define TASK_STACK_LVGL     8192
#define TASK_STACK_MOTOR_MON 2048
#define TASK_PRIO_APP       5
#define TASK_PRIO_LVGL      4
#define TASK_PRIO_MOTOR_MON 6             // Higher than app — safety critical
#define TASK_CORE_APP       0
#define TASK_CORE_LVGL      1
#define TASK_CORE_MOTOR_MON 0

#endif // DESK_CONFIG_H
```

---

## 6. Module: `motor_driver`

### Responsibility
Controls the single 5840-31ZY worm gear motor via LEDC PWM and BTS7960 H-bridge. **All commands are non-blocking.** `motor_mon_task` owns the ramp (soft start/stop) and current-sense stall detection.

### Design Note — Non-Blocking Commands

`move_up()`, `move_down()`, and `stop()` return immediately after setting internal state. The ramp runs inside `motor_mon_task` on each 50ms tick. This keeps `app_task` fully responsive during motor start/stop and means the ramp and stall detection share the same execution context with no concurrency hazard.

### Public Interface

```cpp
// motor_driver.hpp
#pragma once
#include <functional>
#include "desk_config.h"

enum class MotorDirection { UP, DOWN, STOPPED };

class MotorDriver {
public:
    using StallCallback = std::function<void()>;

    // Init LEDC timer+channels, GPIO enables, ADC unit, spawn monitor task.
    // Returns false if any peripheral init fails.
    bool init();

    // Non-blocking. Sets direction and starts ramp in motor_mon_task.
    // Clears any previous stall state.
    void move_up();
    void move_down();

    // Non-blocking soft stop. motor_mon_task ramps duty to 0.
    void stop();

    // Immediate stop — no ramp. Safe to call from any task or ISR.
    void emergency_stop();

    // Register callback invoked from motor_mon_task when stall detected.
    void set_stall_callback(StallCallback cb);

    MotorDirection direction() const { return direction_; }
    bool is_stalled()          const { return stalled_; }
    bool is_running()          const; // true when RUNNING_UP or RUNNING_DOWN

    // Expose ADC handle so button_reader can share it.
    adc_oneshot_unit_handle_t adc_handle() const { return adc_; }

private:
    // Called only from monitor_task.
    void set_duty_raw(uint32_t duty, MotorDirection dir);
    void monitor_loop();
    static void monitor_task(void* arg);

    // Internal motor state for ramp management.
    enum class MotorState {
        STOPPED,
        RAMPING_UP,
        RUNNING_UP,
        RAMPING_DOWN,   // Soft stop, was going UP
        RUNNING_DOWN,
        RAMPING_DOWN2,  // Soft stop, was going DOWN
        STOPPING,       // Emergency or direction change pending
    };

    volatile MotorState  motor_state_ = MotorState::STOPPED;
    volatile MotorDirection direction_ = MotorDirection::STOPPED;
    volatile uint32_t    current_duty_ = 0;
    volatile bool        stalled_      = false;
    TickType_t           ramp_start_tick_ = 0;
    TickType_t           move_start_tick_ = 0;  // For inrush ignore window

    adc_oneshot_unit_handle_t adc_ = nullptr;
    StallCallback        stall_cb_ = nullptr;
    TaskHandle_t         monitor_task_handle_ = nullptr;
};
```

### Implementation Notes

**LEDC Init (in `init()`):**
```
Timer:   LEDC_TIMER_0, LEDC_LOW_SPEED_MODE, 5000Hz, 10-bit
CH0:     GPIO 15 (PWM_R / UP)
CH1:     GPIO 16 (PWM_L / DOWN)
Initial duty: 0 on both channels
```

**ADC Init (in `init()`):**
```
Unit:    ADC_UNIT_1
Atten:   ADC_ATTEN_DB_12
Width:   ADC_BITWIDTH_DEFAULT (12-bit)
CH0:     GPIO1 (IS_R)
CH1:     GPIO2 (IS_L)
```

**`move_up()` (non-blocking):**
1. Clear `stalled_`.
2. Set EN_R=1, EN_L=0.
3. Set `motor_state_ = RAMPING_UP`, record `ramp_start_tick_` and `move_start_tick_`.
4. Return immediately — `motor_mon_task` drives the ramp.

**`move_down()` (non-blocking):** Same but EN_R=0, EN_L=1, state = `RAMPING_DOWN2` (ramp up from 0 on CH1).

> **Note on state naming:** `RAMPING_DOWN` = ramping duty to 0 while stopping from an UP move. `RAMPING_DOWN2` = ramping duty up on CH1 to start a DOWN move. Naming reflects physical direction, not duty direction.

**`stop()` (non-blocking):**
1. Set `motor_state_` to `RAMPING_DOWN` (if was UP) or `RAMPING_DOWN2_STOP` as appropriate.
2. Return immediately — `motor_mon_task` ramps duty to 0 then sets EN=0.

**`emergency_stop()` (immediate, any context):**
1. Set LEDC duty=0 on both channels immediately via `ledc_set_duty()` + `ledc_update_duty()`.
2. Set EN_R=0, EN_L=0.
3. Set `motor_state_ = STOPPED`, `direction_ = STOPPED`, `stalled_ = true`.

**Monitor Task loop (`MOTOR_MON_TASK_MS` = 50ms period):**

```
monitor_loop():
    while true:
        elapsed_ramp = (now - ramp_start_tick_) in ms

        switch motor_state_:

            case RAMPING_UP / RAMPING_DOWN2 (start move):
                // Ramp duty 0 → MOTOR_MAX_DUTY over MOTOR_RAMP_MS
                duty = min((elapsed_ramp * MOTOR_MAX_DUTY) / MOTOR_RAMP_MS,
                           MOTOR_MAX_DUTY)
                set_duty_raw(duty, direction_)
                if duty >= MOTOR_MAX_DUTY:
                    motor_state_ = RUNNING_UP / RUNNING_DOWN
                    move_start_tick_ = now  // Inrush window starts at full speed

            case RUNNING_UP / RUNNING_DOWN:
                // Check for soft limit via current direction + stall sense
                elapsed_move = (now - move_start_tick_) in ms
                if elapsed_move > MOTOR_INRUSH_IGNORE_MS:
                    ch = (direction_ == UP) ? IS_CH_R : IS_CH_L
                    raw = adc_oneshot_read(adc_, ch)
                    if raw > MOTOR_STALL_THRESHOLD_RAW:
                        stall_counter++
                    else:
                        stall_counter = 0
                    if stall_counter >= MOTOR_STALL_CONFIRM_COUNT:
                        emergency_stop()
                        invoke stall_cb_

            case STOPPING (soft stop requested):
                // Ramp duty MOTOR_MAX_DUTY → 0 over MOTOR_RAMP_MS
                duty = max(MOTOR_MAX_DUTY -
                           (elapsed_ramp * MOTOR_MAX_DUTY) / MOTOR_RAMP_MS,
                           0)
                set_duty_raw(duty, direction_)
                if duty == 0:
                    EN_R = 0, EN_L = 0
                    motor_state_ = STOPPED
                    direction_ = STOPPED

            case STOPPED:
                stall_counter = 0
                // Nothing to do

        vTaskDelay(pdMS_TO_TICKS(MOTOR_MON_TASK_MS))
```

> **Ramp resolution:** With a 50ms task tick and 500ms ramp, each tick advances duty by ~102 counts (1023 / 10 steps). The ramp is time-based on elapsed ticks, not step-counted, so minor task scheduling jitter has no effect.

---

## 7. Module: `button_reader`

### Responsibility
Reads the analog button ladder on GPIO5 using ADC oneshot. Provides:
- `read()` — current debounced button state (for hold detection)
- `read_edge()` — fires only on the rising edge of a new press (for one-shot actions)

Shares the ADC unit handle with `motor_driver`.

> **ADC handle sharing:** `motor_driver` creates and owns the `ADC_UNIT_1` handle. `button_reader` receives it at `init()` and calls `adc_oneshot_config_channel()` on it for GPIO5/CH4. `motor_mon_task` reads CH0/CH1 (current sense); `app_task` reads CH4 (buttons). These are separate channels on the same unit. ESP-IDF documents `adc_oneshot_read()` as thread-safe for concurrent reads on separate channels of the same unit.

### Public Interface

```cpp
// button_reader.hpp
#pragma once
#include "esp_adc/adc_oneshot.h"

enum class Button { NONE, UP, DOWN, PRESET1, PRESET2 };

class ButtonReader {
public:
    // Takes shared ADC unit handle from motor_driver.
    // Configures GPIO5/CH4 on that unit.
    bool init(adc_oneshot_unit_handle_t shared_adc);

    // Returns the current debounced button state.
    // Call from app_task for hold-to-move detection (UP/DOWN buttons).
    Button read();

    // Returns the button only on the rising edge (first stable press).
    // Returns NONE if same button is held or no press.
    // Call from app_task for one-shot actions (PRESET1/PRESET2 buttons).
    Button read_edge();

private:
    Button decode(int raw);

    adc_oneshot_unit_handle_t adc_        = nullptr;
    Button last_stable_                   = Button::NONE;  // Last confirmed state
    Button debounce_candidate_            = Button::NONE;
    TickType_t debounce_start_            = 0;
};
```

### ADC Decode Table

| Raw ADC value | Button | Resistor |
|---------------|--------|----------|
| < 100 | UP | 0Ω |
| 250 – 500 | DOWN | 1kΩ |
| 600 – 900 | PRESET1 (Sit) | 2.2kΩ |
| 1150 – 1450 | PRESET2 (Stand) | 4.7kΩ |
| > 1600 | NONE | No press |

### Debounce and State Logic

Both `read()` and `read_edge()` share a single internal debounce filter. The filter updates `last_stable_` only after a candidate state is held for `BTN_DEBOUNCE_MS` (50ms).

```
// Internal: update debounce state, return current stable state
Button _update():
    raw = adc_oneshot_read(CH4)
    candidate = decode(raw)

    if candidate != debounce_candidate_:
        debounce_candidate_ = candidate
        debounce_start_ = now

    if (now - debounce_start_) >= BTN_DEBOUNCE_MS:
        last_stable_ = candidate   // Confirmed state

    return last_stable_

read():
    return _update()               // Current confirmed state (held = repeats same value)

read_edge():
    prev = last_stable_
    current = _update()
    if current != NONE and current != prev:
        return current             // Rising edge only
    return NONE
```

### Usage in app_task

- **UP / DOWN** → use `read()` — motor runs while button returns UP/DOWN, stops when NONE.
- **PRESET1 / PRESET2** → use `read_edge()` + hold-timer check — fires once per press, hold 3s to save.

---

## 8. Module: `height_sensor` (VL53L0X)

### Responsibility
Wraps the VL53L0X Time-of-Flight I2C sensor to provide absolute desk height readings.

> **Important:** The current `VL53L0X.cpp` uses the Arduino `Wire` I2C API and **cannot be compiled under ESP-IDF**. A rewrite using ESP-IDF `i2c_master` is required before this module is functional. See **§18** for the full rewrite plan. This module's interface is defined here so the rest of the application can be written against it.

### Public Interface

```cpp
// height_sensor.hpp
#pragma once
#include <stdint.h>

class HeightSensor {
public:
    // Init I2C bus (I2C_NUM_0) and VL53L0X.
    // Returns false if sensor not found or init fails.
    bool init();

    // Perform a single ranging measurement.
    // Returns distance in mm, or -1 on error.
    int read_mm();

    // Returns true if last read was valid (no timeout, no out-of-range).
    bool is_valid() const { return last_valid_; }

private:
    bool last_valid_ = false;
};
```

### Implementation Notes

- Uses `VL53L0X/VL53L0X.cpp` after ESP-IDF rewrite (see §18).
- I2C init: `I2C_NUM_0`, SDA=GPIO8, SCL=GPIO9, 400kHz.
- Sensor mounted pointing **downward** — measures distance to floor. Height calculation: `height_mm = calib_offset_mm - raw_distance_mm`. The offset is the sensor reading taken at the limit switch position during calibration.
- The VL53L0X has a practical range of ~20–1200mm. At desk heights 650–1200mm, readings are reliable.
- Single-shot ranging blocks ~30ms. Called from `app_task`.

### Calibration Offset

During the calibration sequence (§13), when the limit switch triggers, the sensor reading is stored as the reference (`calib_offset_mm`). All subsequent heights are relative to that. Stored in NVS.

---

## 9. Module: `nvs_manager`

### Responsibility
Reads and writes sit/stand preset heights and the calibration offset to NVS flash.

### Public Interface

```cpp
// nvs_manager.hpp
#pragma once
#include <stdint.h>

struct DeskPresets {
    int sit_mm;            // default: DESK_DEFAULT_SIT_MM
    int stand_mm;          // default: DESK_DEFAULT_STAND_MM
    int calib_offset_mm;   // sensor offset from limit switch calibration (0 = uncalibrated)
};

class NvsManager {
public:
    // Call nvs_flash_init() and open namespace. Returns false on failure.
    bool init();

    // Load presets from NVS. Fills defaults if keys missing.
    DeskPresets load();

    // Persist presets to NVS.
    bool save(const DeskPresets& presets);

    // Persist only the calibration offset.
    bool save_calib_offset(int offset_mm);

private:
    bool opened_ = false;
};
```

### NVS Keys

| Key | Type | Default |
|-----|------|---------|
| `"h_sit"` | int32 | 730 |
| `"h_stand"` | int32 | 1100 |
| `"h_calib"` | int32 | 0 (uncalibrated) |

---

## 10. Module: `height_controller`

### Responsibility
Executes a "goto preset" move: drives the motor up or down until the desk reaches the target height within tolerance, then stops. Uses hysteresis (not PID) for simplicity and reliability.

### Public Interface

```cpp
// height_controller.hpp
#pragma once
#include "motor_driver.hpp"
#include "height_sensor.hpp"

enum class GotoResult {
    SUCCESS,
    STALLED,        // Motor stall detected mid-move
    SENSOR_ERROR,   // Height sensor returned invalid data
    LIMIT_REACHED,  // Hard limit (soft or physical) hit
    CANCELLED,      // Caller cancelled (e.g. button press during move)
    TIMEOUT,        // Move exceeded maximum expected time
};

class HeightController {
public:
    HeightController(MotorDriver& motor, HeightSensor& sensor);

    // Blocking. Drives motor until height within tolerance of target_mm.
    // Polls sensor every GOTO_POLL_MS. Checks cancel_flag each cycle.
    // Feeds the task watchdog each cycle.
    // Returns reason for completion.
    GotoResult goto_height(int target_mm, volatile bool& cancel_flag);

private:
    MotorDriver&  motor_;
    HeightSensor& sensor_;
};
```

### Goto Algorithm

```
#define GOTO_POLL_MS       100
#define GOTO_TIMEOUT_MS    60000   // 60s max move time (1200mm / ~20mm/s)

goto_height(target_mm, cancel_flag):
    esp_task_wdt_add(NULL)  // Register current task with watchdog

    current = sensor.read_mm()
    if current == -1: return SENSOR_ERROR

    if current < target_mm - TOLERANCE:
        motor.move_up()
        direction = UP
    elif current > target_mm + TOLERANCE:
        motor.move_down()
        direction = DOWN
    else:
        esp_task_wdt_delete(NULL)
        return SUCCESS  // Already in range

    start_tick = xTaskGetTickCount()

    while true:
        esp_task_wdt_reset()   // Feed watchdog each cycle
        vTaskDelay(pdMS_TO_TICKS(GOTO_POLL_MS))

        // Timeout guard
        if (xTaskGetTickCount() - start_tick) > pdMS_TO_TICKS(GOTO_TIMEOUT_MS):
            motor.stop()
            esp_task_wdt_delete(NULL)
            return TIMEOUT

        if cancel_flag:
            motor.stop()
            esp_task_wdt_delete(NULL)
            return CANCELLED

        if motor.is_stalled():
            esp_task_wdt_delete(NULL)
            return STALLED

        current = sensor.read_mm()
        if current == -1:
            motor.emergency_stop()
            esp_task_wdt_delete(NULL)
            return SENSOR_ERROR

        // Soft limit checks
        if current <= DESK_MIN_HEIGHT_MM and direction == DOWN:
            motor.stop()
            esp_task_wdt_delete(NULL)
            return LIMIT_REACHED
        if current >= DESK_MAX_HEIGHT_MM and direction == UP:
            motor.stop()
            esp_task_wdt_delete(NULL)
            return LIMIT_REACHED

        // Target reached?
        if abs(current - target_mm) <= TOLERANCE:
            motor.stop()
            esp_task_wdt_delete(NULL)
            return SUCCESS

        // Overshoot correction (safety fallback — not expected in normal operation)
        if direction == UP and current > target_mm + TOLERANCE:
            motor.stop()
            motor.move_down()
            direction = DOWN
        elif direction == DOWN and current < target_mm - TOLERANCE:
            motor.stop()
            motor.move_up()
            direction = UP
```

> **Note:** The worm gear motor is slow (~5–10mm/s desk travel). At 100ms poll rate, overshoot will be <1mm — well within the 5mm tolerance. The overshoot correction branch is a safety fallback. `GOTO_TIMEOUT_MS = 60s` covers a full 550mm stroke at worst-case speed.

---

## 11. Module: `display_manager`

### Responsibility
Initializes the SPI bus and ST7789 LCD panel via `esp_lcd`. Initializes LVGL, installs the flush callback, and provides the LVGL tick timer. Does **not** create any LVGL widgets — that is `ui_manager`'s job.

### Public Interface

```cpp
// display_manager.hpp
#pragma once

class DisplayManager {
public:
    // Init SPI bus, esp_lcd panel, LVGL, flush CB, tick timer.
    // Must be called from lvgl_task before any LVGL calls.
    bool init();

    // Call periodically from lvgl_task to drive LVGL rendering.
    void tick();

private:
    static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_map);
    static bool notify_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                   esp_lcd_panel_io_event_data_t* edata, void* ctx);
    static void lvgl_tick_cb(void* arg);

    esp_lcd_panel_handle_t    panel_    = nullptr;
    esp_lcd_panel_io_handle_t io_       = nullptr;
    lv_disp_drv_t             disp_drv_;
    lv_disp_draw_buf_t        draw_buf_;
    lv_color_t*               buf1_     = nullptr;
    lv_color_t*               buf2_     = nullptr;
    esp_timer_handle_t        tick_timer_ = nullptr;
    SemaphoreHandle_t         flush_sem_ = nullptr;
};
```

### SPI + LCD Init Sequence

```
1. spi_bus_initialize(SPI2_HOST, {mosi=11, clk=12, miso=-1}, SPI_DMA_CH_AUTO)

2. esp_lcd_new_panel_io_spi():
   - cs=10, dc=13
   - pclk_hz = 40MHz
   - lcd_cmd_bits = 8, lcd_param_bits = 8
   - on_color_trans_done = notify_flush_ready (signals flush semaphore)

3. esp_lcd_new_panel_st7789():
   - rst=14
   - color_space = ESP_LCD_COLOR_SPACE_BGR
   - bits_per_pixel = 16
   - vendor_config: force 240×240 window (prevents 240×320 default offset)

4. esp_lcd_panel_reset()
5. esp_lcd_panel_init()
6. esp_lcd_panel_invert_color(true)        // IPS panel requires inversion
7. esp_lcd_panel_set_gap(0, 0)             // No gap offset for 240×240
8. esp_lcd_panel_swap_xy(false)
9. esp_lcd_panel_mirror(true, false)       // Verify orientation on hardware
10. esp_lcd_panel_disp_on_off(true)

LVGL init:
11. lv_init()
12. Allocate buf1_ and buf2_ (each: DISP_WIDTH * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA)
13. lv_disp_draw_buf_init(&draw_buf_, buf1_, buf2_, DISP_WIDTH * 40)
14. lv_disp_drv_init(&disp_drv_)
    - drv.flush_cb = flush_cb
    - drv.draw_buf = &draw_buf_
    - drv.hor_res = 240, ver_res = 240
    - drv.full_refresh = 0
15. lv_disp_drv_register(&disp_drv_)

Tick timer:
16. esp_timer_create() with lvgl_tick_cb, period=1ms
17. esp_timer_start_periodic()
```

### Flush Callback

```cpp
flush_cb(drv, area, color_map):
    esp_lcd_panel_draw_bitmap(panel_, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, color_map)
    xSemaphoreTake(flush_sem_, portMAX_DELAY)  // Wait for DMA transfer
    lv_disp_flush_ready(drv)
```

### `tick()` (called from `lvgl_task` loop)

```cpp
tick():
    lv_task_handler()      // Process LVGL events + render
    vTaskDelay(pdMS_TO_TICKS(5))
```

> The LVGL tick timer (1ms via esp_timer) calls `lv_tick_inc(1)`. The `lv_task_handler()` call in `tick()` processes the accumulated ticks.

---

## 12. Module: `ui_manager`

### Responsibility
Owns all LVGL widgets. Provides methods for each UI state transition. Must only be called from `lvgl_task` or via `lv_async_call()`.

### UI States

```
STARTUP      → Plays letter-by-letter "MoTrotten" animation → transitions to IDLE
IDLE         → Shows current height (large text). Pulses gently.
MOVING_UP    → Shows height + upward scrolling arrows (cyan, 3-arrow trail)
MOVING_DOWN  → Shows height + downward scrolling arrows (cyan, 3-arrow trail)
GOTO_PRESET  → Shows height + target height + progress indicator
STALLED      → Red flash + "BLOCKED" text for 2s → returns to IDLE
CALIBRATING  → Shows "Calibrating..." text
```

### Public Interface

```cpp
// ui_manager.hpp
#pragma once
#include "lvgl.h"

class UIManager {
public:
    // Create all LVGL objects. Call once from lvgl_task after display_manager.init().
    void init();

    // State transitions — call via lv_async_call() from app_task.
    void show_startup(std::function<void()> on_complete);
    void show_idle(float height_mm);
    void show_moving_up(float height_mm);
    void show_moving_down(float height_mm);
    void show_goto_preset(float current_mm, float target_mm);
    void show_stalled();
    void show_calibrating();
    void show_saved_confirmation();

    // Update height reading without changing state (during movement).
    void update_height(float height_mm);
};
```

### Layout

```
┌────────────────────────┐
│                        │  ← 240×240 black background
│    [arrow trail]       │  ← Visible only in MOVING/GOTO states
│                        │
│        1032            │  ← height_label: large, bold, cyan
│          mm            │  ← unit_label: small, white
│                        │
│   [→ 1100 mm]          │  ← target_label: small, visible in GOTO state only
│                        │
└────────────────────────┘
```

### Animation Details

**Startup:** Letter labels for "MoTrotten" fade in one by one (staggered `lv_anim` with delay * index), then container fades out, idle screen fades in.

**Arrow trail (move up/down):**
- 3 arrow labels stacked vertically, scrolling in direction of movement.
- Lead arrow: 100% opacity (cyan bright). Trail 1: 50%. Trail 2: 20%.
- Animates via `lv_anim` on Y position, loops indefinitely.
- Stops and hides when `show_idle()` is called.

**Stall indicator:**
- Screen background flashes red 3× (`lv_anim` on bg color).
- "BLOCKED" label fades in for 2s.
- Calls `show_idle()` after animation completes.

### Async Call Pattern (zero-allocation)

Do **not** heap-allocate arguments for `lv_async_call`. Pass `&g_state` (the existing global pointer) and read the current values inside the callback:

```cpp
// app_task — safe cross-task UI update, no heap allocation:
extern DeskState g_state;

lv_async_call([](void* a) {
    auto* s = static_cast<DeskState*>(a);
    ui_manager.update_height(static_cast<float>(s->current_height_mm));
}, &g_state);

lv_async_call([](void* a) {
    auto* s = static_cast<DeskState*>(a);
    ui_manager.show_moving_up(static_cast<float>(s->current_height_mm));
}, &g_state);
```

> `g_state.current_height_mm` is `volatile int`, updated by `app_task` just before the async call is posted. The LVGL task reads the latest value when it processes the callback. No per-call allocation, no heap fragmentation.

---

## 13. Application: `app_main`

### Responsibility
Entry point. Initializes all modules, runs the calibration sequence on first boot, creates FreeRTOS tasks, and never returns.

### Shared State

```cpp
// In app_main.cpp (file-scope)
struct DeskState {
    volatile int          current_height_mm;  // Updated by app_task from sensor
    volatile bool         goto_cancel;        // Set true by any button press to abort goto
    volatile bool         stall_detected;     // Set by motor stall callback
    volatile DeskStateEnum app_state;         // Current application state
    DeskPresets           presets;            // Loaded from NVS
    SemaphoreHandle_t     mutex;              // Guards presets (non-volatile field)
};
static DeskState g_state;
```

> `current_height_mm`, `goto_cancel`, `stall_detected`, and `app_state` are `volatile` and accessed as single atomic operations. They do not require the mutex. `presets` is guarded by the mutex because it is read and written from multiple code paths in `app_task`.

### Initialization Sequence (`app_main`)

```
1. nvs_manager.init()           → nvs_flash_init + open namespace
2. presets = nvs_manager.load() → load h_sit, h_stand, h_calib
3. motor_driver.init()          → LEDC + GPIO + ADC unit creation + spawn motor_mon_task
4. button_reader.init(motor_driver.adc_handle())  → configure CH4 on shared ADC unit
5. height_sensor.init()         → I2C bus + VL53L0X init
6. g_state.mutex = xSemaphoreCreateMutex()
7. g_state.presets = presets
8. g_state.app_state = DeskStateEnum::STARTUP
9. motor_driver.set_stall_callback(→ sets g_state.stall_detected = true)

10. Create lvgl_task (Core 1, prio 4, stack 8192)
    → display_manager.init()
    → ui_manager.init()
    → ui_manager.show_startup(→ sends task notification to app_main when done)
    → loop: display_manager.tick()

11. ulTaskNotifyTake(pdTRUE, portMAX_DELAY)  // Wait for startup animation

12. If presets.calib_offset_mm == 0 (uncalibrated):
    g_state.app_state = DeskStateEnum::CALIBRATING
    lv_async_call → ui_manager.show_calibrating()

    motor_driver.move_down()
    start_tick = xTaskGetTickCount()
    while gpio_get_level(PIN_LIMIT_SW) != 0:
        if (xTaskGetTickCount() - start_tick) > pdMS_TO_TICKS(CALIB_TIMEOUT_MS):
            motor_driver.emergency_stop()
            ESP_LOGE(TAG, "Calibration timeout — limit switch not triggered")
            // Halt: do not proceed without calibration
            while(true) vTaskDelay(portMAX_DELAY)
        vTaskDelay(pdMS_TO_TICKS(10))
    motor_driver.stop()
    offset = height_sensor.read_mm()
    nvs_manager.save_calib_offset(offset)
    g_state.presets.calib_offset_mm = offset

13. g_state.app_state = DeskStateEnum::IDLE
    lv_async_call → ui_manager.show_idle(g_state.current_height_mm)

14. Create app_task (Core 0, prio 5, stack TASK_STACK_APP)

15. vTaskDelete(NULL)  → delete app_main task
```

### `app_task` Loop

```
app_task():
    bool manual_moving = false  // True when motor is running due to held UP/DOWN button

    while true:
        // ── 1. Read height sensor ──────────────────────────────────
        height = height_sensor.read_mm()
        if height != -1:
            g_state.current_height_mm = height

        // ── 2. Handle stall (highest priority) ────────────────────
        if g_state.stall_detected:
            g_state.stall_detected = false
            manual_moving = false
            g_state.app_state = DeskStateEnum::STALLED
            lv_async_call → ui_manager.show_stalled()
            goto loop_end

        // ── 3. Read button state for hold-to-move (UP/DOWN) ───────
        Button held  = button_reader.read()      // Current held state
        Button edge  = button_reader.read_edge() // Rising edge (for presets)

        // ── 4. Manual move: hold-to-move with soft limits ─────────
        if held == Button::UP:
            if height >= DESK_MAX_HEIGHT_MM:
                // At or past upper limit — stop
                if manual_moving:
                    motor_driver.stop()
                    manual_moving = false
                    g_state.app_state = DeskStateEnum::IDLE
                    lv_async_call → ui_manager.show_idle()
            elif not manual_moving or motor_driver.direction() != UP:
                motor_driver.move_up()
                manual_moving = true
                g_state.app_state = DeskStateEnum::MOVING_UP
                lv_async_call → ui_manager.show_moving_up()

        elif held == Button::DOWN:
            if height <= DESK_MIN_HEIGHT_MM:
                if manual_moving:
                    motor_driver.stop()
                    manual_moving = false
                    g_state.app_state = DeskStateEnum::IDLE
                    lv_async_call → ui_manager.show_idle()
            elif not manual_moving or motor_driver.direction() != DOWN:
                motor_driver.move_down()
                manual_moving = true
                g_state.app_state = DeskStateEnum::MOVING_DOWN
                lv_async_call → ui_manager.show_moving_down()

        else:
            // No UP/DOWN held — stop if we were in manual mode
            if manual_moving:
                motor_driver.stop()
                manual_moving = false
                g_state.app_state = DeskStateEnum::IDLE
                lv_async_call → ui_manager.show_idle()

        // ── 5. Preset buttons (edge-triggered, only when idle) ────
        if not manual_moving and g_state.app_state == DeskStateEnum::IDLE:

            if edge == Button::PRESET1 or edge == Button::PRESET2:
                int target = (edge == PRESET1) ? presets.sit_mm : presets.stand_mm

                // Check hold-to-save gesture
                hold_start = xTaskGetTickCount()
                while button_reader.read() == edge:  // Button held
                    if (xTaskGetTickCount() - hold_start) > pdMS_TO_TICKS(BTN_HOLD_SAVE_MS):
                        // Save current height as this preset
                        xSemaphoreTake(g_state.mutex, portMAX_DELAY)
                        if edge == PRESET1: g_state.presets.sit_mm = height
                        else:               g_state.presets.stand_mm = height
                        nvs_manager.save(g_state.presets)
                        xSemaphoreGive(g_state.mutex)
                        lv_async_call → ui_manager.show_saved_confirmation()
                        goto loop_end
                    vTaskDelay(pdMS_TO_TICKS(20))

                // Not a save — execute goto preset
                g_state.goto_cancel = false
                g_state.app_state = DeskStateEnum::GOTO_PRESET
                lv_async_call → ui_manager.show_goto_preset(height, target)
                result = height_controller.goto_height(target, g_state.goto_cancel)
                g_state.app_state = DeskStateEnum::IDLE
                lv_async_call → ui_manager.show_idle()
                // (stall result handled next cycle via g_state.stall_detected)

        // ── 6. Update height display during movement ───────────────
        if motor_driver.direction() != MotorDirection::STOPPED:
            lv_async_call → ui_manager.update_height()  // reads g_state.current_height_mm

        loop_end:
        vTaskDelay(pdMS_TO_TICKS(50))   // 20Hz app loop
```

> **Manual move cancel:** If a goto is in progress and the user presses UP/DOWN (or any button), the app_task loop checks `g_state.goto_cancel`. The cancel is triggered via the `else` branch catching NONE on held (no button pressed) during a goto, or by any button edge. In practice, set `g_state.goto_cancel = true` whenever `held != NONE` and `app_state == GOTO_PRESET`.

---

## 14. FreeRTOS Design

### Tasks

| Task | Core | Priority | Stack | Function |
|------|------|----------|-------|----------|
| `app_task` | 0 | 5 | 6144 | Button poll, height read, state machine |
| `lvgl_task` | 1 | 4 | 8192 | LVGL render loop (never blocked by app) |
| `motor_mon_task` | 0 | 6 | 2048 | Soft start/stop ramp + current sense stall detect |

> `motor_mon_task` has the **highest priority** on Core 0. It handles both the ramp and stall detection, ensuring neither is blocked by app logic or I2C reads.

### Inter-Task Communication

| From | To | Mechanism | Data |
|------|----|-----------|------|
| `app_task` | `lvgl_task` | `lv_async_call()` | Callback reads `g_state` — no allocation |
| `motor_mon_task` | `app_task` | `volatile bool g_state.stall_detected` | bool |
| `app_main` | `app_task` | Task notification | Startup complete |
| `app_task` | `motor_mon_task` | `volatile MotorState` in `MotorDriver` | State command |

### Mutex Usage

`g_state.mutex` protects `g_state.presets` — read during goto, written during save-preset gesture. Volatile fields (`current_height_mm`, `goto_cancel`, `stall_detected`, `app_state`) are single-word writes on Xtensa and do not require the mutex.

### Watchdog

`goto_height()` uses `esp_task_wdt_add/reset/delete` to keep the task watchdog fed during long blocking moves. Other long operations (calibration in `app_main`, LVGL rendering in `lvgl_task`) must also be verified against the configured TWDT timeout (default 5s).

### Stack Size Rationale

| Task | Reason |
|------|--------|
| `lvgl_task`: 8192 | LVGL allocates internal buffers on stack during render |
| `app_task`: 6144 | `goto_height()` nests: sensor I2C + motor state reads. Increased from 4096 — verify with `uxTaskGetStackHighWaterMark()` during integration. |
| `motor_mon_task`: 2048 | ADC read + ramp math + comparison — minimal stack |

---

## 15. State Machine

```
           ┌──────────┐
    Boot → │ STARTUP  │ ── animation done ──────────────────────┐
           └──────────┘                                          │
                                                     (calib if needed)
                                                                 ▼
           ┌──────────────────────────────────────────────── IDLE ────┐
           │              ↑ stop / done / timeout                      │
           │                                                           │
     BTN_UP/DOWN held                                         BTN_PRESET1/2 edge
           │                                                           │
           ▼                                                           ▼
      MOVING_UP ── stall ──────────────────────────────────→ GOTO_PRESET
      MOVING_DOWN ─ stall ──────────┐                           │     │
                                    │                  stall    │     │ reached / timeout
                   limit reached ───┤                   │       ▼     │
                                    ▼                   └─→ STALLED   │
                                STALLED                     │         │
                                    │                    2s timeout   │
                                2s timeout                  │         │
                                    │                       ▼         │
                                    ▼                     IDLE ←──────┘
                                  IDLE
```

### State Enum

```cpp
enum class DeskStateEnum {
    STARTUP,
    CALIBRATING,
    IDLE,
    MOVING_UP,
    MOVING_DOWN,
    GOTO_PRESET,
    STALLED,
};
```

The active state is stored in `g_state.app_state` (volatile). UI state is driven by `lv_async_call()` transitions from `app_task` — both states should always change together.

---

## 16. Safety Architecture

### Layers (outermost to innermost)

| Layer | Mechanism | Response |
|-------|-----------|----------|
| **Physical** | Worm gear self-locking | Holds position without power |
| **Hardware** | BTS7960 thermal shutdown | Driver disables itself |
| **Firmware — stall** | ADC current threshold over 250ms | `emergency_stop()` + STALLED UI |
| **Firmware — manual limits** | app_task height check on every 50ms cycle during UP/DOWN hold | `stop()` when limit reached |
| **Firmware — goto limits** | `goto_height()` polls height vs soft min/max | `stop()` + LIMIT_REACHED |
| **Firmware — goto timeout** | 60s maximum move time | `stop()` + TIMEOUT result |
| **Firmware — limit switch** | GPIO4 active-low during calibration | `stop()` immediately; CALIB_TIMEOUT_MS guards against stuck switch |
| **Application** | Any button press during goto sets `goto_cancel` | `height_controller` returns CANCELLED |

### Manual Move Limit Enforcement

During hold-to-move (UP/DOWN buttons held), `app_task` checks the current sensor height against `DESK_MIN/MAX_HEIGHT_MM` **every 50ms**. If the limit is reached:
1. `motor_driver.stop()` is called (soft ramp-down via `motor_mon_task`).
2. `manual_moving` is cleared.
3. UI transitions to IDLE.

This closes the gap where `goto_height()` enforced limits but raw manual moves did not.

### Stall Detection Calibration

The threshold `MOTOR_STALL_THRESHOLD_RAW = 2800` is a starting point. During hardware testing:

1. Run motor freely — log peak ADC reading during inrush (first 500ms).
2. Block motor by hand — log ADC at stall.
3. Set threshold midway between free-run peak and stall.
4. Verify with `MOTOR_STALL_CONFIRM_COUNT=5` (250ms) that no false triggers occur.

### Limit Switch Behavior

- GPIO4 is active-low (external 10kΩ pull-up, reads HIGH when not triggered).
- Used exclusively during calibration: motor moves DOWN until `gpio_get_level(PIN_LIMIT_SW) == 0`.
- `CALIB_TIMEOUT_MS = 30000` (30s) guards against a disconnected or stuck-high switch.
- During normal operation, soft limits via sensor height replace the limit switch.

---

## 17. Open Questions

| # | Question | Status |
|---|----------|--------|
| 1 | **Display byte swap:** Does this ST7789 batch need `rgb_ele_order = BGR` or RGB? | Verify on hardware — easy fix |
| 2 | **Display mirror/swap_xy:** Correct orientation needs testing on physical unit | Verify on hardware — easy fix |
| 3 | **VL53L0X driver compatibility:** Current `VL53L0X.cpp` uses Arduino Wire API | **Resolved — see §18 for rewrite plan** |
| 4 | **ADC unit cross-task safety:** `motor_mon_task` reads CH0/CH1; `app_task` reads CH4 on same unit | ESP-IDF documents `adc_oneshot_read()` as thread-safe; confirmed acceptable. Different channels, no shared state. |
| 5 | **Button hold-to-move UX:** Is hold-to-move the correct behavior? | **Resolved — hold-to-move confirmed. `read()` used for UP/DOWN, `read_edge()` for presets.** |
| 6 | **Calibration frequency:** Once vs. per-boot | Once (NVS offset) is sufficient. ToF absolute. Recalib available by clearing `h_calib` key. |
| 7 | **Motor RPM vs. desk travel speed:** What is actual mm/s? | Measure on hardware. Used to tune `GOTO_TIMEOUT_MS` and confirm 100ms poll is adequate. |
| 8 | **LVGL version:** `^8.3` — confirm 8.3 API used throughout | Lock to `8.3.x` explicitly. Do not upgrade to LVGL 9 (breaking API). |

---

## 18. Phase 1 Pre-work: VL53L0X Integration

### Status: Driver Is Already ESP-IDF Native

The existing `VL53L0X/VL53L0X.cpp` has **already been ported** to ESP-IDF. It includes `driver/i2c_master.h`, takes an `i2c_master_dev_handle_t` in its constructor, and all I/O uses `i2c_master_transmit` / `i2c_master_transmit_receive`. No Arduino dependencies exist.

The Phase 1 work is therefore **integration** — wiring the existing driver into `HeightSensor` and verifying it functions correctly on hardware — not a rewrite.

### What `height_sensor.cpp` Must Do

`HeightSensor::init()` is responsible for creating the I2C bus and device handles, then passing the device handle to the `VL53L0X` constructor:

```cpp
// height_sensor.cpp
#include "height_sensor.hpp"
#include "desk_config.h"
#include "VL53L0X/VL53L0X.h"
#include "driver/i2c_master.h"

static i2c_master_bus_handle_t s_bus_handle = nullptr;
static i2c_master_dev_handle_t s_dev_handle = nullptr;
static VL53L0X*                s_sensor     = nullptr;

bool HeightSensor::init() {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = I2C_PORT,
        .sda_io_num        = PIN_I2C_SDA,
        .scl_io_num        = PIN_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = false },
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus_handle) != ESP_OK) return false;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = VL53L0X_ADDR,
        .scl_speed_hz    = I2C_FREQ_HZ,
    };
    if (i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &s_dev_handle) != ESP_OK) return false;

    s_sensor = new VL53L0X(s_dev_handle);
    s_sensor->setTimeout(500);
    return s_sensor->init();  // Returns false if sensor not found at 0x29
}

int HeightSensor::read_mm() {
    if (!s_sensor) return -1;
    uint16_t raw = s_sensor->readRangeSingleMillimeters();
    last_valid_ = !s_sensor->timeoutOccurred() && raw != 65535;
    return last_valid_ ? static_cast<int>(raw) : -1;
}
```

### Height Calculation

The sensor is mounted pointing **downward**, measuring distance to the floor. Raw sensor output is therefore **inversely proportional** to desk height. During calibration, when the limit switch fires:

```
calib_offset_mm = sensor.read_mm()   // Distance to floor at lowest position
```

At any subsequent point:
```
height_mm = calib_offset_mm - sensor.read_mm()
```

This gives height relative to the lowest mechanical position (limit switch point).

### `CMakeLists.txt`

No changes needed. `driver` in `REQUIRES` already pulls in `i2c_master` under ESP-IDF v5.x.

### Verification Steps

1. `idf.py build` succeeds.
2. `height_sensor.init()` returns `true` — sensor acknowledged at 0x29.
3. `height_sensor.read_mm()` returns a plausible raw distance value.
4. Moving the desk manually by ~100mm produces a corresponding ~100mm change in `read_mm()` output.
5. Repeated reads at a fixed position are stable (±5mm jitter is acceptable).
6. Sensor survives 1000 consecutive reads without I2C timeout or bus lockup (`timeoutOccurred()` never set).
