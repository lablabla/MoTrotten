#pragma once

#include "lvgl.h"

// Enum to define which test to run
enum class UITest {
    IDLE,
    MANUAL_MOVE_UP,
    MANUAL_MOVE_DOWN,
    PRESET_MOVE
};

class UIManager {
public:
    UIManager();

    // Test functions
    void test_idle_animation();
    void test_manual_move_animation(bool is_moving_up);
    void test_preset_move_animation();

    // START ANIMATION: Moving UP
    void start_move_up_animation();

    // START ANIMATION: Moving DOWN
    void start_move_down_animation();

    // STOP ANIMATION
    void stop_move_animation();

private:
    void init_styles();
    void configure_and_start_animation(const char* symbol);

    // Animation helpers
    static void arrow_animation_cb(void *var, int32_t v);
    void start_arrow_animation(lv_obj_t* arrow, bool up);
    void update_height_text(float height);

    // UI elements
    lv_style_t style_big_text_;
    lv_style_t style_small_text_;

    lv_obj_t *height_label_;
    lv_obj_t *unit_label_;
    lv_obj_t *progress_bar_;

    lv_obj_t* arrow_container_;   // The invisible box holding the arrows
    lv_obj_t* main_arrow_lbl_;    // The bright, front arrow
    lv_obj_t* trail_arrow_1_lbl_; // The mediumfade trail
    lv_obj_t* trail_arrow_2_lbl_; // The lightest fade trail

    lv_anim_t up_down_anim_;      // Handle to control the animation
    bool is_animating_ = false;   // State tracker

    // Styles for the cyan color effect
    lv_style_t style_cyan_bright_;
    lv_style_t style_cyan_medium_;
    
    lv_style_t style_cyan_light_;
#ifdef CONFIG_LV_COLOR_16_SWAP // If 16-bit color with byte swap is enabled, cyan is red and vice versa
    lv_color_t cyan = lv_palette_main(LV_PALETTE_RED);
#else
    lv_color_t cyan = lv_palette_main(LV_PALETTE_CYAN);
#endif
    
};
