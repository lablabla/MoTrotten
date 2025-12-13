#include <stdio.h>
#include <string.h>
#include <atomic>
#include <chrono>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "logger.hpp"


#include "ina219.h"
// #include "vl53l.hpp" 
#include "motor_driver.hpp"
#include "display_manager.hpp"
#include "ui_manager.hpp"

#include "desk_config.h"

static const char *TAG = "MoTrotten";

static ina219_t ina_dev;

static std::atomic<uint16_t> g_current_height(0);
static std::atomic<float>    g_current_draw_ma(0.0f);
static std::atomic<bool>     g_is_moving(false);

// CHOOSE YOUR TEST
// #define UI_TEST_MODE UITest::IDLE
// #define UI_TEST_MODE UITest::MANUAL_MOVE_UP
#define UI_TEST_MODE UITest::MANUAL_MOVE_DOWN

/*
// --- NVS Helper Functions ---
void save_height_preset(const char* key, uint16_t height) {
    nvs_handle_t nvs_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_u16(nvs_handle, key, height);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
}

uint16_t load_height_preset(const char* key, uint16_t default_val) {
    nvs_handle_t nvs_handle;
    uint16_t height = default_val;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle) == ESP_OK) {
        nvs_get_u16(nvs_handle, key, &height);
        nvs_close(nvs_handle);
    }
    return height;
}

// Task for polling sensors
void sensor_task(void *pvParameters) {
    // static espp::Logger logger({.tag = "SensorTask", .level = espp::Logger::Verbosity::INFO});

    // espp::I2c i2c({
    //     .port = I2C_NUM_0,
    //     .sda_io_num = PIN_I2C_SDA,
    //     .scl_io_num = PIN_I2C_SCL,
    //     .sda_pullup_en = GPIO_PULLUP_ENABLE,
    //     .scl_pullup_en = GPIO_PULLUP_ENABLE,
    //     .clk_speed = 400000 // 400kHz
    // });

    // // Initialize INA219
    // memset(&ina_dev, 0, sizeof(ina219_t));
    // ESP_ERROR_CHECK(ina219_init_desc(&ina_dev, I2C_ADDR_INA219, I2C_NUM_0, PIN_I2C_SDA, PIN_I2C_SCL));
    // ESP_ERROR_CHECK(ina219_init(&ina_dev));
    // ESP_ERROR_CHECK(ina219_configure(&ina_dev, INA219_BUS_RANGE_16V, INA219_GAIN_0_125,
    //                                  INA219_RES_12BIT_1S, INA219_RES_12BIT_1S, INA219_MODE_CONT_SHUNT_BUS));
    // // Calibrate INA219 for 0.1 Ohm shunt and 4A max current
    // ESP_ERROR_CHECK(ina219_calibrate(&ina_dev, 0.1));
    // logger.info("INA219 Initialized");


    // espp::Vl53l vl53l(
    // espp::Vl53l::Config{.device_address = espp::Vl53l::DEFAULT_ADDRESS,
    //                     .write = std::bind(&espp::I2c::write, &i2c, std::placeholders::_1,
    //                                         std::placeholders::_2, std::placeholders::_3),
    //                     .read = std::bind(&espp::I2c::read, &i2c, std::placeholders::_1,
    //                                         std::placeholders::_2, std::placeholders::_3),
    //                     .log_level = espp::Logger::Verbosity::WARN});

    // std::error_code ec;
    // if (!vl53l.set_timing_budget_ms(20, ec)) {
    //     logger.error("Failed to set timing budget: {}", ec.message());
    // }
    // if (!vl53l.set_inter_measurement_period_ms(50, ec)) {
    //     logger.error("Failed to set inter measurement period: {}", ec.message());
    // }
    // if (!vl53l.start_ranging(ec)) {
    //     logger.error("Failed to start ranging: {}", ec.message());
    // } else {
    //     logger.info("VL53L0X Initialized and ranging started.");
    // }

    // while (1) {
    //     // Read INA219
    //     float current;
    //     if (ina219_get_current(&ina_dev, &current) == ESP_OK) {
    //         g_current_draw_ma = current * 1000.0f; // Convert A to mA
    //     }

    //     // Read VL53L0X
    //     uint16_t range_mm = vl53l.get_distance_mm(ec);
    //     if (ec) {
    //          logger.warn("Failed to get range: {}", ec.message());
    //     }
    //     else {
    //         g_current_height = range_mm;
    //     }
        
    //     vTaskDelay(pdMS_TO_TICKS(50));
    // }
}

// Task for motor control and logic

enum class DeskState {
    IDLE,
    MOVING_UP,
    MOVING_DOWN,
    MOVING_TO_PRESET
};

void control_task(void *pvParameters) {
    static espp::Logger logger({.tag = "ControlTask", .level = espp::Logger::Verbosity::INFO});
    MotorDriver motor;
    DeskState state = DeskState::IDLE;

    // Load presets from NVS
    uint16_t sit_height = load_height_preset(NVS_KEY_SIT, 700); // Default 700mm
    uint16_t stand_height = load_height_preset(NVS_KEY_STAND, 1100); // Default 1100mm
    uint16_t target_height = 0;

    logger.info("Presets Loaded: Sit={}, Stand={}", sit_height, stand_height);

    // Button GPIO Configuration
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << PIN_BTN_UP) | (1ULL << PIN_BTN_DOWN) |
                        (1ULL << PIN_BTN_PRESET_1) | (1ULL << PIN_BTN_PRESET_2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_conf);

    logger.info("Control Task Started.");

    uint64_t preset1_press_time = 0;
    uint64_t preset2_press_time = 0;
    const uint64_t LONG_PRESS_DURATION = 2000; // 2 seconds

    while (1) {
        // Read button states
        bool btn_up_pressed = !gpio_get_level(PIN_BTN_UP);
        bool btn_down_pressed = !gpio_get_level(PIN_BTN_DOWN);
        bool btn_preset1_pressed = !gpio_get_level(PIN_BTN_PRESET_1);
        bool btn_preset2_pressed = !gpio_get_level(PIN_BTN_PRESET_2);

        uint16_t current_height = g_current_height.load();
        float current_ma = g_current_draw_ma.load();

        // Safety first: Collision detection
        if (g_is_moving && current_ma > COLLISION_MA) {
            motor.stop();
            g_is_moving = false;
            state = DeskState::IDLE;
            logger.error("COLLISION DETECTED! Current: {:.2f} mA. Motor stopped.", current_ma);
            vTaskDelay(pdMS_TO_TICKS(2000)); // Debounce/wait
            continue;
        }

        // State Machine
        switch (state) {
            case DeskState::IDLE:
                // Manual movement
                if (btn_up_pressed && current_height < DESK_MAX_HEIGHT_MM) {
                    state = DeskState::MOVING_UP;
                    motor.move_up();
                    g_is_moving = true;
                } else if (btn_down_pressed && current_height > DESK_MIN_HEIGHT_MM) {
                    state = DeskState::MOVING_DOWN;
                    motor.move_down();
                    g_is_moving = true;
                }
                
                // Preset Go-To Logic
                if (btn_preset1_pressed && !g_is_moving) {
                    state = DeskState::MOVING_TO_PRESET;
                    target_height = stand_height;
                }
                if (btn_preset2_pressed && !g_is_moving) {
                    state = DeskState::MOVING_TO_PRESET;
                    target_height = sit_height;
                }

                // Preset Save Logic (Long Press)
                if(btn_preset1_pressed) {
                    if (preset1_press_time == 0) preset1_press_time = esp_log_timestamp();
                    if (esp_log_timestamp() - preset1_press_time > LONG_PRESS_DURATION) {
                        save_height_preset(NVS_KEY_STAND, current_height);
                        stand_height = current_height;
                        logger.info("New Stand Height Saved: {} mm", stand_height);
                        preset1_press_time = 0; // Reset
                    }
                } else {
                    preset1_press_time = 0;
                }

                if(btn_preset2_pressed) {
                    if (preset2_press_time == 0) preset2_press_time = esp_log_timestamp();
                    if (esp_log_timestamp() - preset2_press_time > LONG_PRESS_DURATION) {
                        save_height_preset(NVS_KEY_SIT, current_height);
                        sit_height = current_height;
                        logger.info("New Sit Height Saved: {} mm", sit_height);
                        preset2_press_time = 0; // Reset
                    }
                } else {
                    preset2_press_time = 0;
                }
                break;

            case DeskState::MOVING_UP:
                if (!btn_up_pressed || current_height >= DESK_MAX_HEIGHT_MM) {
                    state = DeskState::IDLE;
                    motor.stop();
                    g_is_moving = false;
                }
                break;
            
            case DeskState::MOVING_DOWN:
                if (!btn_down_pressed || current_height <= DESK_MIN_HEIGHT_MM) {
                    state = DeskState::IDLE;
                    motor.stop();
                    g_is_moving = false;
                }
                break;
            
            case DeskState::MOVING_TO_PRESET:
                // Check if we need to move up or down
                if (current_height < target_height - 5) { // 5mm tolerance
                     if (!g_is_moving) {
                        motor.move_up();
                        g_is_moving = true;
                     }
                } else if (current_height > target_height + 5) {
                    if (!g_is_moving) {
                        motor.move_down();
                        g_is_moving = true;
                    }
                } else {
                    motor.stop();
                    g_is_moving = false;
                    state = DeskState::IDLE;
                    logger.info("Reached preset height: {} mm", target_height);
                }
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // Main control loop delay
    }
}
*/

void gui_task(void *pvParameters) {
    static espp::Logger logger({.tag = "GuiTask", .level = espp::Logger::Verbosity::INFO});
    logger.info("GUI Task Started.");
    std::atomic<bool> gui_initialized(false);
    DisplayManager display;
    UIManager ui;
    // ui.play_startup_animation([&]() {
    //   printf("Startup Animation Complete! Showing Main Screen...\n");
      gui_initialized = true;
    // });
    while(1) {
      if (gui_initialized.load())
      {
        switch (UI_TEST_MODE) {
          case UITest::IDLE:
              ui.test_idle_animation();
              break;
          case UITest::MANUAL_MOVE_UP:
              ui.test_manual_move_animation(true);
              break;
          case UITest::MANUAL_MOVE_DOWN:
              ui.test_manual_move_animation(false);
              break;
        }
      }
      lv_timer_handler(); // Handle LVGL tasks
      vTaskDelay(pdMS_TO_TICKS(50)); // Slower delay for tests
    }
}


extern "C" void app_main(void) 
{    
    static espp::Logger logger({.tag = TAG, .level = espp::Logger::Verbosity::INFO});
    logger.info("Booting MoTrotten Display Test...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    logger.info("NVS Initialized.");

    // Create GUI task for display test
    xTaskCreatePinnedToCore(gui_task, "GuiTask", 8192, NULL, 5, NULL, 1);
    
    // Create tasks and pin them to cores
    // xTaskCreatePinnedToCore(sensor_task, "SensorTask", 4096, NULL, 5, NULL, 0);
    // xTaskCreatePinnedToCore(control_task, "ControlTask", 8192, NULL, 5, NULL, 1);
}