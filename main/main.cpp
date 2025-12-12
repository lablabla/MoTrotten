#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "logger.hpp"

// --- Drivers ---
extern "C" {
    #include "ina219.h"
}
#include "i2c.hpp"
#include "vl53l.hpp" 

#include "desk_config.h"

static const char *TAG = "STAND_IDF";


extern "C" void app_main(void) 
{    
    espp::I2c i2c_bus({
        .port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .clk_speed = 400000 // 400kHz
    });

    espp::Vl53l vl53l(
    espp::Vl53l::Config{.device_address = espp::Vl53l::DEFAULT_ADDRESS,
                        .write = std::bind(&espp::I2c::write, &i2c, std::placeholders::_1,
                                            std::placeholders::_2, std::placeholders::_3),
                        .read = std::bind(&espp::I2c::read, &i2c, std::placeholders::_1,
                                            std::placeholders::_2, std::placeholders::_3),
                        .log_level = espp::Logger::Verbosity::WARN});

}