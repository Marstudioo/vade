/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "base_screen.h"

namespace screen {

// Chat/Tasks/Capture/Settings 的占位骨架：M2/M3 逐个替换为真功能页。
class ModuleStubScreen : public BaseScreen {
public:
    ModuleStubScreen(viewmodel::BaseViewModel& view_model,
                     app::AssetManager& assets,
                     const char* module_name,
                     const char* plan_text);

protected:
    void build_content(lv_obj_t* content) override;

private:
    const char* module_name_;
    const char* plan_text_;
};

} // namespace screen
