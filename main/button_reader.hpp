#pragma once

#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum class Button { NONE, UP, DOWN, PRESET1, PRESET2 };

class ButtonReader {
public:
    // Takes shared ADC unit handle from MotorDriver.
    // Configures GPIO5/CH4 on that unit.
    bool init(adc_oneshot_unit_handle_t shared_adc);

    // Advance debounce state: read ADC, update current_ and edge_.
    // Call exactly once per app_task cycle before reading state()/edge().
    void poll();

    // Current confirmed debounced state. Use for hold-to-move (UP/DOWN).
    Button state() const { return current_; }

    // Returns the button on rising edge only, else NONE.
    // Use for one-shot actions (PRESET1/PRESET2).
    Button edge() const { return edge_; }

private:
    Button decode(int raw);

    adc_oneshot_unit_handle_t adc_       = nullptr;
    Button current_                      = Button::NONE;
    Button edge_                         = Button::NONE;
    Button candidate_                    = Button::NONE;
    Button last_stable_                  = Button::NONE;
    TickType_t debounce_start_           = 0;
};
