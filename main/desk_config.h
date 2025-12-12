#ifndef DESK_CONFIG_H
#define DESK_CONFIG_H

#include "driver/gpio.h"

// --- MOTOR SETTINGS ---
// Capping duty cycle at 850 (out of 1023) simulates ~24V 
// when using a 29V supply. (24/29 * 1023 ≈ 846)
#define MOTOR_MAX_DUTY      850  
#define MOTOR_PWM_FREQ_HZ   15000 // 15kHz is silent (above hearing range)
#define MOTOR_RAMP_STEP     15    // How fast to accelerate (Soft Start)

// --- PINS (Modify to match your wiring) ---
// Motor Driver (BTS7960)
#define PIN_MOTOR_PWM       GPIO_NUM_18
#define PIN_MOTOR_L_EN      GPIO_NUM_19 // Up Enable
#define PIN_MOTOR_R_EN      GPIO_NUM_21 // Down Enable

// I2C Bus (VL53L0X & INA219)
#define PIN_I2C_SDA         GPIO_NUM_23
#define PIN_I2C_SCL         GPIO_NUM_22

// UI Buttons
#define PIN_BTN_UP          GPIO_NUM_32
#define PIN_BTN_DOWN        GPIO_NUM_33
#define PIN_BTN_PRESET_1    GPIO_NUM_25 // Standing Height
#define PIN_BTN_PRESET_2    GPIO_NUM_26 // Sitting Height

// --- SAFETY & LIMITS ---
#define DESK_MIN_HEIGHT_MM  650   // Lowest physical height
#define DESK_MAX_HEIGHT_MM  1200  // Highest physical height
#define COLLISION_MA        3500  // 3.5 Amps (Tune this during testing!)

// --- MEMORY ---
#define NVS_NAMESPACE       "desk_mem"
#define NVS_KEY_SIT         "h_sit"
#define NVS_KEY_STAND       "h_stand"

#endif