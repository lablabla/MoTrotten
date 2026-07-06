#include "height_sensor.hpp"
#include "desk_config.h"
#include "VL53L0X.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char* TAG = "HeightSensor";

static i2c_master_bus_handle_t s_bus = nullptr;
static i2c_master_dev_handle_t s_dev = nullptr;
static VL53L0X*                s_tof = nullptr;

bool HeightSensor::init() {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = I2C_PORT,
        .sda_io_num        = PIN_I2C_SDA,
        .scl_io_num        = PIN_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags             = { .enable_internal_pullup = false },
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed");
        return false;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = VL53L0X_ADDR,
        .scl_speed_hz    = I2C_FREQ_HZ,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed");
        return false;
    }

    s_tof = new VL53L0X(s_dev);
    s_tof->setTimeout(500);

    if (!s_tof->init()) {
        ESP_LOGE(TAG, "VL53L0X init failed — sensor not found at 0x%02X", VL53L0X_ADDR);
        return false;
    }

    ESP_LOGI(TAG, "VL53L0X ready");
    return true;
}

int HeightSensor::read_mm() {
    if (!s_tof) return -1;

    uint16_t raw = s_tof->readRangeSingleMillimeters();
    last_valid_ = !s_tof->timeoutOccurred() && raw != 65535;

    if (!last_valid_) return -1;

    // Sensor points down: raw = distance to floor.
    // Height = offset measured at calibration - current distance.
    if (calib_offset_ == 0) {
        // Not calibrated — return raw distance (useful during calibration itself)
        return static_cast<int>(raw);
    }
    return calib_offset_ - static_cast<int>(raw);
}
