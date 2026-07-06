#include "ui_manager.hpp"
#include <stdio.h>
#include <cstring>

// ─── init ────────────────────────────────────────────────────────────────────

void UIManager::init() {
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

    init_styles();

    // Height label (large, cyan)
    height_label_ = lv_label_create(lv_scr_act());
    lv_obj_add_style(height_label_, &style_big_text_, 0);
    lv_obj_add_flag(height_label_, LV_OBJ_FLAG_HIDDEN);

    // Unit label (small, white)
    unit_label_ = lv_label_create(lv_scr_act());
    lv_obj_add_style(unit_label_, &style_small_text_, 0);
    lv_label_set_text(unit_label_, "mm");
    lv_obj_add_flag(unit_label_, LV_OBJ_FLAG_HIDDEN);

    // Overlay label (goto target, stall text, calib text, saved text)
    overlay_label_ = lv_label_create(lv_scr_act());
    lv_obj_add_style(overlay_label_, &style_small_text_, 0);
    lv_obj_align(overlay_label_, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_flag(overlay_label_, LV_OBJ_FLAG_HIDDEN);

    // Arrow container + arrows
    arrow_container_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(arrow_container_);
    lv_obj_set_size(arrow_container_, 240, 240);
    lv_obj_center(arrow_container_);

    trail_arrow_2_lbl_ = lv_label_create(arrow_container_);
    lv_obj_add_style(trail_arrow_2_lbl_, &style_cyan_light_, 0);

    trail_arrow_1_lbl_ = lv_label_create(arrow_container_);
    lv_obj_add_style(trail_arrow_1_lbl_, &style_cyan_medium_, 0);

    main_arrow_lbl_ = lv_label_create(arrow_container_);
    lv_obj_add_style(main_arrow_lbl_, &style_cyan_bright_, 0);

    lv_obj_add_flag(arrow_container_, LV_OBJ_FLAG_HIDDEN);
    lv_memset_00(&arrow_anim_, sizeof(arrow_anim_));
}

void UIManager::init_styles() {
    lv_style_init(&style_big_text_);
    lv_style_set_text_font(&style_big_text_, &lv_font_montserrat_48);
    lv_style_set_text_color(&style_big_text_, cyan_);

    lv_style_init(&style_small_text_);
    lv_style_set_text_font(&style_small_text_, &lv_font_montserrat_24);
    lv_style_set_text_color(&style_small_text_, lv_color_white());

    const lv_font_t* arrow_font = &lv_font_montserrat_48;

    lv_style_init(&style_cyan_bright_);
    lv_style_set_text_color(&style_cyan_bright_, cyan_);
    lv_style_set_text_opa(&style_cyan_bright_, LV_OPA_COVER);
    lv_style_set_text_font(&style_cyan_bright_, arrow_font);

    lv_style_init(&style_cyan_medium_);
    lv_style_set_text_color(&style_cyan_medium_, cyan_);
    lv_style_set_text_opa(&style_cyan_medium_, LV_OPA_60);
    lv_style_set_text_font(&style_cyan_medium_, arrow_font);

    lv_style_init(&style_cyan_light_);
    lv_style_set_text_color(&style_cyan_light_, cyan_);
    lv_style_set_text_opa(&style_cyan_light_, LV_OPA_30);
    lv_style_set_text_font(&style_cyan_light_, arrow_font);
}

// ─── state transitions ───────────────────────────────────────────────────────

void UIManager::show_startup(std::function<void()> on_complete) {
    on_startup_finish_ = on_complete;
    const char* text = "MoTrotten";

    startup_container_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(startup_container_);
    lv_obj_set_size(startup_container_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(startup_container_);
    lv_obj_set_flex_flow(startup_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(startup_container_,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(startup_container_, 2, 0);

    int len = (int)strlen(text);
    letter_labels_.clear();

    for (int i = 0; i < len; i++) {
        lv_obj_t* lbl = lv_label_create(startup_container_);
        lv_label_set_text_fmt(lbl, "%c", text[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(lbl, cyan_, 0);
        lv_obj_set_style_text_opa(lbl, LV_OPA_TRANSP, 0);
        letter_labels_.push_back(lbl);
    }

    for (int i = 0; i < len; i++) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, letter_labels_[i]);
        lv_anim_set_exec_cb(&a, anim_opa_cb);
        lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_time(&a, 800);
        lv_anim_set_delay(&a, i * 150);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        if (i == len - 1) {
            a.user_data = this;
            lv_anim_set_ready_cb(&a, startup_letter_done_cb);
        }
        lv_anim_start(&a);
    }
}

void UIManager::show_idle(float height_mm) {
    stop_arrow_anim();
    lv_obj_add_flag(overlay_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
    update_height_label(height_mm);
}

void UIManager::show_moving_up(float height_mm) {
    lv_obj_add_flag(overlay_label_, LV_OBJ_FLAG_HIDDEN);
    update_height_label(height_mm);
    configure_and_start_arrow_anim(LV_SYMBOL_UP);
}

void UIManager::show_moving_down(float height_mm) {
    lv_obj_add_flag(overlay_label_, LV_OBJ_FLAG_HIDDEN);
    update_height_label(height_mm);
    configure_and_start_arrow_anim(LV_SYMBOL_DOWN);
}

void UIManager::show_goto_preset(float current_mm, float target_mm) {
    update_height_label(current_mm);
    char buf[32];
    snprintf(buf, sizeof(buf), "-> %.0f mm", target_mm);
    lv_label_set_text(overlay_label_, buf);
    lv_obj_set_style_text_color(overlay_label_, lv_color_white(), 0);
    lv_obj_clear_flag(overlay_label_, LV_OBJ_FLAG_HIDDEN);
}

void UIManager::show_stalled() {
    stop_arrow_anim();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_make(180, 0, 0), 0);
    lv_label_set_text(overlay_label_, "BLOCKED");
    lv_obj_set_style_text_color(overlay_label_, lv_color_white(), 0);
    lv_obj_align(overlay_label_, LV_ALIGN_CENTER, 0, 40);
    lv_obj_clear_flag(overlay_label_, LV_OBJ_FLAG_HIDDEN);
    // Timer restores background after 2s (app_task transitions state)
    lv_timer_t* t = lv_timer_create(stall_timer_cb, 2000, this);
    lv_timer_set_repeat_count(t, 1);
}

void UIManager::show_calibrating() {
    stop_arrow_anim();
    lv_label_set_text(overlay_label_, "Calibrating...");
    lv_obj_set_style_text_color(overlay_label_, lv_color_white(), 0);
    lv_obj_align(overlay_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(overlay_label_, LV_OBJ_FLAG_HIDDEN);
}

void UIManager::show_saved_confirmation() {
    lv_label_set_text(overlay_label_, "Saved!");
    lv_obj_set_style_text_color(overlay_label_, cyan_, 0);
    lv_obj_align(overlay_label_, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_clear_flag(overlay_label_, LV_OBJ_FLAG_HIDDEN);
    lv_timer_t* t = lv_timer_create([](lv_timer_t* t) {
        auto* mgr = static_cast<UIManager*>(t->user_data);
        lv_obj_add_flag(mgr->overlay_label_, LV_OBJ_FLAG_HIDDEN);
    }, 1500, this);
    lv_timer_set_repeat_count(t, 1);
}

void UIManager::update_height(float height_mm) {
    update_height_label(height_mm);
}

// ─── private helpers ─────────────────────────────────────────────────────────

void UIManager::update_height_label(float height_mm) {
    lv_obj_clear_flag(height_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(unit_label_,   LV_OBJ_FLAG_HIDDEN);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)height_mm);
    lv_label_set_text(height_label_, buf);
    lv_obj_align(height_label_, LV_ALIGN_CENTER, 0, -10);

    lv_obj_align_to(unit_label_, height_label_, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
}

void UIManager::configure_and_start_arrow_anim(const char* symbol) {
    if (is_arrow_animating_) {
        // Just swap the symbol if direction changed
        lv_label_set_text(main_arrow_lbl_,     symbol);
        lv_label_set_text(trail_arrow_1_lbl_,  symbol);
        lv_label_set_text(trail_arrow_2_lbl_,  symbol);
        return;
    }

    lv_label_set_text(main_arrow_lbl_,     symbol);
    lv_label_set_text(trail_arrow_1_lbl_,  symbol);
    lv_label_set_text(trail_arrow_2_lbl_,  symbol);

    bool up = (strcmp(symbol, LV_SYMBOL_UP) == 0);
    lv_coord_t offset_x = 80;

    if (up) {
        lv_obj_align(main_arrow_lbl_,    LV_ALIGN_CENTER, offset_x, -25);
        lv_obj_align(trail_arrow_1_lbl_, LV_ALIGN_CENTER, offset_x, 0);
        lv_obj_align(trail_arrow_2_lbl_, LV_ALIGN_CENTER, offset_x, 25);
    } else {
        lv_obj_align(trail_arrow_2_lbl_, LV_ALIGN_CENTER, offset_x, -25);
        lv_obj_align(trail_arrow_1_lbl_, LV_ALIGN_CENTER, offset_x, 0);
        lv_obj_align(main_arrow_lbl_,    LV_ALIGN_CENTER, offset_x, 25);
    }

    lv_obj_clear_flag(arrow_container_, LV_OBJ_FLAG_HIDDEN);

    lv_anim_init(&arrow_anim_);
    lv_anim_set_var(&arrow_anim_, arrow_container_);
    lv_anim_set_exec_cb(&arrow_anim_, arrow_anim_cb);
    lv_anim_set_values(&arrow_anim_, up ? 20 : -20, up ? -20 : 20);
    lv_anim_set_time(&arrow_anim_, 1000);
    lv_anim_set_path_cb(&arrow_anim_, lv_anim_path_linear);
    lv_anim_set_repeat_count(&arrow_anim_, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&arrow_anim_);

    is_arrow_animating_ = true;
}

void UIManager::stop_arrow_anim() {
    if (!is_arrow_animating_) return;
    lv_anim_del(arrow_container_, arrow_anim_cb);
    lv_obj_set_y(arrow_container_, 0);
    lv_obj_add_flag(arrow_container_, LV_OBJ_FLAG_HIDDEN);
    is_arrow_animating_ = false;
}

// ─── static callbacks ────────────────────────────────────────────────────────

void UIManager::arrow_anim_cb(void* var, int32_t v) {
    lv_obj_set_y(static_cast<lv_obj_t*>(var), v);
}

void UIManager::anim_opa_cb(void* var, int32_t v) {
    lv_obj_set_style_text_opa(static_cast<lv_obj_t*>(var), (lv_opa_t)v, 0);
}

void UIManager::startup_letter_done_cb(lv_anim_t* a) {
    auto* mgr = static_cast<UIManager*>(a->user_data);

    for (auto* lbl : mgr->letter_labels_) {
        lv_obj_remove_local_style_prop(lbl, LV_STYLE_TEXT_OPA, 0);
    }

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, mgr->startup_container_);
    lv_anim_set_exec_cb(&fade, anim_opa_cb);
    lv_anim_set_values(&fade, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&fade, 500);
    lv_anim_set_delay(&fade, 1000);
    fade.user_data = mgr;
    lv_anim_set_ready_cb(&fade, startup_fade_done_cb);
    lv_anim_start(&fade);
}

void UIManager::startup_fade_done_cb(lv_anim_t* a) {
    auto* mgr = static_cast<UIManager*>(a->user_data);
    lv_obj_del(static_cast<lv_obj_t*>(a->var));
    mgr->startup_container_ = nullptr;
    if (mgr->on_startup_finish_) {
        mgr->on_startup_finish_();
    }
}

void UIManager::stall_timer_cb(lv_timer_t* t) {
    auto* mgr = static_cast<UIManager*>(t->user_data);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
    lv_obj_add_flag(mgr->overlay_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(mgr->overlay_label_, LV_ALIGN_BOTTOM_MID, 0, -20);
}
