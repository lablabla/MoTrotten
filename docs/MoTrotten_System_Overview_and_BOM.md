Here is the updated `MoTrotten_System_Overview_and_BOM.md` file. It has been revised to reflect the shift to a single 5840-31ZY worm gear motor, the updated 50cm unshielded JST-XH cable, and all of the electrical improvements we've made to the passive components (level shifter, SPI termination, updated current sense drains, and the corrected capacitors) based on your KiCad BOMs.

```markdown
# Project MoTrotten: System Overview and Bill of Materials (BOM)

## 1. Project Overview
Project MoTrotten is a custom ESP32-based controller for a motorized standing desk. It transitions the desk from standard controls to a smart, custom UI featuring an ST7789 2" TFT display, a custom 4-button analog control pad, Time-of-Flight (ToF) height sensing, and precise motor current monitoring for safety and limit detection. The software is written in C using the native ESP-IDF framework.

## 2. System Architecture
The system follows a "Smart Brain, Dumb UI" architecture:
* **Main Control Board:** Located under the desk, housing the ESP32-S3, power distribution, and a single BTS7960 high-current motor driver.
* **UI Daughter Board:** A 3D-printed, slanted case (130mm x 60mm x 15mm, 30° tilt) mounted to the desk edge. It contains no microcontrollers. It houses the TFT display and 4 tactile buttons, sending all raw signals back to the main board via a single 50cm 8-core 22AWG unshielded cable to minimize wiring, connected with JST-XH connectors on both ends.

## 3. Bill of Materials (BOM)
### Microcontroller & Power
* **ESP32-S3 Development Board:** (e.g., N16R8). Needs 5V supply for logic and 3.3V for internal GPIOs.
* **Main Power Supply:** 24V DC Power Supply (Minimum 5A required to comfortably handle the ~4.4A max stall current of the motor).
* **Logic Power Converter:** 24V to 5V Step-Down (Buck) Converter to power the ESP32 and motor driver logic.

### Actuation & Logic Control
* **Motor Driver:** 1x BTS7960 43A High-Power Motor Driver Module.
* **Motor:** 1x 5840-31ZY Worm Gear Motor (24V at 260 RPM) driving an existing manual shaft.
* **Logic Level Shifter:** 1x 74AHCT125N (Quad buffer to safely step up the 3.3V ESP32 PWM/EN signals to strong 5V logic for the BTS7960).

### UI Board Components
* **Display:** 2.0" IPS TFT LCD Module (ST7789 Driver, SPI interface, 240x240 resolution, RGB565 color format).
* **Buttons:** 4x Tactile Push Buttons.
* **Cable:** 50cm 8-Core 22AWG non-shielded wire with JST-XH connectors.

### Sensors
* **Height Sensor:** I2C Time-of-Flight (ToF) Sensor (Default Address: `0x29`).
* **Limit Switch:** Standard mechanical limit switch for hard-stop calibration.

### Passive Components (Resistors & Capacitors)
* **Current Sense Drains:** 2x 4.7kΩ Resistors (Scaled up to maximize ESP32 ADC resolution for the worm gear motor's lower current profile).
* **Analog Button Ladder:** 1x 10kΩ (Main Board Pull-up), 1x 4.7kΩ (Btn 4), 1x 2.2kΩ (Btn 3), 1x 1kΩ (Btn 2), 0Ω/Direct (Btn 1).
* **Limit Switch Pull-up:** 1x 10kΩ Resistor.
* **ADC Protection/Filtering:** 4x 1kΩ Resistors (Series), 4x 0.1µF Capacitors (GND) (Hardware debouncing/filtering for the button ladder, limit switch, and both current sense lines).
* **SPI Signal Integrity:** 4x 33Ω Resistors (Source termination for display data/clock lines to prevent ringing).
* **Power Filtering / Bulk Capacitance:** 1x 50V 470µF Electrolytic Capacitor (Placed at the BTS7960 24V input for motor switching), 1x 47µF Capacitor (For 5V logic line stabilization).
```