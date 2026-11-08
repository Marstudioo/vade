/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "base_screen.h"

namespace screen {

class ReaderScreen : public BaseScreen {
public:
    using BaseScreen::BaseScreen;

protected:
    void build_content(lv_obj_t* content) override;

private:
    static void reader_changed_cb(lv_observer_t* observer, lv_subject_t* subject);

    void refresh_content();

    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* body_label_ = nullptr;
    lv_obj_t* scroll_container_ = nullptr;
    lv_observer_t* reader_observer_ = nullptr;
    lv_observer_t* font_observer_ = nullptr;
};

} // namespace screen
