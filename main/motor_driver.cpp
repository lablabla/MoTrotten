#include "motor_driver.hpp"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <cstdlib>

static const char* TAG = "MotorDriver";

// ─── init ────────────────────────────────────────────────────────────────────

bool MotorDriver::init() {
    // Enable pins
    gpio_config_t en_cfg = {
        .pin_bit_mask = (1ULL << PIN_MOTOR_EN_R) | (1ULL << PIN_MOTOR_EN_L),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&en_cfg) != ESP_OK) return false;
    gpio_set_level(PIN_MOTOR_EN_R, 0);
    gpio_set_level(PIN_MOTOR_EN_L, 0);

    // LEDC timer
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = MOTOR_LEDC_MODE,
        .duty_resolution = MOTOR_LEDC_RES,
        .timer_num       = MOTOR_LEDC_TIMER,
        .freq_hz         = MOTOR_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&timer_cfg) != ESP_OK) return false;

    // Channel R (UP)
    ledc_channel_config_t ch_r = {
        .gpio_num   = PIN_MOTOR_PWM_R,
        .speed_mode = MOTOR_LEDC_MODE,
        .channel    = MOTOR_LEDC_CH_R,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = MOTOR_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    if (ledc_channel_config(&ch_r) != ESP_OK) return false;

    // Channel L (DOWN)
    ledc_channel_config_t ch_l = {
        .gpio_num   = PIN_MOTOR_PWM_L,
        .speed_mode = MOTOR_LEDC_MODE,
        .channel    = MOTOR_LEDC_CH_L,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = MOTOR_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    if (ledc_channel_config(&ch_l) != ESP_OK) return false;

    // ADC unit (shared with ButtonReader)
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id  = MOTOR_IS_ADC_UNIT,
        .clk_src  = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&adc_cfg, &adc_) != ESP_OK) return false;

    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(adc_, MOTOR_IS_CH_R, &ch_cfg) != ESP_OK) return false;
    if (adc_oneshot_config_channel(adc_, MOTOR_IS_CH_L, &ch_cfg) != ESP_OK) return false;

    xTaskCreatePinnedToCore(monitor_task, "motor_mon",
                            TASK_STACK_MOTOR_MON, this,
                            TASK_PRIO_MOTOR_MON, &monitor_task_handle_,
                            TASK_CORE_MOTOR_MON);
    if (!monitor_task_handle_) return false;

    ESP_LOGI(TAG, "Initialized");
    return true;
}

// ─── public commands ─────────────────────────────────────────────────────────

void MotorDriver::move_up() {
    portENTER_CRITICAL(&mux_);
    stalled_ = false;
    if (motor_state_ == MotorState::RAMPING_UP || motor_state_ == MotorState::RUNNING_UP) {
        portEXIT_CRITICAL(&mux_);
        return;
    }
    gpio_set_level(PIN_MOTOR_EN_R, 1);
    gpio_set_level(PIN_MOTOR_EN_L, 0);
    ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_R, 0);
    ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_R);
    ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_L, 0);
    ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_L);
    current_duty_     = 0;
    direction_        = MotorDirection::UP;
    ramp_start_tick_  = xTaskGetTickCount();
    motor_state_      = MotorState::RAMPING_UP;
    portEXIT_CRITICAL(&mux_);
    ESP_LOGI(TAG, "move_up");
}

void MotorDriver::move_down() {
    portENTER_CRITICAL(&mux_);
    stalled_ = false;
    if (motor_state_ == MotorState::RAMPING_DOWN || motor_state_ == MotorState::RUNNING_DOWN) {
        portEXIT_CRITICAL(&mux_);
        return;
    }
    gpio_set_level(PIN_MOTOR_EN_R, 0);
    gpio_set_level(PIN_MOTOR_EN_L, 1);
    ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_R, 0);
    ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_R);
    ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_L, 0);
    ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_L);
    current_duty_     = 0;
    direction_        = MotorDirection::DOWN;
    ramp_start_tick_  = xTaskGetTickCount();
    motor_state_      = MotorState::RAMPING_DOWN;
    portEXIT_CRITICAL(&mux_);
    ESP_LOGI(TAG, "move_down");
}

void MotorDriver::stop() {
    portENTER_CRITICAL(&mux_);
    switch (motor_state_) {
        case MotorState::STOPPED:
        case MotorState::STOPPING_FROM_UP:
        case MotorState::STOPPING_FROM_DOWN:
            portEXIT_CRITICAL(&mux_);
            return;
        case MotorState::RAMPING_UP:
        case MotorState::RUNNING_UP:
            motor_state_ = MotorState::STOPPING_FROM_UP;
            break;
        case MotorState::RAMPING_DOWN:
        case MotorState::RUNNING_DOWN:
            motor_state_ = MotorState::STOPPING_FROM_DOWN;
            break;
    }
    ramp_start_tick_ = xTaskGetTickCount();
    portEXIT_CRITICAL(&mux_);
    ESP_LOGI(TAG, "stop (soft)");
}

void MotorDriver::emergency_stop() {
    // Safe from any context — direct register writes only.
    ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_R, 0);
    ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_R);
    ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_L, 0);
    ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_L);
    gpio_set_level(PIN_MOTOR_EN_R, 0);
    gpio_set_level(PIN_MOTOR_EN_L, 0);
    current_duty_ = 0;
    direction_    = MotorDirection::STOPPED;
    motor_state_  = MotorState::STOPPED;
    stalled_      = true;
    ESP_LOGE(TAG, "emergency_stop");
}

void MotorDriver::set_stall_callback(StallCallback cb) {
    stall_cb_ = cb;
}

bool MotorDriver::is_running() const {
    return motor_state_ == MotorState::RUNNING_UP ||
           motor_state_ == MotorState::RUNNING_DOWN;
}

// ─── private ─────────────────────────────────────────────────────────────────

void MotorDriver::set_duty_raw(uint32_t duty, MotorDirection dir) {
    ledc_channel_t ch = (dir == MotorDirection::UP) ? MOTOR_LEDC_CH_R : MOTOR_LEDC_CH_L;
    ledc_set_duty(MOTOR_LEDC_MODE, ch, duty);
    ledc_update_duty(MOTOR_LEDC_MODE, ch);
    current_duty_ = duty;
}

void MotorDriver::monitor_task(void* arg) {
    static_cast<MotorDriver*>(arg)->monitor_loop();
}

void MotorDriver::monitor_loop() {
    int stall_counter = 0;

    while (true) {
        portENTER_CRITICAL(&mux_);
        MotorState  state = motor_state_;
        MotorDirection dir = direction_;
        TickType_t  ramp_tick = ramp_start_tick_;
        TickType_t  move_tick = move_start_tick_;
        portEXIT_CRITICAL(&mux_);

        TickType_t now     = xTaskGetTickCount();
        uint32_t   elapsed = (uint32_t)pdTICKS_TO_MS(now - ramp_tick);

        switch (state) {
            case MotorState::RAMPING_UP:
            case MotorState::RAMPING_DOWN: {
                // Ramp duty 0 → MOTOR_MAX_DUTY over MOTOR_RAMP_MS
                uint32_t duty = (elapsed >= MOTOR_RAMP_MS)
                    ? MOTOR_MAX_DUTY
                    : (elapsed * MOTOR_MAX_DUTY / MOTOR_RAMP_MS);
                set_duty_raw(duty, dir);
                if (duty >= MOTOR_MAX_DUTY) {
                    portENTER_CRITICAL(&mux_);
                    motor_state_ = (dir == MotorDirection::UP)
                        ? MotorState::RUNNING_UP
                        : MotorState::RUNNING_DOWN;
                    move_start_tick_ = now;
                    portEXIT_CRITICAL(&mux_);
                    stall_counter = 0;
                    ESP_LOGI(TAG, "ramp complete, running");
                }
                break;
            }

            case MotorState::RUNNING_UP:
            case MotorState::RUNNING_DOWN: {
                uint32_t move_elapsed = (uint32_t)pdTICKS_TO_MS(now - move_tick);
                if (move_elapsed > MOTOR_INRUSH_IGNORE_MS) {
                    adc_channel_t ch = (dir == MotorDirection::UP)
                        ? MOTOR_IS_CH_R : MOTOR_IS_CH_L;
                    int raw = 0;
                    if (adc_oneshot_read(adc_, ch, &raw) == ESP_OK) {
                        if (raw > MOTOR_STALL_THRESHOLD_RAW) {
                            stall_counter++;
                        } else {
                            stall_counter = 0;
                        }
                        if (stall_counter >= MOTOR_STALL_CONFIRM_COUNT) {
                            ESP_LOGE(TAG, "stall detected (raw=%d)", raw);
                            emergency_stop();
                            if (stall_cb_) stall_cb_();
                            stall_counter = 0;
                        }
                    }
                }
                break;
            }

            case MotorState::STOPPING_FROM_UP:
            case MotorState::STOPPING_FROM_DOWN: {
                // Ramp duty MOTOR_MAX_DUTY → 0 over MOTOR_RAMP_MS
                uint32_t duty = (elapsed >= MOTOR_RAMP_MS)
                    ? 0
                    : MOTOR_MAX_DUTY - (elapsed * MOTOR_MAX_DUTY / MOTOR_RAMP_MS);
                set_duty_raw(duty, dir);
                if (duty == 0) {
                    gpio_set_level(PIN_MOTOR_EN_R, 0);
                    gpio_set_level(PIN_MOTOR_EN_L, 0);
                    portENTER_CRITICAL(&mux_);
                    motor_state_ = MotorState::STOPPED;
                    direction_   = MotorDirection::STOPPED;
                    portEXIT_CRITICAL(&mux_);
                    stall_counter = 0;
                    ESP_LOGI(TAG, "stopped");
                }
                break;
            }

            case MotorState::STOPPED:
                stall_counter = 0;
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(MOTOR_MON_TASK_MS));
    }
}
