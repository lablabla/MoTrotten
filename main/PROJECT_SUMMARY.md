# Project: Stand-IDF (Motorized Ikea Trotten)

## 1. Project Goal
Convert a manual **IKEA Trotten** standing desk into a motorized smart desk using an **ESP32** and a high-torque DC motor. The system replaces the manual hand crank with a motor coupled to the existing hex rod mechanism.

## 2. Hardware Architecture
* **MCU:** ESP32-WROOM-32.
* **Motor:** 24V DC Worm Gear Motor (Model 5840-31ZY, ~160-260 RPM).
    * *Note:* Powered by a **29V** supply. Software must cap PWM duty cycle to simulate ~24V.
* **Motor Driver:** BTS7960 High-Current H-Bridge (Logic: 3.3V, Power: 24V+).
* **Sensors:**
    * **Position:** VL53L0X Time-of-Flight sensor (I2C) mounted facing the floor.
    * **Safety/Current:** INA219 Current Sensor (I2C) monitoring motor load.
* **Power:** 29V DC Supply (from a recliner/desk system) $\rightarrow$ Motor Driver. Step-down Buck Converter (LM2596) $\rightarrow$ 5V for ESP32.

## 4. Key Logic & State Management
* **Concurrency:** FreeRTOS with Dual Core usage.
    * **Task A (Sensors):** High-priority producer. Polls I2C sensors (VL53L0X, INA219) every 50ms.
    * **Task B (Control):** Consumer. Handles button inputs, logic, and safety checks every 20ms.
* **State Synchronization:** `std::atomic` variables used for thread-safe communication between tasks.
    * `std::atomic<uint16_t> g_current_height`
    * `std::atomic<float> g_current_draw_ma`
    * `std::atomic<bool> g_is_moving`
* **Safety Features:**
    * **Collision Detection:** Immediate emergency stop if `g_current_draw_ma` exceeds threshold (e.g., >3.5A).
    * **Soft Limits:** Motor forbidden from moving if height < `MIN_LIMIT` or > `MAX_LIMIT`.
    * **Voltage Compensation:** PWM `MAX_DUTY` capped at ~85% to safely run 24V motor on 29V supply.
    * **Soft Start:** PWM ramping to prevent mechanical jerking.

## 5. Next Implementation Steps for Agent
1.  Implement `vl53l0x` read logic using the C++ `espp` component API.
2.  Implement `ina219` read logic using the C `esp-idf-lib` API.
3.  Implement NVS logic to save/load "Sit" and "Stand" height presets on button long-press.
4.  Refine PID or simple hysteresis loop for "Go to Preset" functionality.