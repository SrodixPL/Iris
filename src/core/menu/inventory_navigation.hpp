#pragma once
#include "core/menu/menu_state.hpp"
namespace iris::ui {
// UI-only glue: a roster snapshot has separate ownership from cosmetic
// overrides.
void refresh_inventory_loadout(MenuRuntime &state);
void open_inventory_slot(MenuRuntime &state, const InventorySlot &slot);
void back_to_inventory_overview(MenuRuntime &state);
bool step_inventory_slot(MenuRuntime &state, int direction);
const InventorySlot *selected_inventory_slot(const MenuRuntime &state);
const char *inventory_category(int definition);
bool inventory_slot_preview_only(const MenuRuntime &state, const InventorySlot &slot);
bool inventory_handles_escape(const MenuRuntime &state);
void inventory_overview(MenuRuntime &state);
}
