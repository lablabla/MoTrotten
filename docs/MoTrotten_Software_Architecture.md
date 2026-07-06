This software architecture snippet is **not completely up to date**. It still reflects some of the legacy parameters from before we optimized the hardware for the single 5840-31ZY motor and updated the resistor values on your KiCad boards.

Here are the specific discrepancies you need to address:

### 1. The Analog Button Ladder Math is Outdated
In Section 3, your example software debounce code `if (val > 1500 && val < 1800) { button = 4; }` is based on the old resistor values. We changed the resistor ladder on your UI Board to use 0Ω, 1KΩ, 2.2KΩ, and 4.7KΩ against a 10KΩ pull-up on the Main Board. 

Because the resistance values changed, the voltage (and therefore the ADC values) will be much lower. Using a 12-bit ADC (values 0-4095) with a 3.3V reference:
*   **Button 4 (Preset 2 - 4.7kΩ):** Yields ~1.05V. Expected ADC value is roughly **1309**.
*   **Button 3 (Preset 1 - 2.2kΩ):** Yields ~0.59V. Expected ADC value is roughly **738**.
*   **Button 2 (Down - 1kΩ):** Yields ~0.30V. Expected ADC value is roughly **372**.
*   **Button 1 (Up - 0Ω):** Yields 0V. Expected ADC value is roughly **0**.

Your software ranges need to be shifted down to match these new hardware targets.

### 2. Missing Current Sense Configuration
Section 3 is currently missing the software logic for the motor's current sensing. Because we upgraded the BTS7960's physical drain resistors (`R10` and `R11`) to **4.7kΩ** on the Main Board to accommodate the worm gear motor's lower 4.4A stall current, the generated voltage will max out around ~2.4V. The documentation should explicitly state that keeping `ADC_ATTEN_DB_12` is the correct setting to read this 2.4V signal.

### 3. Motor PWM Context
In Section 2, the text states "Separate channels assigned for Right PWM and Left PWM". While this is technically correct because your single BTS7960 driver requires an `EN_R`/`PWM_R` and an `EN_L`/`PWM_L` signal to drive your single motor forwards and backwards, the legacy wording implies the old two-motor design. It is better to clarify that these control the two sides of a *single* half-bridge topology.

---

### Updated Markdown Suggestion
Here is the corrected markdown you can copy and paste to keep your software architecture document perfectly aligned with your final PCB design:

```markdown
# Project MoTrotten: ESP-IDF Software Configuration

## 1. Display Driver Configuration (ST7789)
The project utilizes the native `esp_lcd` component. 
* **Resolution Quirk:** The physical screen is 240x240, but the ST7789 driver defaults to a 240x320 memory map.
* **Solution:** Use `esp_lcd_panel_st7789_vendor_config_t` to force the correct geometry and prevent rendering offset "squares".
* **Color Format:** The display expects RGB565 (16-bit). To fix color swapping (e.g., Red rendering as Blue), configure `.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB` (or `BGR` depending on the specific batch) and ensure byte-swapping is handled either in software memory allocation or via SPI flags.

## 2. Motor PWM (LEDC)
The ESP32's LED Control (LEDC) peripheral is used for motor speed control.
* **Timer Config:** `LEDC_LOW_SPEED_MODE`, 10-bit resolution (values 0-1023), operating at a frequency of `5000 Hz` (5 kHz) to avoid audible motor whine while remaining within the BTS7960's switching capabilities.
* **Channel Config:** Separate channels are assigned for `PWM_R` and `PWM_L` to drive the single BTS7960 motor driver. Direction is handled by the Enable pins (BTS7960 uses a half-bridge topology per side; driving one side while enabling both dictates direction).

## 3. ADC Reading (Sensors, Buttons & Current)
Uses the `esp_adc/adc_oneshot.h` driver.
* **Configuration:** `ADC_UNIT_1`, `ADC_BITWIDTH_DEFAULT` (12-bit, giving values 0-4095).
* **Attenuation:** `ADC_ATTEN_DB_12` is required to read voltages up to the ESP32's maximum ~3.3V range. This attenuation perfectly captures the analog buttons and the BTS7960 current sense limits (which scales up to ~2.4V at a 4.4A stall thanks to the 4.7kΩ hardware drain resistors).
* **Software Debouncing:** The analog button ladder requires software ranges to determine button presses. Based on the 10KΩ pull-up and scaled pull-downs, the code should target these approximate ranges:
    * `if (val < 100) { button = 1; } // 0Ω`
    * `if (val > 250 && val < 500) { button = 2; } // 1KΩ`
    * `if (val > 600 && val < 900) { button = 3; } // 2.2KΩ`
    * `if (val > 1150 && val < 1450) { button = 4; } // 4.7KΩ`
```