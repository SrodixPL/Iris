#include "core/menu/inventory_picker.hpp"
#include "core/menu/inventory_actions.hpp"
#include "core/menu/inventory_model.hpp"
#include "core/menu/config_io.hpp"
#include <algorithm>
namespace iris::ui {
namespace {
int finish_rarity(const cosmetics::Finish &finish) {
  return finish.paint < 0 ? (-finish.paint) % 6 + 1
                          : rarity_level(finish.definition, finish.paint);
}
}
void refresh_inventory_skins(MenuRuntime &state) {
  state.visible_skins.clear();
  const auto accept = [&](const cosmetics::Finish &finish) {
    if (text_matches(finish.name, state.skin_search.data()) &&
        (!state.rarity_filter || finish_rarity(finish) == state.rarity_filter))
      state.visible_skins.push_back(&finish);
  };
  for (const auto &finish : cosmetics::catalog())
    if (finish.definition == state.inventory_definition) {
      accept(finish);
    }
  state.grid_dirty = false;
  state.grid_scroll_reset = true;
}
void select_inventory_type(MenuRuntime &state, int definition) {
  if (state.inventory_slot == knife_slot) {
    const auto *weapon = cosmetics::find_weapon(definition);
    if (!weapon || !weapon->knife ||
        !(weapon->teams & (1u << state.inventory_team)))
      return;
  } else if (state.inventory_slot != glove_slot || !cosmetics::is_glove(definition) ||
             !(cosmetics::find_weapon(definition)->teams & (1u << state.inventory_team))) {
    return;
  }
  if (state.inventory_definition == definition)
    return;
  auto &editor = skin_editor(state, state.inventory_team, state.inventory_slot,
                           state.inventory_definition);
  editor.value.definition = definition;
  state.inventory_definition = definition;
  select_inventory_finish(state, 0);
  state.grid_dirty = true;
}
void select_inventory_finish(MenuRuntime &state, int paint) {
  if (paint < 0) return;
  auto &previews =
      state.inventory_navigation.preview_stages[state.inventory_team];
  std::erase_if(previews, [&](const auto &entry) {
    return entry.slot == state.inventory_slot;
  });
  state.fixture_skin = 0;
  select_skin(state, paint);
  commit_inventory_selection(state);
}
}
