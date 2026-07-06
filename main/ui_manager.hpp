#pragma once

#include <functional>
#include <vector>
#include "lvgl.h"

class UIManager {
public:
    // Create all LVGL objects. Call once from lvgl_task after display_manager.init().
    void init();

    // State transitions. Call via lv_async_call() from app_task.
    void show_startup(std::function<void()> on_complete);
    void show_idle(float height_mm);
    void show_moving_up(float height_mm);
    void show_moving_down(float height_mm);
    void show_goto_preset(float current_mm, float target_mm);
    void show_stalled();
    void show_calibrating();
    void show_saved_confirmation();

    // Update height label without changing UI state (during movement).
    void update_height(float height_mm);

private:
    void init_styles();
    void configure_and_start_arrow_anim(const char* symbol);
    void stop_arrow_anim();
    void update_height_label(float height_mm);
    void clear_overlay_labels();

    // Animation callbacks
    static void arrow_anim_cb(void* var, int32_t v);
    static void anim_opa_cb(void* var, int32_t v);
    static void startup_letter_done_cb(lv_anim_t* a);
    static void startup_fade_done_cb(lv_anim_t* a);
    static void stall_timer_cb(lv_timer_t* t);

    // Height display
    lv_obj_t* height_label_  = nullptr;
    lv_obj_t* unit_label_    = nullptr;

    // Arrow animation
    lv_obj_t* arrow_container_    = nullptr;
    lv_obj_t* main_arrow_lbl_     = nullptr;
    lv_obj_t* trail_arrow_1_lbl_  = nullptr;
    lv_obj_t* trail_arrow_2_lbl_  = nullptr;
    lv_anim_t arrow_anim_         = {};
    bool      is_arrow_animating_ = false;

    // Overlay labels (goto target, stall, calib, saved)
    lv_obj_t* overlay_label_ = nullptr;

    // Startup
    lv_obj_t*              startup_container_ = nullptr;
    std::vector<lv_obj_t*> letter_labels_;
    std::function<void()>  on_startup_finish_;

    // Styles
    lv_style_t style_big_text_    = {};
    lv_style_t style_small_text_  = {};
    lv_style_t style_cyan_bright_ = {};
    lv_style_t style_cyan_medium_ = {};
    lv_style_t style_cyan_light_  = {};

#ifdef CONFIG_LV_COLOR_16_SWAP
    lv_color_t cyan_ = lv_palette_main(LV_PALETTE_RED);
#else
    lv_color_t cyan_ = lv_palette_main(LV_PALETTE_CYAN);
#endif
};
