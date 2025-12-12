#include <stdio.h>
#include <string.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "logger.hpp"


#include "ina219.h"
#include "i2c.hpp"
#include "vl53l.hpp" 

#include "desk_config.h"

static const char *TAG = "MoTrotten";

static ina219_t ina_dev;

static std::atomic<uint16_t> g_current_height(0);
static std::atomic<float>    g_current_draw_ma(0.0f);
static std::atomic<bool>     g_is_moving(false);

// Task for polling sensors
void sensor_task(void *pvParameters) {
    static espp::Logger logger({.tag = "SensorTask", .level = espp::Logger::Verbosity::INFO});

    espp::I2c i2c({
        .port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .clk_speed = 400000 // 400kHz
    });

    // Initialize INA219
    memset(&ina_dev, 0, sizeof(ina219_t));
    ESP_ERROR_CHECK(ina219_init_desc(&ina_dev, I2C_ADDR_INA219, I2C_NUM_0, PIN_I2C_SDA, PIN_I2C_SCL));
    ESP_ERROR_CHECK(ina219_init(&ina_dev));
    ESP_ERROR_CHECK(ina219_configure(&ina_dev, INA219_BUS_RANGE_16V, INA219_GAIN_0_125,
                                     INA219_RES_12BIT_1S, INA219_RES_12BIT_1S, INA219_MODE_CONT_SHUNT_BUS));
    // Calibrate INA219 for 0.1 Ohm shunt and 4A max current
    ESP_ERROR_CHECK(ina219_calibrate(&ina_dev, 0.1));
    logger.info("INA219 Initialized");


    espp::Vl53l vl53l(
    espp::Vl53l::Config{.device_address = espp::Vl53l::DEFAULT_ADDRESS,
                        .write = std::bind(&espp::I2c::write, &i2c, std::placeholders::_1,
                                            std::placeholders::_2, std::placeholders::_3),
                        .read = std::bind(&espp::I2c::read, &i2c, std::placeholders::_1,
                                            std::placeholders::_2, std::placeholders::_3),
                        .log_level = espp::Logger::Verbosity::WARN});

    std::error_code ec;
    if (!vl53l.set_timing_budget_ms(20, ec)) {
        logger.error("Failed to set timing budget: {}", ec.message());
    }
    if (!vl53l.set_inter_measurement_period_ms(50, ec)) {
        logger.error("Failed to set inter measurement period: {}", ec.message());
    }
    if (!vl53l.start_ranging(ec)) {
        logger.error("Failed to start ranging: {}", ec.message());
    } else {
        logger.info("VL53L0X Initialized and ranging started.");
    }

    while (1) {
        // Read INA219
        float current;
        if (ina219_get_current(&ina_dev, &current) == ESP_OK) {
            g_current_draw_ma = current * 1000.0f; // Convert A to mA
        }

        // Read VL53L0X
        uint16_t range_mm = vl53l.get_distance_mm(ec);
        if (ec) {
             logger.warn("Failed to get range: {}", ec.message());
        }
        else {
            g_current_height = range_mm;
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Task for motor control and logic
void control_task(void *pvParameters) {
    static espp::Logger logger({.tag = "ControlTask", .level = espp::Logger::Verbosity::INFO});
    logger.info("Control Task Started.");

    while (1) {
        // For now, just print the current state from sensors.
        logger.info("Height: {} mm, Current: {:.2f} mA, Moving: {}",
                    g_current_height.load(), g_current_draw_ma.load(), g_is_moving.load());

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


extern "C" void app_main(void) 
{    
    static espp::Logger logger({.tag = TAG, .level = espp::Logger::Verbosity::INFO});
    logger.info("Booting MoTrotten...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    logger.info("NVS Initialized.");

    // Create tasks and pin them to cores
    xTaskCreatePinnedToCore(sensor_task, "SensorTask", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(control_task, "ControlTask", 4096, NULL, 5, NULL, 1);
}