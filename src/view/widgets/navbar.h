/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"
#include "asset_manager.h"
#include "lvgl.h"

namespace view::widgets {

class NavBar : public BaseWidgets {
public:
    NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~NavBar() override;

    void build();
    void teardown();

private:
    static void button_clicked_cb(lv_event_t* event);
    static void update_icons_cb(lv_observer_t* observer, lv_subject_t* subject);

    void on_button(size_t index);
    void refresh_icons();

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    lv_obj_t* icon_labels_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* buttons_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    lv_observer_t* page_observer_ = nullptr;
    lv_observer_t* theme_observer_ = nullptr;
};

} // namespace view::widgets
