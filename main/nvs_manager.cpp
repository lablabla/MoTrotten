#include "nvs_manager.hpp"
#include "desk_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char* TAG = "NvsManager";

bool NvsManager::init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition erased and re-initialized");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return false;
    }
    opened_ = true;
    ESP_LOGI(TAG, "Initialized");
    return true;
}

DeskPresets NvsManager::load() {
    DeskPresets p;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "Namespace not found, using defaults");
        return p;
    }
    int32_t val;
    if (nvs_get_i32(h, NVS_KEY_SIT,   &val) == ESP_OK) p.sit_mm       = val;
    if (nvs_get_i32(h, NVS_KEY_STAND, &val) == ESP_OK) p.stand_mm     = val;
    if (nvs_get_i32(h, NVS_KEY_CALIB, &val) == ESP_OK) p.calib_offset = val;
    nvs_close(h);
    ESP_LOGI(TAG, "Loaded: sit=%d stand=%d calib=%d", p.sit_mm, p.stand_mm, p.calib_offset);
    return p;
}

bool NvsManager::save(const DeskPresets& p) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = (nvs_set_i32(h, NVS_KEY_SIT,   p.sit_mm)       == ESP_OK)
           && (nvs_set_i32(h, NVS_KEY_STAND, p.stand_mm)     == ESP_OK)
           && (nvs_commit(h)                                   == ESP_OK);
    nvs_close(h);
    if (ok) ESP_LOGI(TAG, "Saved: sit=%d stand=%d", p.sit_mm, p.stand_mm);
    return ok;
}

bool NvsManager::save_calib_offset(int offset_mm) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = (nvs_set_i32(h, NVS_KEY_CALIB, offset_mm) == ESP_OK)
           && (nvs_commit(h)                              == ESP_OK);
    nvs_close(h);
    if (ok) ESP_LOGI(TAG, "Calibration offset saved: %d mm", offset_mm);
    return ok;
}
