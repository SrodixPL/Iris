#pragma once
#include "core/menu/menu_state.hpp"
namespace iris::ui {
bool reset_inventory_slot(MenuRuntime &state);
bool remove_inventory_skin(MenuRuntime &state);
bool copy_inventory_slot(MenuRuntime &state);
bool commit_inventory_selection(MenuRuntime &state);
void randomize_inventory_seed(MenuRuntime &state);
}
