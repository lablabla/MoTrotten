#pragma once

#include "lvgl.h"

class DisplayManager {
public:
    DisplayManager();
    void start_render_loop();

private:
    static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);
    static void lvgl_tick_cb(void *arg);

    lv_disp_draw_buf_t disp_buf_;
    lv_disp_drv_t disp_drv_;
    lv_color_t *buf1_;
    lv_color_t *buf2_;
};
