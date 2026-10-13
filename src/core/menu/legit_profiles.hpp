#pragma once
#include "core/menu/menu_state.hpp"
namespace iris::ui::legit_profiles {
// Caller owns the page's scrolling child. Edits use the existing ApplyQueue path.
void render(MenuRuntime& state);
void selector(MenuRuntime& state,float width);
// Global, category and weapon hotkeys run on hidden frames with stable catalog IDs.
void evaluate(MenuRuntime& state);
// Scoped IDs preserve the existing global catalog identities.
std::string binding_path(const char* path,int category=-1,int weapon=0);
bool known_scoped_binding(ImGuiID id);
// Canonical paths shared by catalog/search, UI and hotkeys.
const char* toggle_path(const char* key) noexcept;
const char* number_path(const char* key) noexcept;
const char* choice_path(const char* key) noexcept;
}
