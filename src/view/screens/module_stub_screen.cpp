/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#include "module_stub_screen.h"

#include "bindings.h"

namespace screen {

ModuleStubScreen::ModuleStubScreen(viewmodel::BaseViewModel& view_model,
                                   app::AssetManager& assets,
                                   const char* module_name,
                                   const char* plan_text)
    : BaseScreen(view_model, assets), module_name_(module_name), plan_text_(plan_text) {}

void ModuleStubScreen::build_content(lv_obj_t* content) {
    auto* name = lv_label_create(content);
    lv_label_set_text(name, module_name_);
    auto* name_font = assets().load_standard_font(18, app::StandardFontWeight::Bold);
    if (name_font) {
        lv_obj_set_style_text_font(name, name_font, 0);
    }
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 14);
    reactive::bind_theme(name, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* badge = lv_label_create(content);
    lv_label_set_text(badge, "\u9AA8\u67B6\u5DF2\u5C31\u4F4D \u00B7 \u529F\u80FD\u5F00\u53D1\u4E2D");  // 骨架已就位 · 功能开发中
    auto* badge_font = assets().load_standard_font(13, app::StandardFontWeight::Regular);
    if (badge_font) {
        lv_obj_set_style_text_font(badge, badge_font, 0);
    }
    lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_text_opa(badge, LV_OPA_60, 0);
    reactive::bind_theme(badge, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* plan = lv_label_create(content);
    lv_label_set_text(plan, plan_text_);
    if (badge_font) {
        lv_obj_set_style_text_font(plan, badge_font, 0);
    }
    lv_label_set_long_mode(plan, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(plan, 280);
    lv_obj_align(plan, LV_ALIGN_TOP_MID, 0, 66);
    lv_obj_set_style_text_opa(plan, LV_OPA_50, 0);
    reactive::bind_theme(plan, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
}

} // namespace screen
