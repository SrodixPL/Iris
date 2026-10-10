#pragma once
#include "core/menu/menu_state.hpp"
namespace iris::ui {
// UI adapters only: catalog filtering and staging, never feature submission.
void refresh_inventory_skins(MenuRuntime &state);
void select_inventory_type(MenuRuntime &state, int definition);
void select_inventory_finish(MenuRuntime &state, int paint);
}
