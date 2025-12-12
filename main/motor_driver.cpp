#include "motor_driver.hpp"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

MotorDriver::MotorDriver() : logger_({.tag = "MotorDriver", .level = espp::Logger::Verbosity::INFO}) {
    // Configure GPIOs for motor enable pins
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << PIN_MOTOR_L_EN) | (1ULL << PIN_MOTOR_R_EN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Configure LEDC for PWM signal
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t channel_conf = {
        .gpio_num = PIN_MOTOR_PWM,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .flags = {
            .output_invert = 0
        }
    };
    ledc_channel_config(&channel_conf);
    logger_.info("Motor driver initialized.");
}

void MotorDriver::set_duty(uint32_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    current_duty_ = duty;
}

void MotorDriver::move_up() {
    gpio_set_level(PIN_MOTOR_R_EN, 0); // Disable down
    gpio_set_level(PIN_MOTOR_L_EN, 1); // Enable up
    
    // Ramp up PWM for soft start
    for (uint32_t duty = current_duty_; duty <= MOTOR_MAX_DUTY; duty += MOTOR_RAMP_STEP) {
        set_duty(duty);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    set_duty(MOTOR_MAX_DUTY);
    logger_.info("Motor moving UP at {} duty.", current_duty_);
}

void MotorDriver::move_down() {
    gpio_set_level(PIN_MOTOR_L_EN, 0); // Disable up
    gpio_set_level(PIN_MOTOR_R_EN, 1); // Enable down

    // Ramp up PWM for soft start
    for (uint32_t duty = current_duty_; duty <= MOTOR_MAX_DUTY; duty += MOTOR_RAMP_STEP) {
        set_duty(duty);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    set_duty(MOTOR_MAX_DUTY);
    logger_.info("Motor moving DOWN at {} duty.", current_duty_);
}

void MotorDriver::stop() {
    // Ramp down
    for (uint32_t duty = current_duty_; duty > 0; duty -= MOTOR_RAMP_STEP) {
        set_duty(duty);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    set_duty(0);
    gpio_set_level(PIN_MOTOR_L_EN, 0);
    gpio_set_level(PIN_MOTOR_R_EN, 0);
    logger_.info("Motor STOPPED.");
}
