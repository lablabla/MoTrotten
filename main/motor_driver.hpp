#pragma once

#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "esp_adc/adc_oneshot.h"
#include "desk_config.h"

enum class MotorDirection { UP, DOWN, STOPPED };

class MotorDriver {
public:
    using StallCallback = std::function<void()>;

    // Init LEDC, GPIO enables, ADC unit, spawn motor_mon_task.
    bool init();

    // Non-blocking. Sets direction and starts ramp in motor_mon_task.
    // Clears stall state. Idempotent if already moving in same direction.
    void move_up();
    void move_down();

    // Non-blocking soft stop. motor_mon_task ramps duty to 0.
    void stop();

    // Immediate stop — no ramp. Safe from any task context.
    void emergency_stop();

    void set_stall_callback(StallCallback cb);

    MotorDirection direction() const { return direction_; }
    bool is_stalled()          const { return stalled_; }
    bool is_running()          const;

    // Shared ADC unit handle — pass to ButtonReader at init.
    adc_oneshot_unit_handle_t adc_handle() const { return adc_; }

private:
    enum class MotorState {
        STOPPED,
        RAMPING_UP,
        RUNNING_UP,
        STOPPING_FROM_UP,
        RAMPING_DOWN,
        RUNNING_DOWN,
        STOPPING_FROM_DOWN,
    };

    void set_duty_raw(uint32_t duty, MotorDirection dir);
    void monitor_loop();
    static void monitor_task(void* arg);

    volatile MotorState     motor_state_  = MotorState::STOPPED;
    volatile MotorDirection direction_    = MotorDirection::STOPPED;
    volatile uint32_t       current_duty_ = 0;
    volatile bool           stalled_      = false;
    TickType_t              ramp_start_tick_ = 0;
    TickType_t              move_start_tick_ = 0;

    adc_oneshot_unit_handle_t adc_             = nullptr;
    StallCallback             stall_cb_        = nullptr;
    TaskHandle_t              monitor_task_handle_ = nullptr;
    portMUX_TYPE              mux_             = portMUX_INITIALIZER_UNLOCKED;
};
