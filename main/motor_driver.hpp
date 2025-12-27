#pragma once

#include "driver/mcpwm_prelude.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "desk_config.h"
#include "logger.hpp"
#include <functional>

// Hardware Assumption:
// PIN_MOTOR_R_PWM, PIN_MOTOR_L_PWM, PIN_MOTOR_R_EN, PIN_MOTOR_L_EN defined in desk_config.h
// NEW: PIN_MOTOR_R_IS (ADC Channel), PIN_MOTOR_L_IS (ADC Channel)
// Example: #define PIN_MOTOR_R_IS ADC_CHANNEL_6

class MotorDriver {
public:
    using StallCallback = std::function<void(bool is_stalled)>;

    MotorDriver();
    ~MotorDriver();

    void move_up();
    void move_down();
    void stop();

    // Register a function to be called when stall status changes
    // callback(true)  = Stalled
    // callback(false) = Stall Cleared / Ready
    void register_stall_callback(StallCallback cb);

private:
    void set_speed(float speed);
    void enable_driver(bool enable);
    
    // Background task to monitor current
    static void monitor_task_entry(void* arg);
    void monitor_task_loop();

    espp::Logger logger_;
    float current_speed_ = 0.0f;
    uint32_t period_ticks_ = 0;
    
    // State
    bool is_stalled_ = false;
    TickType_t movement_start_tick_ = 0;

    // MCPWM Handles
    mcpwm_timer_handle_t timer_ = NULL;
    mcpwm_oper_handle_t oper_ = NULL;
    mcpwm_cmpr_handle_t comparator_ = NULL;
    mcpwm_gen_handle_t gen_r_ = NULL;
    mcpwm_gen_handle_t gen_l_ = NULL;

    // ADC Handles
    adc_oneshot_unit_handle_t adc_handle_ = NULL;
    
    // Callback
    StallCallback stall_callback_ = nullptr;
    TaskHandle_t monitor_task_handle_ = nullptr;
};