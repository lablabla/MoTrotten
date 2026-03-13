# Project MoTrotten: Hardware Wiring and Pinout Reference

## 1. ESP32-S3 Pin Mapping
| Component / Function | ESP32-S3 Pin | Notes |
| :--- | :--- | :--- |
| **Display SPI (SPI2_HOST)** | | |
| DISP_MOSI | GPIO 11 | Data to screen (Routed through 33Ω series termination resistor) |
| DISP_CLK | GPIO 12 | SPI Clock (Routed through 33Ω series termination resistor) |
| DISP_CS | GPIO 10 | Chip Select (Routed through 33Ω series termination resistor) |
| DISP_DC | GPIO 13 | Data/Command Toggle (Routed through 33Ω series termination resistor) |
| DISP_RST | GPIO 14 | Hardware Reset |
| **Motor (Single BTS7960)** | | |
| PWM_R | GPIO 15 | LEDC Channel 0 (Speed) - Shifted to 5V via 74AHCT125 |
| PWM_L | GPIO 16 | LEDC Channel 1 (Speed) - Shifted to 5V via 74AHCT125 |
| EN_R | GPIO 17 | Enable High - Shifted to 5V via 74AHCT125 |
| EN_L | GPIO 18 | Enable High - Shifted to 5V via 74AHCT125 |
| IS_R | GPIO 1 | ADC1_CH0 (Current Sense Right) - Filtered via 1kΩ/0.1µF |
| IS_L | GPIO 2 | ADC1_CH1 (Current Sense Left) - Filtered via 1kΩ/0.1µF |
| **Sensors & Inputs** | | |
| I2C_SDA | GPIO 8 | ToF Sensor Data |
| I2C_SCL | GPIO 9 | ToF Sensor Clock |
| XSHUT | TBD | ToF Sensor Shutdown |
| LIMIT_SW | GPIO 4 | Hard-stop limit. 10kΩ external pull-up + 1kΩ/0.1µF hardware debounce |
| ADC_BTN | GPIO 5 | ADC1_CH4 (Analog Button Ladder) |

## 2. Analog Button Ladder Design (Straight RJ45 / JST-XH Cable)
To save wires, 4 buttons are read over a single ADC pin (`GPIO 5`) using a voltage divider network pulling down against a common pull-up located exclusively on the Main Board.

* **Main Board Side:** `3.3V` $\rightarrow$ `10kΩ Pull-up Resistor` (`R9`) $\rightarrow$ **ADC Signal Line**.
* **Main Board Filter:** **ADC Signal Line** $\rightarrow$ `1kΩ Series Resistor` (`R1`) $\rightarrow$ `GPIO 5`. Includes a `0.1µF` capacitor (`C2`) from `GPIO 5` to `GND` for hardware debouncing.
* **UI Board Side (Buttons to GND):**
    * **Button 1 (Up/SW1):** Connects ADC Line directly to `GND` (0Ω). ADC reads ~0V.
    * **Button 2 (Down/SW2):** Connects ADC Line to `GND` via `1kΩ` (`R1`). 
    * **Button 3 (Preset 1/SW3):** Connects ADC Line to `GND` via `2.2kΩ` (`R2`). 
    * **Button 4 (Preset 2/SW4):** Connects ADC Line to `GND` via `4.7kΩ` (`R3`). 

## 3. BTS7960 Motor Driver Nuances
* **Logic Voltage & Level Shifting:** The module's `VCC` pin powers the onboard 74HC244 octal buffer and **must be connected to 5V**. Because the ESP32 outputs 3.3V, all `PWM` and `EN` signals from the ESP32 must be routed through a **74AHCT125N level shifter** buffer. This cleanly steps the logic up to a strong 5V to prevent undervoltage lockouts or missed PWM pulses.
* **Current Sense (IS) Pins:** The `IS_R` and `IS_L` pins are **Current Sources** (ratio 1:8500), not voltage sources. 
    * **Crucial Wiring:** Because the 5840-31ZY worm gear motor has a low stall current (~4.4A), you must place **`4.7kΩ` drain resistors** (`R10`, `R11`) connecting both `IS` pins directly to `GND` to stretch the voltage into a readable range.
    * **Sensing:** Tap the voltage *above* these resistors, route it through `1kΩ` series protection resistors (`R7`, `R8`), bypass to `GND` with `0.1µF` capacitors (`C4`, `C5`), and finally feed it into the ESP32 ADC pins.
