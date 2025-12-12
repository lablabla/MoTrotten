#include "ui_manager.hpp"
#include <stdio.h>

UIManager::UIManager() {
    // Initialize styles
    lv_style_init(&style_big_text_);
    lv_style_set_text_font(&style_big_text_, &lv_font_montserrat_48);

    lv_style_init(&style_small_text_);
    lv_style_set_text_font(&style_small_text_, &lv_font_montserrat_24);

    // Create the shared UI elements
    height_label_ = lv_label_create(lv_scr_act());
    lv_obj_add_style(height_label_, &style_big_text_, 0);

    unit_label_ = lv_label_create(lv_scr_act());
    lv_obj_add_style(unit_label_, &style_small_text_, 0);
    lv_label_set_text(unit_label_, "cm");

    progress_bar_ = lv_bar_create(lv_scr_act());
    lv_obj_set_size(progress_bar_, 200, 20);
    lv_obj_center(progress_bar_);
    lv_obj_align(progress_bar_, LV_ALIGN_LEFT_MID, 50, 50);

    static lv_style_t style_indic;
    lv_style_init(&style_indic);
    lv_style_set_bg_opa(&style_indic, LV_OPA_COVER);
    lv_style_set_bg_color(&style_indic, cyan);
    lv_obj_add_style(progress_bar_, &style_indic, LV_PART_INDICATOR);

    init_styles();

    arrow_container_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(arrow_container_); // Make it invisible (no border, no bg)
    lv_obj_set_size(arrow_container_, 320, 240);
    lv_obj_center(arrow_container_); // Center the container itself
    
    // 1. The lightest trail (furthest back)
    trail_arrow_2_lbl_ = lv_label_create(arrow_container_);
    lv_obj_add_style(trail_arrow_2_lbl_, &style_cyan_light_, 0);

    // 2. The medium trail
    trail_arrow_1_lbl_ = lv_label_create(arrow_container_);
    lv_obj_add_style(trail_arrow_1_lbl_, &style_cyan_medium_, 0);

    // 3. The main bright arrow (on top)
    main_arrow_lbl_ = lv_label_create(arrow_container_);
    lv_obj_add_style(main_arrow_lbl_, &style_cyan_bright_, 0);

    // Initially hide the entire container
    lv_obj_add_flag(arrow_container_, LV_OBJ_FLAG_HIDDEN);
    
    // Initialize animation struct to zero to avoid garbage data
    lv_memset_00(&up_down_anim_, sizeof(lv_anim_t));

    // Initially hide elements that are not part of the idle screen
    lv_obj_add_flag(progress_bar_, LV_OBJ_FLAG_HIDDEN);
}

void UIManager::test_idle_animation() {
    lv_obj_clear_flag(height_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(unit_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(progress_bar_, LV_OBJ_FLAG_HIDDEN);

    static float height = 95.0;
    height += 0.1;
    if (height > 105.0) height = 95.0;

    update_height_text(height);

}

void UIManager::test_manual_move_animation(bool is_moving_up) {
    if (is_moving_up) {
        start_move_up_animation();
    } else {
        start_move_down_animation();
    }
    
    static float height = 95.0;
    update_height_text(height);
}

void UIManager::test_preset_move_animation() {
    lv_obj_clear_flag(height_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(unit_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(progress_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(arrow_container_, LV_OBJ_FLAG_HIDDEN);
    
    static int progress = 0;
    progress = (progress + 1) % 101;
    lv_bar_set_value(progress_bar_, progress, LV_ANIM_ON);

    update_height_text(95.0 + (progress / 100.0) * 10.0);
}

void UIManager::init_styles() {

    const lv_font_t* arrow_font = &lv_font_montserrat_48;

    lv_style_init(&style_cyan_bright_);
    lv_style_set_text_color(&style_cyan_bright_, cyan);
    lv_style_set_text_opa(&style_cyan_bright_, LV_OPA_COVER); // 100% opaque
    lv_style_set_text_font(&style_cyan_bright_, arrow_font);

    lv_style_init(&style_cyan_medium_);
    lv_style_set_text_color(&style_cyan_medium_, cyan);
    lv_style_set_text_opa(&style_cyan_medium_, LV_OPA_60);   // 60% opaque
    lv_style_set_text_font(&style_cyan_medium_, arrow_font);

    lv_style_init(&style_cyan_light_);
    lv_style_set_text_color(&style_cyan_light_, cyan);
    lv_style_set_text_opa(&style_cyan_light_, LV_OPA_30);    // 30% opaque
    lv_style_set_text_font(&style_cyan_light_, arrow_font);
}

void UIManager::start_move_up_animation() {
    // Only start if not already animating to avoid resetting the timeline
    if (is_animating_) {
        // Optional: If we were moving down, we might want to switch symbols instantly
        lv_label_set_text(main_arrow_lbl_, LV_SYMBOL_UP);
        lv_label_set_text(trail_arrow_1_lbl_, LV_SYMBOL_UP);
        lv_label_set_text(trail_arrow_2_lbl_, LV_SYMBOL_UP);
        return;
    }
    
    configure_and_start_animation(LV_SYMBOL_UP);
}

void UIManager::start_move_down_animation() {
    if (is_animating_) {
        // Optional: Switch symbols instantly if direction changed
        lv_label_set_text(main_arrow_lbl_, LV_SYMBOL_DOWN);
        lv_label_set_text(trail_arrow_1_lbl_, LV_SYMBOL_DOWN);
        lv_label_set_text(trail_arrow_2_lbl_, LV_SYMBOL_DOWN);
        return;
    }

    configure_and_start_animation(LV_SYMBOL_DOWN);
}

void UIManager::stop_move_animation() {
    if (!is_animating_) return;

    // Stop animation
    lv_anim_del(arrow_container_, arrow_animation_cb);
    
    // Reset position to exact center (0) so it doesn't get stuck offset
    lv_obj_set_y(arrow_container_, 0);

    // Hide everything
    lv_obj_add_flag(arrow_container_, LV_OBJ_FLAG_HIDDEN);
    
    is_animating_ = false;
}

void UIManager::configure_and_start_animation(const char* symbol) {
    // 1. Set Symbols
    lv_label_set_text(main_arrow_lbl_, symbol);
    lv_label_set_text(trail_arrow_1_lbl_, symbol);
    lv_label_set_text(trail_arrow_2_lbl_, symbol);

    // 2. Determine Direction Physics
    int32_t start_y, end_y;
    
    lv_coord_t arrows_offset = 90;
    if (strcmp(symbol, LV_SYMBOL_UP) == 0) {
        lv_obj_align(main_arrow_lbl_,    LV_ALIGN_CENTER, arrows_offset, -25); // Top
        lv_obj_align(trail_arrow_1_lbl_, LV_ALIGN_CENTER, arrows_offset, 0);   // Middle
        lv_obj_align(trail_arrow_2_lbl_, LV_ALIGN_CENTER, arrows_offset, 25);  // Bottom

        // Animation Physics: Move Up
        start_y = 20; 
        end_y = -20;
    } else {
        lv_obj_align(trail_arrow_2_lbl_, LV_ALIGN_CENTER, arrows_offset, -25); // Top
        lv_obj_align(trail_arrow_1_lbl_, LV_ALIGN_CENTER, arrows_offset, 0);   // Middle
        lv_obj_align(main_arrow_lbl_,    LV_ALIGN_CENTER, arrows_offset, 25);  // Bottom

        // Animation Physics: Move Down
        start_y = -20;
        end_y = 20;
    }

    lv_obj_clear_flag(arrow_container_, LV_OBJ_FLAG_HIDDEN);

    // 3. Setup Animation
    lv_anim_init(&up_down_anim_);
    lv_anim_set_var(&up_down_anim_, arrow_container_);
    lv_anim_set_exec_cb(&up_down_anim_, arrow_animation_cb);

    // MOTION: One way flow
    lv_anim_set_values(&up_down_anim_, start_y, end_y);
    lv_anim_set_time(&up_down_anim_, 1000); // 1 second duration
    
    lv_anim_set_playback_time(&up_down_anim_, 0); 
    
    // PATH: Linear looks best for continuous flow, or ease_in
    lv_anim_set_path_cb(&up_down_anim_, lv_anim_path_linear);
    
    lv_anim_set_repeat_count(&up_down_anim_, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&up_down_anim_);
    
    is_animating_ = true;
}

void UIManager::arrow_animation_cb(void *var, int32_t v) {
    lv_obj_set_y((lv_obj_t*)var, v);
}

void UIManager::start_arrow_animation(lv_obj_t* arrow, bool up) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arrow);
    lv_anim_set_exec_cb(&a, arrow_animation_cb);
    
    int32_t start_y = lv_obj_get_y(arrow);
    int32_t end_y = start_y + (up ? -10 : 10);

    lv_anim_set_values(&a, start_y, end_y);
    lv_anim_set_time(&a, 500);
    lv_anim_set_playback_time(&a, 500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

void UIManager::update_height_text(float height) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%.1f", height);
    lv_label_set_text(height_label_, buf);
    
    // Center height label logic
    lv_obj_align(height_label_, LV_ALIGN_LEFT_MID, 50, 0);
    
    // Dynamic unit label position
    lv_coord_t x_offset = height > 100.0 ? -95 : -110;
    lv_obj_align(unit_label_, LV_ALIGN_BOTTOM_RIGHT, x_offset, -95);
}
