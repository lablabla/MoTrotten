#pragma once

#include <stdint.h>

struct DeskPresets {
    int sit_mm         = 730;   // DESK_DEFAULT_SIT_MM
    int stand_mm       = 1100;  // DESK_DEFAULT_STAND_MM
    int calib_offset   = 0;     // 0 = uncalibrated
};

class NvsManager {
public:
    // nvs_flash_init() + open namespace. Returns false on failure.
    bool init();

    // Load presets from NVS. Returns defaults for missing keys.
    DeskPresets load();

    // Persist sit_mm and stand_mm.
    bool save(const DeskPresets& presets);

    // Persist calibration offset only.
    bool save_calib_offset(int offset_mm);

private:
    bool opened_ = false;
};
