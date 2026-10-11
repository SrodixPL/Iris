#pragma once
#include "core/menu/menu_state.hpp"
namespace iris::ui::legit_profiles {
// Caller owns the page's scrolling child. Edits use the existing ApplyQueue path.
void render(MenuRuntime& state);
void selector(MenuRuntime& state,float width);
// Global-field hotkeys run on hidden frames with stable catalog IDs.
void evaluate(MenuRuntime& state);
// Canonical paths shared by catalog/search, UI and hotkeys.
const char* toggle_path(const char* key) noexcept;
const char* number_path(const char* key) noexcept;
const char* choice_path(const char* key) noexcept;
}
