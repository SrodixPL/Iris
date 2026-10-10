#pragma once
#include "core/menu/menu_state.hpp"
namespace iris::ui {
void evaluate_bindings(MenuRuntime& state);
void commit_feature_edit(MenuRuntime& state,const MenuState& before);
}
