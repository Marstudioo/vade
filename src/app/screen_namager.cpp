/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */

// 注意：文件名沿用模板历史命名（screen_namager.cpp），实现的是 ScreenManager。

#include "screen_manager.h"

#include "module_stub_screen.h"
#include "reader_screen.h"
#include "search_screen.h"

namespace app {
namespace {

void flush_requested_page_async(void* user_data) {
    auto* manager = static_cast<ScreenManager*>(user_data);
    if (manager) {
        manager->flush_requested_page();
    }
}

void current_page_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* manager = static_cast<ScreenManager*>(lv_observer_get_target(observer));
    if (!manager) {
        return;
    }

    manager->request_page(static_cast<model::AppPage>(lv_subject_get_int(subject)));
}

} // namespace

ScreenManager::ScreenManager(viewmodel::BaseViewModel& view_model, AssetManager& assets)
    : view_model_(view_model), assets_(assets) {}

ScreenManager::~ScreenManager() {
    lv_async_call_cancel(flush_requested_page_async, this);

    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
}

void ScreenManager::start() {
    if (page_observer_) {
        return;
    }

    page_observer_ = lv_subject_add_observer_with_target(view_model_.current_page_subject(),
                                                         current_page_observer,
                                                         this,
                                                         nullptr);
}

std::unique_ptr<screen::BaseScreen> ScreenManager::create_screen(model::AppPage page) {
    switch (page) {
        case model::AppPage::Search:
            return std::make_unique<screen::SearchScreen>(view_model_, assets_);
        case model::AppPage::Reader:
            return std::make_unique<screen::ReaderScreen>(view_model_, assets_);
        case model::AppPage::Chat:
            return std::make_unique<screen::ModuleStubScreen>(
                view_model_, assets_, "\u5BF9\u8BDD",  // 对话
                "M2 \u5F00\u53D1\uFF1AAgent \u9002\u914D\u5C42\uFF08DSH/Hermes/Anthropic/\u81EA\u5B9A\u4E49\uFF09\u3001\n"
                "\u6C14\u6CE1\u6D41 + \u4E09\u6001\u6E32\u67D3 + \u5411\u4E0A\u8F7D\u5165\u5386\u53F2\u3002");
        case model::AppPage::Tasks:
            return std::make_unique<screen::ModuleStubScreen>(
                view_model_, assets_, "\u4EFB\u52A1",  // 任务
                "M2 \u5F00\u53D1\uFF1AKanban \u901F\u89C8\u3001\n\u624B\u52A8\u5237\u65B0\u3001\u72B6\u6001\u5206\u7EC4\u3002");
        case model::AppPage::Capture:
            return std::make_unique<screen::ModuleStubScreen>(
                view_model_, assets_, "\u91C7\u96C6",  // 采集
                "M3 \u5F00\u53D1\uFF1A\u62CD\u7167\uFF08libcamera\uFF09\u3001\n\u8BED\u97F3\u901F\u8BB0\uFF08sherpa-onnx\uFF09\u3001\u624B\u52A8\u540C\u6B65\u3002");
        case model::AppPage::Settings:
            return std::make_unique<screen::ModuleStubScreen>(
                view_model_, assets_, "\u8BBE\u7F6E",  // 设置
                "M3 \u5F00\u53D1\uFF1A\u5916\u89C2/\u7F51\u7EDC/\u9690\u79C1/\u6570\u636E/\u7CFB\u7EDF\u3002\n"
                "\u540E\u7AEF\u7BA1\u7406\u5668 + \u80CC\u666F\u8272\u9884\u8BBE\u3002");
        default:
            return std::make_unique<screen::SearchScreen>(view_model_, assets_);
    }
}

void ScreenManager::request_page(model::AppPage page) {
    requested_page_ = page;

    if (page_switch_scheduled_) {
        return;
    }

    page_switch_scheduled_ = lv_async_call(flush_requested_page_async, this) == LV_RESULT_OK;
}

void ScreenManager::flush_requested_page() {
    page_switch_scheduled_ = false;

    if (has_loaded_page_ && requested_page_ == loaded_page_) {
        return;
    }

    load_screen(create_screen(requested_page_));
    loaded_page_ = requested_page_;
    has_loaded_page_ = true;
}

lv_obj_t* ScreenManager::current_screen() const {
    return current_screen_ ? current_screen_->root() : nullptr;
}

void ScreenManager::load_screen(std::unique_ptr<screen::BaseScreen> next_screen) {
    if (!next_screen) {
        return;
    }
    next_screen->init();
    if (!next_screen->root()) {
        return;
    }

    lv_screen_load(next_screen->root());
    current_screen_ = std::move(next_screen);
}

} // namespace app
