#include "display_manager.hpp"
#include "desk_config.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"

static const char* TAG = "DisplayManager";

// ─── init ────────────────────────────────────────────────────────────────────

bool DisplayManager::init() {
    flush_sem_ = xSemaphoreCreateBinary();
    if (!flush_sem_) return false;

    // SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = PIN_DISP_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = PIN_DISP_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = DISP_WIDTH * 40 * sizeof(uint16_t),
    };
    if (spi_bus_initialize(DISP_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed");
        return false;
    }

    // Panel IO
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num          = PIN_DISP_CS,
        .dc_gpio_num          = PIN_DISP_DC,
        .spi_mode             = 0,
        .pclk_hz              = DISP_SPI_FREQ_HZ,
        .trans_queue_depth    = 10,
        .on_color_trans_done  = notify_flush_ready,
        .user_ctx             = this,
        .lcd_cmd_bits         = 8,
        .lcd_param_bits       = 8,
    };
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISP_SPI_HOST, &io_cfg, &io_) != ESP_OK) {
        ESP_LOGE(TAG, "Panel IO init failed");
        return false;
    }

    // ST7789 panel
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num  = PIN_DISP_RST,
        .rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel  = 16,
        .flags           = { .reset_active_high = 0 },
        .vendor_config   = nullptr,
    };
    if (esp_lcd_new_panel_st7789(io_, &panel_cfg, &panel_) != ESP_OK) {
        ESP_LOGE(TAG, "Panel init failed");
        return false;
    }

    esp_lcd_panel_reset(panel_);
    esp_lcd_panel_init(panel_);
    esp_lcd_panel_invert_color(panel_, true);   // Required for IPS panels
    esp_lcd_panel_set_gap(panel_, 0, 0);        // May need offset adjustment on hardware
    esp_lcd_panel_swap_xy(panel_, false);
    esp_lcd_panel_mirror(panel_, true, false);  // Verify orientation on hardware
    esp_lcd_panel_disp_on_off(panel_, true);

    // LVGL
    lv_init();

    buf1_ = (lv_color_t*)heap_caps_malloc(DISP_WIDTH * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    buf2_ = (lv_color_t*)heap_caps_malloc(DISP_WIDTH * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!buf1_ || !buf2_) {
        ESP_LOGE(TAG, "LVGL buffer allocation failed");
        return false;
    }

    lv_disp_draw_buf_init(&draw_buf_, buf1_, buf2_, DISP_WIDTH * 40);
    lv_disp_drv_init(&disp_drv_);
    disp_drv_.hor_res   = DISP_WIDTH;
    disp_drv_.ver_res   = DISP_HEIGHT;
    disp_drv_.flush_cb  = flush_cb;
    disp_drv_.draw_buf  = &draw_buf_;
    disp_drv_.user_data = this;
    lv_disp_drv_register(&disp_drv_);

    // 1ms tick timer
    esp_timer_create_args_t tick_args = {
        .callback        = lvgl_tick_cb,
        .arg             = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "lvgl_tick",
    };
    esp_timer_create(&tick_args, &tick_timer_);
    esp_timer_start_periodic(tick_timer_, 1000); // 1ms

    ESP_LOGI(TAG, "Initialized (%dx%d)", DISP_WIDTH, DISP_HEIGHT);
    return true;
}

// ─── tick ────────────────────────────────────────────────────────────────────

void DisplayManager::tick() {
    lv_task_handler();
    vTaskDelay(pdMS_TO_TICKS(5));
}

// ─── static callbacks ────────────────────────────────────────────────────────

void DisplayManager::flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_map) {
    auto* dm = static_cast<DisplayManager*>(drv->user_data);
    esp_lcd_panel_draw_bitmap(dm->panel_,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              color_map);
    // Wait for DMA transfer to complete before releasing the buffer
    xSemaphoreTake(dm->flush_sem_, portMAX_DELAY);
    lv_disp_flush_ready(drv);
}

bool DisplayManager::notify_flush_ready(esp_lcd_panel_io_handle_t,
                                        esp_lcd_panel_io_event_data_t*, void* ctx) {
    auto* dm = static_cast<DisplayManager*>(ctx);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(dm->flush_sem_, &woken);
    portYIELD_FROM_ISR(woken);
    return false;
}

void DisplayManager::lvgl_tick_cb(void*) {
    lv_tick_inc(1);
}
