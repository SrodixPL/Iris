#pragma once
#include "core/menu/menu_state.hpp"
#include <string_view>
namespace iris::ui::feature_switches {
// Translate only controls with an implemented behavior and their own gate.
void project(MenuState& state);
cosmetics::Configuration runtime_configuration(const MenuState& state);
bool wired(std::string_view path);
bool enabled(const MenuState& state,std::string_view path);
void set_toggle(MenuState& state,std::string_view path,bool enabled);
void reconcile(const MenuState& before,const MenuState& requested,const MenuState& actual,MenuState& target,bool publish);
}
