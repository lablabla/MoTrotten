#include "button_reader.hpp"
#include "desk_config.h"
#include "esp_log.h"

static const char* TAG = "ButtonReader";

bool ButtonReader::init(adc_oneshot_unit_handle_t shared_adc) {
    adc_ = shared_adc;
    adc_oneshot_chan_cfg_t cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(adc_, BTN_ADC_CHANNEL, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure button ADC channel");
        return false;
    }
    debounce_start_ = xTaskGetTickCount();
    ESP_LOGI(TAG, "Initialized");
    return true;
}

void ButtonReader::poll() {
    int raw = 0;
    adc_oneshot_read(adc_, BTN_ADC_CHANNEL, &raw);

    Button sampled = decode(raw);

    // Debounce: candidate must be stable for BTN_DEBOUNCE_MS
    if (sampled != candidate_) {
        candidate_      = sampled;
        debounce_start_ = xTaskGetTickCount();
    }

    Button prev = last_stable_;

    if (pdTICKS_TO_MS(xTaskGetTickCount() - debounce_start_) >= BTN_DEBOUNCE_MS) {
        last_stable_ = candidate_;
    }

    current_ = last_stable_;

    // Rising edge: new stable press that differs from previous stable state
    if (current_ != Button::NONE && current_ != prev) {
        edge_ = current_;
    } else {
        edge_ = Button::NONE;
    }
}

Button ButtonReader::decode(int raw) {
    if (raw < BTN_UP_MAX)                                    return Button::UP;
    if (raw >= BTN_DOWN_MIN    && raw <= BTN_DOWN_MAX)       return Button::DOWN;
    if (raw >= BTN_PRESET1_MIN && raw <= BTN_PRESET1_MAX)    return Button::PRESET1;
    if (raw >= BTN_PRESET2_MIN && raw <= BTN_PRESET2_MAX)    return Button::PRESET2;
    return Button::NONE;
}
