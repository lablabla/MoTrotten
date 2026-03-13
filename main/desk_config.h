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
#define DISP_SPI_FREQ_HZ    (40 * 1000 * 1000)
#define DISP_WIDTH          240
#define DISP_HEIGHT         240

// ─── MOTOR ──────────────────────────────────────────────
#define PIN_MOTOR_PWM_R     GPIO_NUM_15   // LEDC CH0 — UP drive
#define PIN_MOTOR_PWM_L     GPIO_NUM_16   // LEDC CH1 — DOWN drive
#define PIN_MOTOR_EN_R      GPIO_NUM_17   // UP half-bridge enable
#define PIN_MOTOR_EN_L      GPIO_NUM_18   // DOWN half-bridge enable

#define MOTOR_LEDC_TIMER    LEDC_TIMER_0
#define MOTOR_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define MOTOR_LEDC_FREQ_HZ  5000
#define MOTOR_LEDC_RES      LEDC_TIMER_10_BIT  // 0–1023
#define MOTOR_LEDC_CH_R     LEDC_CHANNEL_0
#define MOTOR_LEDC_CH_L     LEDC_CHANNEL_1
#define MOTOR_MAX_DUTY      1023
#define MOTOR_RAMP_MS       500

// ─── CURRENT SENSE ──────────────────────────────────────
// Hardware: 4.7kΩ drain resistor on BTS7960 IS pins.
// At 4.4A stall: V_IS = (4.4 / 8500) * 4700 ≈ 2.43V
// ADC (12-bit, 3.3V ref): 2.43 / 3.3 * 4095 ≈ 3017 raw
#define PIN_MOTOR_IS_R      GPIO_NUM_1    // ADC1_CH0
#define PIN_MOTOR_IS_L      GPIO_NUM_2    // ADC1_CH1
#define MOTOR_IS_ADC_UNIT   ADC_UNIT_1
#define MOTOR_IS_CH_R       ADC_CHANNEL_0
#define MOTOR_IS_CH_L       ADC_CHANNEL_1
#define MOTOR_STALL_THRESHOLD_RAW  2800
#define MOTOR_STALL_CONFIRM_COUNT  5      // 5 × 50ms = 250ms
#define MOTOR_MON_TASK_MS          50
#define MOTOR_INRUSH_IGNORE_MS     500

// ─── I2C / VL53L0X ──────────────────────────────────────
#define PIN_I2C_SDA         GPIO_NUM_8
#define PIN_I2C_SCL         GPIO_NUM_9
#define I2C_PORT            I2C_NUM_0
#define I2C_FREQ_HZ         400000
#define VL53L0X_ADDR        0x29

// ─── LIMIT SWITCH ───────────────────────────────────────
#define PIN_LIMIT_SW        GPIO_NUM_4    // Active low, ext 10kΩ pull-up

// ─── ANALOG BUTTON LADDER ───────────────────────────────
// 10kΩ pull-up on main board.
// ADC_ATTEN_DB_12: 0–3.3V → 0–4095 (12-bit)
#define PIN_ADC_BTN         GPIO_NUM_5
#define BTN_ADC_CHANNEL     ADC_CHANNEL_4
#define BTN_NONE_MIN        1600
#define BTN_UP_MAX          100           // SW1: 0Ω
#define BTN_DOWN_MIN        250
#define BTN_DOWN_MAX        500           // SW2: 1kΩ
#define BTN_PRESET1_MIN     600
#define BTN_PRESET1_MAX     900           // SW3: 2.2kΩ
#define BTN_PRESET2_MIN     1150
#define BTN_PRESET2_MAX     1450          // SW4: 4.7kΩ
#define BTN_DEBOUNCE_MS     50
#define BTN_HOLD_SAVE_MS    3000

// ─── DESK LIMITS ────────────────────────────────────────
#define DESK_MIN_HEIGHT_MM      650
#define DESK_MAX_HEIGHT_MM      1200
#define DESK_GOTO_TOLERANCE_MM  5
#define DESK_DEFAULT_SIT_MM     730
#define DESK_DEFAULT_STAND_MM   1100
#define DESK_GOTO_TIMEOUT_MS    60000

// ─── NVS ────────────────────────────────────────────────
#define NVS_NAMESPACE       "desk_mem"
#define NVS_KEY_SIT         "h_sit"
#define NVS_KEY_STAND       "h_stand"
#define NVS_KEY_CALIB       "h_calib"

// ─── CALIBRATION ────────────────────────────────────────
#define CALIB_TIMEOUT_MS    30000

// ─── TASK CONFIG ────────────────────────────────────────
#define TASK_STACK_APP      6144
#define TASK_STACK_LVGL     8192
#define TASK_STACK_MOTOR_MON 2048
#define TASK_PRIO_APP       5
#define TASK_PRIO_LVGL      4
#define TASK_PRIO_MOTOR_MON 6
#define TASK_CORE_APP       0
#define TASK_CORE_LVGL      1
#define TASK_CORE_MOTOR_MON 0

#endif // DESK_CONFIG_H
