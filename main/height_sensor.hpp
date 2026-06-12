#pragma once

#include <stdint.h>

class HeightSensor {
public:
    // Init I2C bus and VL53L0X. Returns false if sensor not found.
    bool init();

    // Single ranging measurement. Returns distance to floor in mm, or -1 on error.
    // Blocks ~30ms.
    int read_mm();

    bool is_valid() const { return last_valid_; }

    // Set calibration offset (sensor reading at lowest position).
    // height_mm = calib_offset - raw_distance.
    void set_calib_offset(int offset_mm) { calib_offset_ = offset_mm; }
    int  calib_offset()             const { return calib_offset_; }

private:
    bool last_valid_   = false;
    int  calib_offset_ = 0;
};
