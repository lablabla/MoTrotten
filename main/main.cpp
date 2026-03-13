
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "MOTOR_TEST";

// --- PIN DEFINITIONS ---
#define R_EN_PIN  GPIO_NUM_5
#define L_EN_PIN  GPIO_NUM_15
#define R_PWM_PIN GPIO_NUM_6
#define L_PWM_PIN GPIO_NUM_16

// PWM Settings
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO_R        (R_PWM_PIN)
#define LEDC_OUTPUT_IO_L        (L_PWM_PIN)
#define LEDC_CHANNEL_R          LEDC_CHANNEL_0
#define LEDC_CHANNEL_L          LEDC_CHANNEL_1
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT // Set duty resolution to 10 bits
#define LEDC_FREQUENCY          5000              // Frequency in Hertz. Set frequency at 5 kHz

void init_gpio() {
    // Configure Enable Pins as Output
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << R_EN_PIN) | (1ULL << L_EN_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Enable the driver (Active High)
    gpio_set_level(R_EN_PIN, 1);
    gpio_set_level(L_EN_PIN, 1);
    ESP_LOGI(TAG, "Motor Driver Enabled");
}

void init_pwm() {
    // 1. Timer Config
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode       = LEDC_MODE;
    ledc_timer.timer_num        = LEDC_TIMER;
    ledc_timer.duty_resolution  = LEDC_DUTY_RES;
    ledc_timer.freq_hz          = LEDC_FREQUENCY;
    ledc_timer.clk_cfg          = LEDC_AUTO_CLK;
    ledc_timer_config(&ledc_timer);

    // 2. Channel Config (Right)
    ledc_channel_config_t ledc_channel_r = {};
    ledc_channel_r.speed_mode     = LEDC_MODE;
    ledc_channel_r.channel        = LEDC_CHANNEL_R;
    ledc_channel_r.timer_sel      = LEDC_TIMER;
    ledc_channel_r.intr_type      = LEDC_INTR_DISABLE;
    ledc_channel_r.gpio_num       = R_PWM_PIN;
    ledc_channel_r.duty           = 0; // Set duty to 0%
    ledc_channel_r.hpoint         = 0;
    ledc_channel_config(&ledc_channel_r);

    // 3. Channel Config (Left)
    ledc_channel_config_t ledc_channel_l = {};
    ledc_channel_l.speed_mode     = LEDC_MODE;
    ledc_channel_l.channel        = LEDC_CHANNEL_L;
    ledc_channel_l.timer_sel      = LEDC_TIMER;
    ledc_channel_l.intr_type      = LEDC_INTR_DISABLE;
    ledc_channel_l.gpio_num       = L_PWM_PIN;
    ledc_channel_l.duty           = 0;
    ledc_channel_l.hpoint         = 0;
    ledc_channel_config(&ledc_channel_l);
}

// Helper to set PWM Duty
void set_motor_speed(int speed_r, int speed_l) {
    // Duty range is 0 to 1023 (10-bit resolution)
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_R, speed_r);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_R);
    
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_L, speed_l);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_L);
}

extern "C" void app_main() {
    init_gpio();
    init_pwm();

    ESP_LOGI(TAG, "Starting Test Loop...");

    while (1) {
        ESP_LOGI(TAG, "Moving FORWARD (50%)");
        // 512 is 50% of 1023 (10-bit)
        set_motor_speed(512, 0); 
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "STOP");
        set_motor_speed(0, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "Moving BACKWARD (50%)");
        set_motor_speed(0, 512);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "STOP");
        set_motor_speed(0, 0);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}