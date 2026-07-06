#pragma once

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

class DisplayManager {
public:
    // Init SPI bus, ST7789 panel, LVGL, flush callback, tick timer.
    // Must be called from lvgl_task before any LVGL calls.
    bool init();

    // Drive LVGL rendering. Call in a tight loop from lvgl_task.
    void tick();

private:
    static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_map);
    static bool notify_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                   esp_lcd_panel_io_event_data_t* edata, void* ctx);
    static void lvgl_tick_cb(void* arg);

    esp_lcd_panel_handle_t    panel_     = nullptr;
    esp_lcd_panel_io_handle_t io_        = nullptr;
    lv_disp_drv_t             disp_drv_ = {};
    lv_disp_draw_buf_t        draw_buf_ = {};
    lv_color_t*               buf1_     = nullptr;
    lv_color_t*               buf2_     = nullptr;
    esp_timer_handle_t        tick_timer_ = nullptr;
    SemaphoreHandle_t         flush_sem_  = nullptr;
};
