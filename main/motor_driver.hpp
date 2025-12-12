#pragma once

#include "driver/ledc.h"
#include "desk_config.h"
#include "logger.hpp"

class MotorDriver {
public:
    MotorDriver();

    void move_up();
    void move_down();
    void stop();

private:
    void set_duty(uint32_t duty);

    espp::Logger logger_;
    uint32_t current_duty_ = 0;
};
