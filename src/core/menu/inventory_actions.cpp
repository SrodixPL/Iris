#include "core/menu/inventory_actions.hpp"
#include "core/menu/inventory_model.hpp"
#include "core/menu/inventory_navigation.hpp"
#include <algorithm>
#include <random>
namespace iris::ui {
bool commit_inventory_selection(MenuRuntime &state) {
  if (state.fixture_skin < 0)
    return false;
  const int counter =
      skin_editor(state, state.inventory_team, state.inventory_slot,
                 state.inventory_definition)
          .cached_counter;
  if (!commit_skin(state))
    return false;
  for (int team = 0; team < 2; ++team) {
    if (team != state.inventory_team && !state.values.inventory.both_teams)
      continue;
    std::erase_if(
        state.inventory_navigation.preview_stages[team],
        [&](const auto &stage) { return stage.slot == state.inventory_slot; });
    skin_editor(state, team, state.inventory_slot, state.inventory_definition)
        .cached_counter = counter;
  }
  return true;
}
bool remove_inventory_skin(MenuRuntime &state) {
  auto previous = state.values;
  if (state.inventory_slot == glove_slot) {
    state.values.inventory.gloves[state.inventory_team].enabled = false;
    std::erase_if(state.values.inventory.editors[state.inventory_team], [&](const auto& d){return d.slot==state.inventory_slot;});
    if (submit_features(state)) return true;
    state.values = std::move(previous); return false;
  }
  auto &entries = state.values.cosmetics.loadouts[state.inventory_team];
  std::erase_if(entries, [&](const auto &entry) {
    return state.inventory_slot == knife_slot
               ? cosmetics::is_knife(entry.definition)
               : entry.definition == state.inventory_slot;
  });
  std::erase_if(state.values.inventory.editors[state.inventory_team], [&](const auto& d){return d.slot==state.inventory_slot;});
  if (submit_features(state))
    return true;
  state.values = std::move(previous);
  return false;
}
bool reset_inventory_slot(MenuRuntime &state) {
  const auto *slot = selected_inventory_slot(state);
  if (!slot ||
      !remove_inventory_skin(state))
    return false;
  auto &editor =
      skin_editor(state, state.inventory_team, slot->slot, slot->definition);
  editor.value = cosmetics::Selection{};
  editor.value.definition = slot->definition;
  editor.cached_counter = 0;
  state.inventory_definition = slot->definition;
  std::erase_if(state.inventory_navigation.preview_stages[state.inventory_team],
                [&](const auto &stage) { return stage.slot == slot->slot; });
  state.fixture_skin = 0;
  state.grid_dirty = true;
  return true;
}
bool copy_inventory_slot(MenuRuntime &state) {
  const auto *source = applied_selection(state.values, state.inventory_team,
                                         state.inventory_slot);
  if (!source)
    return false;
  const auto value = *source;
  const auto previous = state.values;
  const int team=state.inventory_team;
  bool success=false;
  try{stage_skin(state.values,1-team,state.inventory_slot,value);success=submit_features(state);}
  catch(const std::exception& error){state.error=error.what();}
  if(!success)state.values=previous;
  else std::erase_if(state.inventory_navigation.preview_stages[1-team],[&](const auto& stage){return stage.slot==state.inventory_slot;});
  return success;
}
void randomize_inventory_seed(MenuRuntime &state) {
  static std::minstd_rand rng{0x4e575549};
  skin_editor(state, state.inventory_team, state.inventory_slot,
             state.inventory_definition)
      .value.seed = static_cast<int>(rng() % 1001);
  commit_inventory_selection(state);
}
}
