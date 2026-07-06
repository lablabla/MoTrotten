# MoTrotten — Software Design: Current State
**Branch:** `claude_impl` · **Date:** 2026-06-03

---

## 1. System Overview

MoTrotten is a motorized standing desk controller built on an **ESP32-S3 (N16R8)**. It drives a 24V BTS7960 H-bridge, reads desk height with a VL53L0X ToF sensor, accepts input from a 4-button resistor ladder, and shows state on an ST7789 320×240 IPS display. Everything runs under **ESP-IDF v5.5 / FreeRTOS**.

---

## 2. Hardware Summary

| Peripheral | Interface | GPIOs |
|---|---|---|
| ST7789 display (320×240) | SPI2, 40 MHz | MOSI=11, CLK=12, CS=10, DC=13, RST=14 |
| BTS7960 H-bridge (UP) | LEDC CH0, GPIO | PWM=15, EN=17, IS=GPIO1/ADC1_CH0 |
| BTS7960 H-bridge (DOWN) | LEDC CH1, GPIO | PWM=16, EN=18, IS=GPIO2/ADC1_CH1 |
| Button ladder (4 buttons) | ADC1_CH4 | GPIO5 |
| VL53L0X ToF sensor | I2C0, 400 kHz | SDA=8, SCL=9 |
| Limit switch | GPIO (active-low) | GPIO4, 10kΩ ext. pull-up |

**Motor PWM:** LEDC timer 0, `LEDC_LOW_SPEED_MODE`, 5 kHz, 10-bit (duty 0–1023).  
**Current sense resistors:** 2.2 kΩ (substituted from design 4.7 kΩ). Stall threshold: 1300 raw ≈ 4.0 A.

---

## 3. RTOS Task Architecture

```
Core 0                                Core 1
──────────────────────────────────    ───────────────────────────────
motor_mon_task  prio 6  stack 2048    lvgl_task  prio 4  stack 8192
app_task        prio 5  stack 6144
```

### 3.1 `motor_mon_task` (Core 0, prio 6)
- Created by `MotorDriver::init()`, loops every **50 ms**.
- Exclusively owns the motor ramp state machine.
- Reads current-sense ADC (`IS_R` or `IS_L` depending on direction).
- Calls `emergency_stop()` + stall callback when `raw > MOTOR_STALL_THRESHOLD_RAW` for 5 consecutive samples (= 250 ms).

### 3.2 `app_task` (Core 0, prio 5)
- Created by `app_main()` after calibration completes.
- Loops every **50 ms** (plus ~30 ms for sensor blocking read → effective ~80 ms/cycle).
- Reads sensor, polls buttons, drives the application state machine, posts UI callbacks.

### 3.3 `lvgl_task` (Core 1, prio 4)
- Created first (before `app_task`) so the display is ready.
- Calls `DisplayManager::init()`, `UIManager::init()`, runs startup animation, then notifies `app_main()` via `xTaskNotifyGive`.
- Loops: `DisplayManager::tick()` → `lv_task_handler()` → `vTaskDelay(5 ms)`.

### 3.4 `app_main()` (becomes idle after startup)
- Runs on Core 0 (the default IDF task) while orchestrating boot.
- After launching `app_task` it calls `vTaskDelete(nullptr)` and exits.

### 3.5 Synchronisation

| Mechanism | Used for |
|---|---|
| `portMUX_TYPE mux_` (spinlock) | Atomic state transitions in MotorDriver from any task/ISR |
| `SemaphoreHandle_t flush_sem_` | DMA-safe LVGL flush: ISR gives, `flush_cb` waits before releasing buffer |
| `SemaphoreHandle_t g_state.mutex` | Protecting preset writes during hold-to-save |
| `xTaskNotifyGive / ulTaskNotifyTake` | `lvgl_task` signals `app_main` that startup animation is done |
| `lv_async_call()` | `app_task` posts zero-alloc UI callbacks to `lvgl_task` |
| `volatile` fields in `DeskState` | `stall_detected`, `current_height_mm`, `app_state`, `goto_target_mm` read across tasks without a mutex |

> **Note on `volatile`:** On ARM Cortex-M4, aligned 32-bit reads/writes are atomic, so using `volatile int` for `current_height_mm` (read by lvgl callbacks, written by app_task) is safe in practice. `stall_detected` is written by `motor_mon_task` and read by `app_task`.

### 3.6 No queues
There are no FreeRTOS queues in the current design. Communication between tasks happens through shared `DeskState` (guarded by `volatile` + `mutex` where needed) and `lv_async_call`.

---

## 4. Initialisation Sequence

```
app_main()
  │
  ├─ 1. NvsManager::init()          — nvs_flash_init(), load presets
  ├─ 2. MotorDriver::init()         — LEDC, GPIO EN, ADC unit, spawn motor_mon_task
  ├─ 3. ButtonReader::init(adc)     — share ADC unit, configure ADC1_CH4
  ├─ 4. HeightSensor::init()        — I2C bus + device, VL53L0X::init()
  ├─ 5. xSemaphoreCreateMutex()     — g_state.mutex
  ├─ 6. s_motor.set_stall_callback  — lambda: sets g_state.stall_detected = true
  │
  ├─ 7. xTaskCreatePinnedToCore(lvgl_task, Core 1)
  │       └─ DisplayManager::init() — SPI2, ST7789, LVGL, DMA flush semaphore, 1ms tick timer
  │       └─ UIManager::init()      — create LVGL objects (labels, arrows, overlay)
  │       └─ show_startup()         — "MoTrotten" letter-by-letter fade-in animation
  │           └─ on complete: xTaskNotifyGive(app_main)
  │
  ├─ 8. ulTaskNotifyTake()          — wait for startup animation
  │
  ├─ 9. Calibration (if calib_offset == 0)
  │       └─ move_down() → poll limit switch → stop()
  │       └─ read_mm() → save calib_offset to NVS
  │
  ├─ 10. g_state.app_state = IDLE, lv_async_call(cb_show_idle)
  │
  ├─ 11. xTaskCreatePinnedToCore(app_task, Core 0, prio 5)
  │
  └─ vTaskDelete(nullptr)
```

Any init failure in steps 1–4 is **fatal**: the firmware halts with `while(true) vTaskDelay(portMAX_DELAY)`.

---

## 5. Application State Machine

```
                  ┌──────────┐
                  │ STARTUP  │ (lvgl_task runs animation)
                  └────┬─────┘
                       │ animation complete (TaskNotify)
                  ┌────▼─────┐
                  │CALIBRATING│ (if calib_offset==0, first boot)
                  └────┬─────┘
                       │ limit switch triggered + offset saved
                  ┌────▼─────┐◄─────────────────────────────┐
          ┌───────┤   IDLE   ├───────┐                       │
          │       └──────────┘       │                       │
          │ hold UP/DOWN             │ tap PRESET1/PRESET2   │ 2s elapsed
          ▼                          ▼                        │
    ┌──────────┐              ┌─────────────┐         ┌──────┴──────┐
    │MOVING_UP │              │ GOTO_PRESET │         │   STALLED   │
    │MOVING_DOWN│             └──────┬──────┘         └─────────────┘
    └─────┬────┘                     │                      ▲
          │ button released          │ reached target       │
          │ or soft limit            │ or cancel            │ stall_detected
          └──────────────────────────┴──────────────────────┘
```

### State descriptions

| State | Motor | Display |
|---|---|---|
| `STARTUP` | Off | "MoTrotten" letter animation |
| `CALIBRATING` | DOWN until limit switch | "Calibrating..." overlay |
| `IDLE` | Off | Height in mm (cyan, 48pt) |
| `MOVING_UP` | UP | Height + animated ▲ arrow trail |
| `MOVING_DOWN` | DOWN | Height + animated ▼ arrow trail |
| `GOTO_PRESET` | UP or DOWN | Height + "→ NNN mm" overlay |
| `STALLED` | Off (emergency stopped) | Red background + "BLOCKED", auto-clears 2s |

---

## 6. Button Functions

### Physical layout (resistor ladder, GPIO5, ADC1_CH4)

| Button | Series R | ADC raw | Function |
|---|---|---|---|
| SW1 (UP) | 0 Ω | < 100 | Hold to move desk up |
| SW2 (DOWN) | 1 kΩ | 250–500 | Hold to move desk down |
| SW3 (PRESET1) | 2.2 kΩ | 600–900 | Tap → go to sit height. Hold 3s → save current height as sit |
| SW4 (PRESET2) | 4.7 kΩ | 1150–1450 | Tap → go to stand height. Hold 3s → save current height as stand |

### Detailed button behaviour

**UP (hold-to-move):**
- Held → `MOVING_UP` state, motor ramps up, height updates every cycle.
- Released → soft stop (motor ramps down), return to `IDLE`.
- Held at `DESK_MAX_HEIGHT_MM` (1200 mm) → motor stopped, `IDLE`.
- During `GOTO_PRESET`: cancels goto immediately.

**DOWN (hold-to-move):**
- Same as UP but opposite direction.
- Soft limit: `DESK_MIN_HEIGHT_MM` (650 mm).

**PRESET1 / PRESET2 (tap or hold):**
- Only active from `IDLE`.
- **Tap** (< 3s): start `GOTO_PRESET` to saved sit/stand height (default 730/1100 mm).
  - Motor runs toward target. Stops when `|height - target| ≤ 5 mm`.
  - Timeout: 60 s.
  - Any button press cancels.
- **Hold ≥ 3s**: saves current height as the preset. Shows "Saved!" overlay for 1.5s.

### `ButtonReader` internals
- `poll()`: single ADC read → `decode()` → 50 ms debounce state machine.
- `state()`: debounced held state (used for UP/DOWN continuous move).
- `edge()`: rising edge only (used for PRESET1/2 one-shot detection).
- Debounce: candidate must be stable for `BTN_DEBOUNCE_MS` (50 ms) before `last_stable_` updates.

---

## 7. Motor Driver

### LEDC configuration
- Timer 0, `LEDC_LOW_SPEED_MODE`, 5 kHz, 10-bit resolution (0–1023).
- CH0 (`PWM_R`, GPIO15) = UP half-bridge.
- CH1 (`PWM_L`, GPIO16) = DOWN half-bridge.

### EN pin behaviour *(fixed — verified in hw_test)*
Both `EN_R` (GPIO17) and `EN_L` (GPIO18) are driven **HIGH** simultaneously during movement. Direction is controlled by which PWM channel carries duty > 0; the idle channel stays at duty=0 (low-side switch closed → GND return path).

With EN_idle=0 the output floats, M+ and M- equalise at 24V, no current flows. This was the initial bug found during hardware testing.

### Ramp state machine (`motor_mon_task`, 50 ms loop)

```
STOPPED
  │ move_up() / move_down()
  ▼
RAMPING_UP / RAMPING_DOWN
  │  duty = elapsed/RAMP_MS × MAX_DUTY (linear, 500 ms)
  ▼
RUNNING_UP / RUNNING_DOWN
  │  after MOTOR_INRUSH_IGNORE_MS (500 ms): check IS ADC
  │  if raw > 1300 for 5 × 50 ms → emergency_stop() + stall callback
  │
  │ stop() called
  ▼
STOPPING_FROM_UP / STOPPING_FROM_DOWN
  │  duty ramps 1023→0 over 500 ms
  │  then: both EN LOW
  ▼
STOPPED
```

`emergency_stop()` bypasses ramp: sets both PWM=0, both EN=0 atomically.

### Current sense
- IS_R/IS_L (GPIO1/GPIO2) via ADC1_CH0/CH1.
- Formula: I = (raw × 3.3 / 4095) / 2200 × 8500
- Stall threshold: 1300 raw ≈ 4.0A (needs empirical tuning — see hw_test test 5).

---

## 8. Height Sensor

- VL53L0X at I2C address 0x29, 400 kHz. External 10 kΩ pull-ups on SDA/SCL.
- `read_mm()` calls `readRangeSingleMillimeters()` — **blocks ~30 ms**.
- Calibration formula: `height = calib_offset - raw_distance` (sensor points toward ceiling or floor — depends on mounting orientation; verified during calibration).
- Invalid reads (timeout / 65535) return -1 and are skipped without updating `current_height_mm`.

### Calibration
- On first boot (`calib_offset == 0`): desk drives DOWN until the limit switch (GPIO4) pulls low.
- Motor stops, sensor reads raw distance → saved as `calib_offset` in NVS.
- Timeout guard: 30 s (`CALIB_TIMEOUT_MS`). If limit switch never triggers: `emergency_stop()` + halt.

---

## 9. Display & UI

### `DisplayManager`
- SPI2, 40 MHz, ST7789 320×240.
- LVGL 8.x, double buffer: 2× (320×40×2 bytes) = 51.2 KB from DMA heap.
- **Flush semaphore:** `notify_flush_ready` (ISR) gives semaphore; `flush_cb` waits before calling `lv_disp_flush_ready` — prevents LVGL touching a buffer still in DMA.
- 1 ms tick via `esp_timer`.

### `UIManager` LVGL objects (all on default screen)
| Object | Type | Description |
|---|---|---|
| `height_label_` | `lv_label` | Large cyan 48pt number (current height in mm) |
| `unit_label_` | `lv_label` | Small white 24pt "mm" below height |
| `overlay_label_` | `lv_label` | Shared: goto target, stall, calib, saved text |
| `arrow_container_` | `lv_obj` | Parent for 3-arrow trail animation |
| `main_arrow_lbl_` | `lv_label` | Lead arrow (100% opacity) |
| `trail_arrow_1_lbl_` | `lv_label` | Trail 1 (60% opacity) |
| `trail_arrow_2_lbl_` | `lv_label` | Trail 2 (30% opacity) |
| `startup_container_` | `lv_obj` | Flex row; destroyed after startup |

### Arrow animation
- `lv_anim` on `arrow_container_` Y position: ±20 px over 1000 ms, infinite repeat.
- UP: main arrow at top, trail below (–25/0/+25 px offsets).
- DOWN: reversed trail order.
- Symbol swaps in-place if direction changes while already animating.

### `lv_async_call` pattern
All UI calls from `app_task` go through static callbacks that read `&g_state`:
```
app_task (Core 0) → lv_async_call(cb_show_idle, &g_state)
                        ↓ (queued to LVGL event loop)
lvgl_task (Core 1) → cb_show_idle() → s_ui.show_idle(g_state.current_height_mm)
```
Zero heap allocation per call. Safe because callbacks reference the static `g_state` struct.

### ⚠ Known display issue (unresolved)
Colors are rendering incorrectly (Red→Yellow, Green→Teal, Blue→Magenta). Root cause: ESP32 DMA sends `uint16_t` LSB-first; the ST7789 expects MSB-first. Fix: byte-swap each pixel (`__builtin_bswap16`) before sending. Diagnostic test `d` in `hw_test.cpp` will identify the correct combination. `display_manager.cpp` needs updating once confirmed.

Arrow container is hardcoded at 240×240 px (line 33, `ui_manager.cpp`). With a 320×240 display this may clip the right side of the arrow area, as the container is not full-width.

---

## 10. NVS Persistence

Namespace: `"desk_mem"` (key length ≤ 15 chars)

| Key | Type | Default | Description |
|---|---|---|---|
| `"h_sit"` | i32 | 730 | Sit preset height (mm) |
| `"h_stand"` | i32 | 1100 | Stand preset height (mm) |
| `"h_calib"` | i32 | 0 | Calibration offset (0 = uncalibrated) |

On `ESP_ERR_NVS_NO_FREE_PAGES`: erase + reinitialise (resets all to defaults).

---

## 11. Issues & Improvements

The following were identified by comparing the main firmware against `hw_test.cpp` (where we have confirmed working hardware behaviour) and static analysis.

### 🔴 Critical

**C1 — Display colors wrong (unresolved)**
`display_manager.cpp` sends `lv_color_t` values without byte-swapping. Hardware test confirms the display needs `__builtin_bswap16()` applied to each pixel. Until this is fixed, the UI is unreadable with wrong colours.
*Fix: apply byte swap in `flush_cb`, OR set `LV_COLOR_16_SWAP 1` in `lv_conf.h` so LVGL swaps at source.*

**C2 — `esp_task_wdt.h` included but never used (app_main.cpp:6)**
This is a dead include. It was probably left from an earlier design. Remove it.

**C3 — Overshoot correction calls `move_down()` while motor may still be stopping**
In `app_task` GOTO_PRESET branch:
```cpp
s_motor.stop();      // non-blocking: sets STOPPING_FROM_UP
s_motor.move_up();   // immediately called — but motor is still decelerating
```
`move_up()` will proceed since state is `STOPPING_FROM_UP` (not `RUNNING_UP`), starting a new ramp while the stop ramp is still active. `monitor_loop` will then see `RAMPING_UP` state on the next tick and start ramping up — this is undefined because `set_duty_raw()` is being called with the wrong direction channel.
*Fix: add a `wait_stopped(timeout_ms)` helper or check `is_running()` before issuing the reversal.*

### 🟡 Important

**I1 — app_task blocks up to 3s during hold-to-save**
```cpp
while (true) {
    s_buttons.poll();
    if (s_buttons.state() != edge) break;
    if (elapsed >= BTN_HOLD_SAVE_MS) { ... break; }
    vTaskDelay(pdMS_TO_TICKS(20));
}
```
During this 3-second window: no height reading, no stall detection, no soft-limit enforcement. If motor_mon_task detects a stall and sets `stall_detected = true`, app_task won't process it until the loop exits.
Since hold-to-save only runs from `IDLE` state (motor should be off), the motor-side risk is low. However, stall recovery is delayed.
*Fix: instead of a blocking loop, implement as a hold-start timestamp in the main 50ms cycle.*

**I2 — Stall threshold not empirically validated**
`MOTOR_STALL_THRESHOLD_RAW = 1300` (~4.0 A) was calculated from the 2.2 kΩ resistor change but not measured on the actual motor. Run hw_test **test 5** (integrated IS + motor run) to log real running current and set an appropriate threshold.

**I3 — Arrow container width hardcoded to 240 px**
`ui_manager.cpp:33`: `lv_obj_set_size(arrow_container_, 240, 240)`. Display is 320×240. Arrow is positioned at `offset_x = 80` from centre, placing the rightmost arrow at ~200 px from left — within the 240 px container. While the arrows themselves are likely visible, the container does not span the full display width. Not a functional bug but inconsistent.
*Fix: use `DISP_WIDTH` / `DISP_HEIGHT` constants.*

**I4 — sensor read blocks for ~30ms inside the 50ms app_task cycle**
`read_mm()` calls `readRangeSingleMillimeters()` (blocking, ~30 ms). Combined with `vTaskDelay(50ms)`, each app_task cycle takes ~80 ms. This means:
- Button response latency: up to 80 ms
- Height display update rate: ~12.5 Hz during movement

Acceptable but worth noting. For snappier response, switch to VL53L0X continuous ranging mode (`startContinuous()`) with `readRangeContinuousMillimeters()`, which returns the last measurement immediately.

**I5 — GOTO_PRESET has no active stall handling**
During `GOTO_PRESET`, the stall check at the top of `app_task` (step 2) handles `g_state.stall_detected`. This is correct. However, the goto will keep issuing `move_up()/move_down()` calls in the overshoot correction branch even after `emergency_stop()` is called (because the state machine checks `done` flag separately). The stall at step 2 sets `app_state = STALLED` which jumps past the GOTO branch on the *next* cycle, but the current cycle still continues into step 5. This is a one-cycle race — low risk in practice.

### 🟢 Minor

**M1 — Calibration height formula needs physical verification**
`height = calib_offset - raw_distance` (height_sensor.cpp). This makes sense if the sensor is pointing toward a fixed target (ceiling) and `calib_offset` is the distance at the lowest position. Verify on hardware that the height reading increases as the desk rises.

**M2 — NVS save not called after successful calibration when running from fully reset device**
`save_calib_offset()` is called, but the presets (sit/stand defaults) are not saved to NVS on first boot. If NVS is later erased, calibration runs again but presets would revert to defaults (730/1100). This is the intended behaviour but worth documenting explicitly.

**M3 — No watchdog**
`esp_task_wdt.h` is included (unused, see C2) but no task is registered with the watchdog. If `app_task` or `lvgl_task` hangs, the device will not reset. Add WDT registration if production stability is required.

**M4 — `CONFIG_LV_COLOR_16_SWAP` workaround in ui_manager.hpp**
```cpp
#ifdef CONFIG_LV_COLOR_16_SWAP
    lv_color_t cyan_ = lv_palette_main(LV_PALETTE_RED);
#else
    lv_color_t cyan_ = lv_palette_main(LV_PALETTE_CYAN);
#endif
```
This suggests byte-swap was partially considered. The swap compensates at the palette level rather than at the driver level. Once C1 is resolved properly (LV_COLOR_16_SWAP or flush_cb swap), this ifdef can be simplified.

---

## 12. Summary of Changes Needed (Priority Order)

| # | Issue | File | Effort |
|---|---|---|---|
| C1 | Fix display colours (byte swap) | `display_manager.cpp` / `lv_conf.h` | Small |
| C2 | Remove unused `esp_task_wdt.h` | `app_main.cpp` | Trivial |
| C3 | Fix overshoot correction race in GOTO_PRESET | `app_main.cpp` | Small |
| I1 | Non-blocking hold-to-save | `app_main.cpp` | Medium |
| I2 | Validate stall threshold via hw_test 5 | `desk_config.h` | Test only |
| I3 | Fix arrow container to use DISP_WIDTH/HEIGHT | `ui_manager.cpp` | Trivial |
| I4 | Switch VL53L0X to continuous mode | `height_sensor.cpp` | Small |
| M3 | Add task watchdog | `app_main.cpp` | Small |
