#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "MOTOR_TEST";

// --- PINS ---
#define PIN_MOT_R_PWM    15
#define PIN_MOT_L_PWM    16
#define PIN_MOT_R_EN     17
#define PIN_MOT_L_EN     18

#define PIN_ADC_IS_R     ADC_CHANNEL_0  // GPIO 1
#define PIN_ADC_IS_L     ADC_CHANNEL_1  // GPIO 2

// --- CONFIG ---
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT // 0-1023
#define LEDC_FREQUENCY          5000              // 5 kHz

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Minimal Motor Diagnostic...");

    // 1. SETUP ADC (Current Sense)
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, PIN_ADC_IS_R, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, PIN_ADC_IS_L, &config));

    // 2. SETUP GPIO (Enables)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL<<PIN_MOT_R_EN) | (1ULL<<PIN_MOT_L_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE // Default to OFF
    };
    gpio_config(&io_conf);

    // 3. SETUP PWM (Motor Inputs)
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel_r = {
        .speed_mode     = LEDC_MODE, .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE, .gpio_num = PIN_MOT_R_PWM, .duty = 0, .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_r));

    ledc_channel_config_t ledc_channel_l = {
        .speed_mode     = LEDC_MODE, .channel = LEDC_CHANNEL_1, .timer_sel = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE, .gpio_num = PIN_MOT_L_PWM, .duty = 0, .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_l));

    // --- MAIN LOOP ---
    int r_val = 0, l_val = 0;

    while(1) {
        // --- STATE 1: DISABLED (Everything OFF) ---
        gpio_set_level(PIN_MOT_R_EN, 0);
        gpio_set_level(PIN_MOT_L_EN, 0);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, 0); ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_1, 0); ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_1);
        
        vTaskDelay(500 / portTICK_PERIOD_MS); // Wait for caps to discharge
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, PIN_ADC_IS_R, &r_val));
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, PIN_ADC_IS_L, &l_val));
        ESP_LOGW(TAG, "[DISABLED] EN:0 PWM:0 | IS_R: %d | IS_L: %d", r_val, l_val);
        vTaskDelay(1500 / portTICK_PERIOD_MS);


        // --- STATE 2: ENABLED IDLE (Enables ON, PWM 0) ---
        gpio_set_level(PIN_MOT_R_EN, 1);
        gpio_set_level(PIN_MOT_L_EN, 1);
        
        vTaskDelay(100 / portTICK_PERIOD_MS);
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, PIN_ADC_IS_R, &r_val));
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, PIN_ADC_IS_L, &l_val));
        ESP_LOGI(TAG, "[IDLE]     EN:1 PWM:0 | IS_R: %d | IS_L: %d", r_val, l_val);
        vTaskDelay(1500 / portTICK_PERIOD_MS);


        // --- STATE 3: DRIVE RIGHT (PWM R at 30%) ---
        // Duty 300/1023 is approx 30%
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, 300); ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
        
        vTaskDelay(500 / portTICK_PERIOD_MS); // Let motor spin up
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, PIN_ADC_IS_R, &r_val));
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, PIN_ADC_IS_L, &l_val));
        ESP_LOGI(TAG, "[RIGHT>>]  EN:1 PWM:30| IS_R: %d | IS_L: %d", r_val, l_val);
        
        // Stop
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, 0); ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
        vTaskDelay(500 / portTICK_PERIOD_MS);


        // --- STATE 4: DRIVE LEFT (PWM L at 30%) ---
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_1, 300); ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_1);
        
        vTaskDelay(500 / portTICK_PERIOD_MS);
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, PIN_ADC_IS_R, &r_val));
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, PIN_ADC_IS_L, &l_val));
        ESP_LOGI(TAG, "[<<LEFT]   EN:1 PWM:30| IS_R: %d | IS_L: %d", r_val, l_val);

        // Stop
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_1, 0); ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_1);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}