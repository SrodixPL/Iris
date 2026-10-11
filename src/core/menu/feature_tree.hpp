#pragma once
#include "core/menu/menu_state.hpp"
namespace iris::ui::feature_tree {
// Canonical controls persist locally in previews and dispatch through ApplyQueue in game.
void render(MenuRuntime& state);
void player_selector(MenuRuntime& state,float width);
void evaluate(MenuRuntime& state);
void inventory_options(MenuRuntime& state);
// Search the same catalog that owns the rendered controls and reveal the selected row.
bool search_results(MenuRuntime& state,bool select_first=false);
int search_result_count(const MenuRuntime& state);
}
