#include "ui_manager.hpp"
#include <stdio.h>

UIManager::UIManager() {
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
    // Initialize styles
    lv_style_init(&style_big_text_);
    lv_style_set_text_font(&style_big_text_, &lv_font_montserrat_48);

    lv_style_init(&style_small_text_);
    lv_style_set_text_font(&style_small_text_, &lv_font_montserrat_24);

    // Create the shared UI elements
    height_label_ = lv_label_create(lv_scr_act());
    lv_obj_add_flag(height_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_style(height_label_, &style_big_text_, 0);

    unit_label_ = lv_label_create(lv_scr_act());
    lv_obj_add_flag(unit_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_style(unit_label_, &style_small_text_, 0);
    lv_label_set_text(unit_label_, "cm");

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
}

void UIManager::test_idle_animation() {

    static float height = 95.0;
    height += 0.1;
    if (height > 105.0) height = 95.0;

    update_height_text(height);

}

void UIManager::test_manual_move_animation(bool is_moving_up) {
    static float height = 95.0;
    if (is_moving_up) {
        height += 0.1;
        if (height > 105.0) height = 95.0;
        start_move_up_animation();
    } else {        
        height -= 0.1;
        if (height < 95.0) height = 105.0;
        start_move_down_animation();
    }
    
    update_height_text(height);
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

void UIManager::show_idle_state(float height) {
    stop_move_animation();

    update_height_text(height);
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
    
    lv_coord_t arrows_offset = 100;
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
    lv_obj_clear_flag(height_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(unit_label_, LV_OBJ_FLAG_HIDDEN);
    char buf[20];
    snprintf(buf, sizeof(buf), "%.1f", height);
    lv_label_set_text(height_label_, buf);
    
    // Center height label logic
    lv_obj_align(height_label_, LV_ALIGN_LEFT_MID, 50, 0);
    
    // Dynamic unit label position
    lv_coord_t x_offset = height > 100.0 ? -95 : -110;
    lv_obj_align(unit_label_, LV_ALIGN_BOTTOM_RIGHT, x_offset, -95);
}

void UIManager::anim_opa_cb(void* var, int32_t v) {
    // Cast the raw pointer back to an LVGL object
    lv_obj_t* obj = (lv_obj_t*)var;
    // Set the text opacity style property based on the animation value (v)
    lv_obj_set_style_text_opa(obj, (lv_opa_t)v, 0);
}

void UIManager::startup_sequence_end_cb(lv_anim_t* a) {
    UIManager* mgr = (UIManager*)a->user_data;
    
    // Get the container (parent of the last letter)
    lv_obj_t* last_letter = (lv_obj_t*)a->var;
    lv_obj_t* container = lv_obj_get_parent(last_letter);

    for(auto* lbl : mgr->letter_labels_) {
        lv_obj_remove_local_style_prop(lbl, LV_STYLE_TEXT_OPA, 0);
    }

    // Create manual Fade Out animation
    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, container);
    lv_anim_set_exec_cb(&fade_anim, anim_opa_cb); // Use the same OPA helper
    lv_anim_set_values(&fade_anim, LV_OPA_COVER, LV_OPA_TRANSP); // Fade to transparent
    lv_anim_set_time(&fade_anim, 500);
    lv_anim_set_delay(&fade_anim, 1000); // Wait 1 second before fading out
    
    // Pass 'mgr' to the final callback
    fade_anim.user_data = (void*)mgr;
    lv_anim_set_ready_cb(&fade_anim, final_cleanup_cb);
    
    lv_anim_start(&fade_anim);
}

void UIManager::final_cleanup_cb(lv_anim_t* a) {
    UIManager* mgr = (UIManager*)a->user_data;
    lv_obj_t* container = (lv_obj_t*)a->var;
    
    // Delete the UI elements
    lv_obj_del(container);
    
    // Fire the user's callback
    if (mgr->on_startup_finish_) {
        mgr->on_startup_finish_();
    }
}

void UIManager::play_startup_animation(std::function<void()> on_complete) {
    this->on_startup_finish_ = on_complete;
    const char* text = "MoTrotten";
    
    // 1. Create a container to align letters horizontally
    startup_container_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(startup_container_); // Invisible container
    lv_obj_set_size(startup_container_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(startup_container_);
    
    // Use a flex layout to automatically stack letters horizontally
    lv_obj_set_flex_flow(startup_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(startup_container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // Add a little space between letters (optional)
    lv_obj_set_style_pad_column(startup_container_, 2, 0);

    // 2. Create individual labels for each letter
    int len = strlen(text);
    letter_labels_.clear(); // Ensure vector is empty

    for(int i = 0; i < len; i++) {
        lv_obj_t* lbl = lv_label_create(startup_container_);
        // Set text to a single character
        lv_label_set_text_fmt(lbl, "%c", text[i]);
        
        // Use a nice big font and color
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
        // Use the cyan color you like (or white)
        lv_obj_set_style_text_color(lbl, cyan, 0);
        
        // IMPORTANT: Start completely transparent
        lv_obj_set_style_text_opa(lbl, LV_OPA_TRANSP, 0);

        letter_labels_.push_back(lbl);
    }


    // 3. Create staggered animations
    for(int i = 0; i < len; i++) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, letter_labels_[i]);
        // Use our static helper function
        lv_anim_set_exec_cb(&a, anim_opa_cb);

        // Animate Opacity from Transparent (0) to Covered (255)
        lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_time(&a, 800); // Fade duration for one letter

        // THE KEY TRICK: Staggered Delays
        // Delay increases by 150ms for each subsequent letter
        lv_anim_set_delay(&a, i * 150); 
        
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);

        // If this is the very last letter, attach the cleanup callback
        if (i == len - 1) {
            a.user_data = (void*)this;
            lv_anim_set_ready_cb(&a, startup_sequence_end_cb);
        }

        lv_anim_start(&a);
    }
}
