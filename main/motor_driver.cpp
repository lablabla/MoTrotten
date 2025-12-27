#include "motor_driver.hpp"
#include <cmath>

// --- Configuration ---
// Adjust these based on your specific motor testing
#define STALL_CHECK_PERIOD_MS    50
#define STALL_STARTUP_IGNORE_MS  500   // Ignore inrush current for first 0.5s
#define STALL_THRESHOLD_RAW      2800  // ~2.2V (Assuming 12-bit ADC, 3.3V ref). Calibrate this!
#define STALL_CONFIRM_COUNT      5     // Must be over threshold for 5 checks (250ms) to trigger

MotorDriver::MotorDriver() : logger_({.tag = "MotorDriver", .level = espp::Logger::Verbosity::INFO}) {
    // 1. Configure Enable Pins (GPIO)
    gpio_config_t en_conf = {};
    en_conf.intr_type = GPIO_INTR_DISABLE;
    en_conf.mode = GPIO_MODE_OUTPUT;
    en_conf.pin_bit_mask = (1ULL << PIN_MOTOR_R_EN) | (1ULL << PIN_MOTOR_L_EN);
    en_conf.pull_down_en = GPIO_PULLDOWN_ENABLE; 
    en_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&en_conf);
    
    enable_driver(false);

    // 2. Configure ADC for Current Sensing
    // Assuming ADC Unit 1 for simplicity. Check your specific pins in datasheet.
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc_handle_));

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12, // 11dB or 12dB covers full 3.3V range
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    // Configure both R_IS and L_IS channels
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle_, (adc_channel_t)PIN_MOTOR_R_IS, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle_, (adc_channel_t)PIN_MOTOR_L_IS, &config));


    // 3. Configure MCPWM Timer
    mcpwm_timer_config_t timer_conf = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, 
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = 10 * 1000 * 1000 / 20000, 
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_conf, &timer_));
    period_ticks_ = timer_conf.period_ticks;

    // 4. Configure Operator
    mcpwm_operator_config_t oper_conf = { .group_id = 0 };
    ESP_ERROR_CHECK(mcpwm_new_operator(&oper_conf, &oper_));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper_, timer_));

    // 5. Configure Comparator
    mcpwm_comparator_config_t cmpr_conf = { .flags = { .update_cmp_on_tez = true } };
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper_, &cmpr_conf, &comparator_));

    // 6. Configure Generators
    mcpwm_generator_config_t gen_conf = {};
    gen_conf.gen_gpio_num = PIN_MOTOR_R_PWM;
    ESP_ERROR_CHECK(mcpwm_new_generator(oper_, &gen_conf, &gen_r_));
    gen_conf.gen_gpio_num = PIN_MOTOR_L_PWM;
    ESP_ERROR_CHECK(mcpwm_new_generator(oper_, &gen_conf, &gen_l_));

    // 7. Start Timer
    ESP_ERROR_CHECK(mcpwm_timer_enable(timer_));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer_, MCPWM_TIMER_START_NO_STOP));

    // 8. Start Monitoring Task
    xTaskCreate(monitor_task_entry, "motor_mon", 4096, this, 5, &monitor_task_handle_);

    logger_.info("Motor Driver Initialized with Stall Detection.");
}

MotorDriver::~MotorDriver() {
    stop();
    if (monitor_task_handle_) {
        vTaskDelete(monitor_task_handle_);
    }
    if (timer_) {
        mcpwm_del_timer(timer_);
    }
    if (adc_handle_) {
        adc_oneshot_del_unit(adc_handle_);
    }
}

void MotorDriver::register_stall_callback(StallCallback cb) {
    stall_callback_ = cb;
}

void MotorDriver::monitor_task_entry(void* arg) {
    MotorDriver* driver = static_cast<MotorDriver*>(arg);
    driver->monitor_task_loop();
}

void MotorDriver::monitor_task_loop() {
    int stall_counter = 0;
    
    while (true) {
        // Only check if motor is supposedly moving and we aren't already in a stalled state
        if (current_speed_ != 0.0f && !is_stalled_) {
            
            // 1. Check if we are in the "Inrush Ignore" window
            TickType_t now = xTaskGetTickCount();
            if (pdTICKS_TO_MS(now - movement_start_tick_) > STALL_STARTUP_IGNORE_MS) {
                
                int raw_val = 0;
                adc_channel_t channel_to_read;

                // 2. Select the channel based on direction
                // If moving UP (Speed > 0), the Right Half Bridge is active -> Read R_IS
                // If moving DOWN (Speed < 0), the Left Half Bridge is active -> Read L_IS
                if (current_speed_ > 0) {
                    channel_to_read = (adc_channel_t)PIN_MOTOR_R_IS;
                } else {
                    channel_to_read = (adc_channel_t)PIN_MOTOR_L_IS;
                }

                // 3. Read ADC
                if (adc_oneshot_read(adc_handle_, channel_to_read, &raw_val) == ESP_OK) {
                    
                    // 4. Check Threshold
                    if (raw_val > STALL_THRESHOLD_RAW) {
                        stall_counter++;
                        // logger_.warn("High Current: {}", raw_val); // Uncomment for debug
                    } else {
                        stall_counter = 0;
                    }

                    // 5. Trigger Stall
                    if (stall_counter >= STALL_CONFIRM_COUNT) {
                        logger_.error("STALL DETECTED! Stopping motor.");
                        
                        // Stop physics immediately
                        stop();
                        
                        // Set state
                        is_stalled_ = true;

                        // Notify App
                        if (stall_callback_) {
                            stall_callback_(true);
                        }
                    }
                }
            }
        } else {
            // Not moving, reset counter
            stall_counter = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(STALL_CHECK_PERIOD_MS));
    }
}

void MotorDriver::enable_driver(bool enable) {
    int level = enable ? 1 : 0;
    gpio_set_level(PIN_MOTOR_R_EN, level);
    gpio_set_level(PIN_MOTOR_L_EN, level);
}

void MotorDriver::set_speed(float speed) {
    if (speed > 100.0f) { speed = 100.0f; }
    if (speed < -100.0f) { speed = -100.0f; }
    
    current_speed_ = speed;

    uint32_t duty_ticks = (uint32_t)(std::abs(speed) / 100.0f * period_ticks_);
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator_, duty_ticks));

    if (speed > 0.1f) {
        // UP
        mcpwm_generator_set_force_level(gen_r_, -1, true); 
        mcpwm_generator_set_action_on_timer_event(gen_r_, 
                MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH));
        mcpwm_generator_set_action_on_compare_event(gen_r_, 
                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator_, MCPWM_GEN_ACTION_LOW));

        mcpwm_generator_set_force_level(gen_l_, 0, true);
        enable_driver(true);

    } else if (speed < -0.1f) {
        // DOWN
        mcpwm_generator_set_force_level(gen_r_, 0, true);

        mcpwm_generator_set_force_level(gen_l_, -1, true); 
        mcpwm_generator_set_action_on_timer_event(gen_l_, 
                MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH));
        mcpwm_generator_set_action_on_compare_event(gen_l_, 
                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator_, MCPWM_GEN_ACTION_LOW));

        enable_driver(true);
    } else {
        // STOP
        mcpwm_generator_set_force_level(gen_r_, 0, true);
        mcpwm_generator_set_force_level(gen_l_, 0, true);
        enable_driver(false);
    }
}

void MotorDriver::move_up() {
    // If we were stalled, verify if we can clear it.
    // For now, any new move command attempts to clear the stall state.
    if (is_stalled_) {
        is_stalled_ = false;
        if (stall_callback_) {
            stall_callback_(false); // Notify app: Stall cleared
        }
    }

    // Capture start time for inrush protection
    movement_start_tick_ = xTaskGetTickCount();
    
    logger_.info("Moving UP");
    for (float s = 10.0f; s <= 100.0f; s += 2.0f) {
        set_speed(s);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    set_speed(100.0f);
}

void MotorDriver::move_down() {
    if (is_stalled_) {
        is_stalled_ = false;
        if (stall_callback_) {
            stall_callback_(false); // Notify app: Stall cleared
        }
    }

    movement_start_tick_ = xTaskGetTickCount();

    logger_.info("Moving DOWN");
    for (float s = -10.0f; s >= -100.0f; s -= 2.0f) {
        set_speed(s);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    set_speed(-100.0f);
}

void MotorDriver::stop() {
    logger_.info("Stopping");
    if (current_speed_ > 0) {
        for (float s = current_speed_; s >= 0; s -= 5.0f) {
            set_speed(s);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    } else if (current_speed_ < 0) {
        for (float s = current_speed_; s <= 0; s += 5.0f) {
            set_speed(s);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    set_speed(0.0f);
    
    // Note: We do NOT clear is_stalled_ here. 
    // If stop() was called by the user, that's fine.
    // If stop() was called by the stall task, is_stalled_ is already true.
}