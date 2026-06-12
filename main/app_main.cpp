#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "lvgl.h"

#include "desk_config.h"
#include "motor_driver.hpp"
#include "button_reader.hpp"
#include "height_sensor.hpp"
#include "nvs_manager.hpp"
#include "display_manager.hpp"
#include "ui_manager.hpp"

static const char* TAG = "App";

// ─── Application state ───────────────────────────────────────────────────────

enum class DeskStateEnum {
    STARTUP,
    CALIBRATING,
    IDLE,
    MOVING_UP,
    MOVING_DOWN,
    GOTO_PRESET,
    STALLED,
};

struct DeskState {
    volatile int          current_height_mm = 0;
    volatile bool         stall_detected    = false;
    volatile DeskStateEnum app_state        = DeskStateEnum::STARTUP;
    volatile int          goto_target_mm    = 0;
    DeskPresets           presets           = {};
    SemaphoreHandle_t     mutex             = nullptr;
};

// ─── Module instances ────────────────────────────────────────────────────────

static DeskState      g_state;
static MotorDriver    s_motor;
static ButtonReader   s_buttons;
static HeightSensor   s_sensor;
static NvsManager     s_nvs;
static DisplayManager s_display;
static UIManager      s_ui;

// Notify handle: lvgl_task signals app_main when startup animation completes
static TaskHandle_t s_main_task_handle = nullptr;

// ─── lv_async_call callbacks (zero-allocation) ───────────────────────────────

static void cb_show_idle(void* a) {
    auto* s = static_cast<DeskState*>(a);
    s_ui.show_idle(static_cast<float>(s->current_height_mm));
}
static void cb_show_moving_up(void* a) {
    auto* s = static_cast<DeskState*>(a);
    s_ui.show_moving_up(static_cast<float>(s->current_height_mm));
}
static void cb_show_moving_down(void* a) {
    auto* s = static_cast<DeskState*>(a);
    s_ui.show_moving_down(static_cast<float>(s->current_height_mm));
}
static void cb_show_goto_preset(void* a) {
    auto* s = static_cast<DeskState*>(a);
    s_ui.show_goto_preset(static_cast<float>(s->current_height_mm),
                          static_cast<float>(s->goto_target_mm));
}
static void cb_update_height(void* a) {
    auto* s = static_cast<DeskState*>(a);
    s_ui.update_height(static_cast<float>(s->current_height_mm));
}
static void cb_show_stalled(void*) {
    s_ui.show_stalled();
}
static void cb_show_calibrating(void*) {
    s_ui.show_calibrating();
}
static void cb_show_saved(void*) {
    s_ui.show_saved_confirmation();
}

// ─── LVGL task ───────────────────────────────────────────────────────────────

static void lvgl_task(void*) {
    s_display.init();
    s_ui.init();

    s_ui.show_startup([]() {
        xTaskNotifyGive(s_main_task_handle);
    });

    while (true) {
        s_display.tick();
    }
}

// ─── App task ────────────────────────────────────────────────────────────────

static void app_task(void*) {
    bool       manual_moving    = false;
    TickType_t stall_time       = 0;
    TickType_t goto_start_tick  = 0;
    MotorDirection goto_dir     = MotorDirection::STOPPED;

    while (true) {
        // ── 1. Read height ────────────────────────────────────────────────
        int h = s_sensor.read_mm();
        if (h > 0) g_state.current_height_mm = h;
        int height = g_state.current_height_mm;

        // ── 2. Handle stall (highest priority) ───────────────────────────
        if (g_state.stall_detected) {
            g_state.stall_detected = false;
            manual_moving          = false;
            g_state.app_state      = DeskStateEnum::STALLED;
            stall_time             = xTaskGetTickCount();
            lv_async_call(cb_show_stalled, &g_state);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // ── 3. Stall recovery (2s timeout) ───────────────────────────────
        if (g_state.app_state == DeskStateEnum::STALLED) {
            if (pdTICKS_TO_MS(xTaskGetTickCount() - stall_time) >= 2000) {
                g_state.app_state = DeskStateEnum::IDLE;
                lv_async_call(cb_show_idle, &g_state);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // ── 4. Read buttons ───────────────────────────────────────────────
        s_buttons.poll();
        Button held = s_buttons.state();
        Button edge = s_buttons.edge();

        // ── 5. GOTO_PRESET state management ──────────────────────────────
        if (g_state.app_state == DeskStateEnum::GOTO_PRESET) {
            bool done = false;

            if (held != Button::NONE || edge != Button::NONE) {
                // Any button cancels goto
                s_motor.stop();
                done = true;
                ESP_LOGI(TAG, "goto cancelled by button");
            } else if (pdTICKS_TO_MS(xTaskGetTickCount() - goto_start_tick) > DESK_GOTO_TIMEOUT_MS) {
                s_motor.stop();
                done = true;
                ESP_LOGW(TAG, "goto timeout");
            } else if (height <= DESK_MIN_HEIGHT_MM && goto_dir == MotorDirection::DOWN) {
                s_motor.stop();
                done = true;
            } else if (height >= DESK_MAX_HEIGHT_MM && goto_dir == MotorDirection::UP) {
                s_motor.stop();
                done = true;
            } else if (abs(height - (int)g_state.goto_target_mm) <= DESK_GOTO_TOLERANCE_MM) {
                s_motor.stop();
                done = true;
                ESP_LOGI(TAG, "goto reached target %d mm", (int)g_state.goto_target_mm);
            } else {
                // Overshoot correction
                if (goto_dir == MotorDirection::UP &&
                    height > (int)g_state.goto_target_mm + DESK_GOTO_TOLERANCE_MM) {
                    s_motor.stop();
                    s_motor.move_down();
                    goto_dir = MotorDirection::DOWN;
                } else if (goto_dir == MotorDirection::DOWN &&
                           height < (int)g_state.goto_target_mm - DESK_GOTO_TOLERANCE_MM) {
                    s_motor.stop();
                    s_motor.move_up();
                    goto_dir = MotorDirection::UP;
                }
                lv_async_call(cb_update_height, &g_state);
            }

            if (done) {
                g_state.app_state = DeskStateEnum::IDLE;
                lv_async_call(cb_show_idle, &g_state);
            }

            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // ── 6. Manual move: hold-to-move with soft limits ─────────────────
        if (held == Button::UP) {
            if (height >= DESK_MAX_HEIGHT_MM) {
                if (manual_moving) {
                    s_motor.stop();
                    manual_moving     = false;
                    g_state.app_state = DeskStateEnum::IDLE;
                    lv_async_call(cb_show_idle, &g_state);
                }
            } else if (!manual_moving || s_motor.direction() != MotorDirection::UP) {
                s_motor.move_up();
                manual_moving     = true;
                g_state.app_state = DeskStateEnum::MOVING_UP;
                lv_async_call(cb_show_moving_up, &g_state);
            } else {
                lv_async_call(cb_update_height, &g_state);
            }
        } else if (held == Button::DOWN) {
            if (height <= DESK_MIN_HEIGHT_MM) {
                if (manual_moving) {
                    s_motor.stop();
                    manual_moving     = false;
                    g_state.app_state = DeskStateEnum::IDLE;
                    lv_async_call(cb_show_idle, &g_state);
                }
            } else if (!manual_moving || s_motor.direction() != MotorDirection::DOWN) {
                s_motor.move_down();
                manual_moving     = true;
                g_state.app_state = DeskStateEnum::MOVING_DOWN;
                lv_async_call(cb_show_moving_down, &g_state);
            } else {
                lv_async_call(cb_update_height, &g_state);
            }
        } else {
            // No UP/DOWN held — stop if we were in manual mode
            if (manual_moving) {
                s_motor.stop();
                manual_moving     = false;
                g_state.app_state = DeskStateEnum::IDLE;
                lv_async_call(cb_show_idle, &g_state);
            }
        }

        // ── 7. Preset buttons (edge, only when IDLE) ─────────────────────
        if (g_state.app_state == DeskStateEnum::IDLE &&
            (edge == Button::PRESET1 || edge == Button::PRESET2)) {

            int target = (edge == Button::PRESET1)
                ? g_state.presets.sit_mm
                : g_state.presets.stand_mm;

            // Check hold-to-save gesture (block up to BTN_HOLD_SAVE_MS)
            TickType_t hold_start = xTaskGetTickCount();
            bool saved = false;

            while (true) {
                s_buttons.poll();
                if (s_buttons.state() != edge) break;  // Released

                if (pdTICKS_TO_MS(xTaskGetTickCount() - hold_start) >= BTN_HOLD_SAVE_MS) {
                    xSemaphoreTake(g_state.mutex, portMAX_DELAY);
                    if (edge == Button::PRESET1) g_state.presets.sit_mm   = height;
                    else                         g_state.presets.stand_mm = height;
                    s_nvs.save(g_state.presets);
                    xSemaphoreGive(g_state.mutex);
                    lv_async_call(cb_show_saved, &g_state);
                    ESP_LOGI(TAG, "Preset saved: %d mm", height);
                    saved = true;
                    // Drain button release
                    while (s_buttons.state() != Button::NONE) {
                        s_buttons.poll();
                        vTaskDelay(pdMS_TO_TICKS(20));
                    }
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(20));
            }

            if (!saved) {
                // Execute goto preset
                if (abs(height - target) > DESK_GOTO_TOLERANCE_MM) {
                    goto_dir             = (target > height) ? MotorDirection::UP : MotorDirection::DOWN;
                    g_state.goto_target_mm = target;
                    goto_start_tick      = xTaskGetTickCount();
                    g_state.app_state    = DeskStateEnum::GOTO_PRESET;

                    if (goto_dir == MotorDirection::UP) s_motor.move_up();
                    else                                s_motor.move_down();

                    lv_async_call(cb_show_goto_preset, &g_state);
                    ESP_LOGI(TAG, "goto %d mm", target);
                }
                // Already at target — nothing to do
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ─── app_main ────────────────────────────────────────────────────────────────

extern "C" void app_main() {
    s_main_task_handle = xTaskGetCurrentTaskHandle();

    // 1. NVS
    if (!s_nvs.init()) {
        ESP_LOGE(TAG, "NVS init failed — halting");
        while (true) vTaskDelay(portMAX_DELAY);
    }
    g_state.presets = s_nvs.load();

    // 2. Motor driver (creates ADC unit + motor_mon_task)
    if (!s_motor.init()) {
        ESP_LOGE(TAG, "Motor driver init failed — halting");
        while (true) vTaskDelay(portMAX_DELAY);
    }

    // 3. Button reader (shares ADC unit with motor driver)
    if (!s_buttons.init(s_motor.adc_handle())) {
        ESP_LOGE(TAG, "Button reader init failed — halting");
        while (true) vTaskDelay(portMAX_DELAY);
    }

    // 4. Height sensor
    if (!s_sensor.init()) {
        ESP_LOGE(TAG, "Height sensor init failed — halting");
        while (true) vTaskDelay(portMAX_DELAY);
    }
    s_sensor.set_calib_offset(g_state.presets.calib_offset);

    // 5. Shared state
    g_state.mutex = xSemaphoreCreateMutex();

    // 6. Stall callback
    s_motor.set_stall_callback([]() {
        g_state.stall_detected = true;
    });

    // 7. Start LVGL task — it runs display + UI init + startup animation
    xTaskCreatePinnedToCore(lvgl_task, "lvgl",
                            TASK_STACK_LVGL, nullptr,
                            TASK_PRIO_LVGL, nullptr,
                            TASK_CORE_LVGL);

    // 8. Wait for startup animation to complete
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // 9. Calibration (first boot or cleared NVS)
    if (g_state.presets.calib_offset == 0) {
        ESP_LOGI(TAG, "Calibrating...");
        g_state.app_state = DeskStateEnum::CALIBRATING;
        lv_async_call(cb_show_calibrating, &g_state);
        vTaskDelay(pdMS_TO_TICKS(100)); // Let UI update

        // Configure limit switch GPIO
        gpio_config_t sw_cfg = {
            .pin_bit_mask = 1ULL << PIN_LIMIT_SW,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,   // External 10kΩ pull-up on PCB
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&sw_cfg);

        s_motor.move_down();
        TickType_t calib_start = xTaskGetTickCount();

        while (gpio_get_level(PIN_LIMIT_SW) != 0) {
            if (pdTICKS_TO_MS(xTaskGetTickCount() - calib_start) > CALIB_TIMEOUT_MS) {
                s_motor.emergency_stop();
                ESP_LOGE(TAG, "Calibration timeout — limit switch not triggered. Halting.");
                while (true) vTaskDelay(portMAX_DELAY);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        s_motor.stop();
        vTaskDelay(pdMS_TO_TICKS(200)); // Settle

        // Raw sensor reading at lowest position = offset
        int raw = s_sensor.read_mm();
        if (raw <= 0) {
            ESP_LOGE(TAG, "Sensor read failed during calibration — halting");
            while (true) vTaskDelay(portMAX_DELAY);
        }

        g_state.presets.calib_offset = raw;
        s_sensor.set_calib_offset(raw);
        s_nvs.save_calib_offset(raw);
        ESP_LOGI(TAG, "Calibration complete. Offset = %d mm", raw);
    }

    // 10. Enter idle
    g_state.app_state = DeskStateEnum::IDLE;
    lv_async_call(cb_show_idle, &g_state);

    // 11. Start app task
    xTaskCreatePinnedToCore(app_task, "app",
                            TASK_STACK_APP, nullptr,
                            TASK_PRIO_APP, nullptr,
                            TASK_CORE_APP);

    vTaskDelete(nullptr);
}
