#pragma once
#include "core/menu/menu_state.hpp"
namespace iris::ui {
inline constexpr int knife_slot=-1,glove_slot=-2,agent_slot=-3;
SkinEditor& skin_editor(MenuRuntime& state,int team,int slot,int definition);
const cosmetics::Selection* applied_selection(const MenuState& state,int team,int slot);
void select_skin(MenuRuntime& state,int paint);
bool commit_skin(MenuRuntime& state);
void stage_skin(MenuState& state,int team,int slot,const cosmetics::Selection& selection);
void select_slot(MenuRuntime& state,int slot,int definition);
ImU32 rarity_color(int definition,int paint);
int rarity_level(int definition,int paint);
int weapon_group(int definition);
}
