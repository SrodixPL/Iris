#include "core/menu/inventory_navigation.hpp"
#include "core/menu/inventory_model.hpp"
#include <algorithm>
namespace iris::ui {
namespace {
int slot_id(int definition) {
  return (definition == 0 || cosmetics::is_glove(definition)) ? glove_slot
         : cosmetics::is_knife(definition) ? knife_slot
                                           : definition;
}
void snapshot(std::vector<InventorySlot> &out,
              std::span<const cosmetics::Selection> source) {
  out.clear();
  for (const auto &entry : source) {
    const int id = slot_id(entry.definition);
    if (std::none_of(out.begin(), out.end(),
                     [&](const auto &slot) { return slot.slot == id; }))
      out.push_back({id, entry.definition});
  }
  std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
    const auto group = [](int d) {
      return (d == 0 || cosmetics::is_glove(d)) ? 1 : cosmetics::is_knife(d) ? 0 : weapon_group(d) + 2;
    };
    return group(a.definition) < group(b.definition);
  });
}
}
void refresh_inventory_loadout(MenuRuntime &state) {
  auto &navigation = state.inventory_navigation;
  const auto roster = cosmetics::equipped_loadout();
  navigation.roster_revision = roster.revision;
  navigation.next_roster_poll = ImGui::GetTime() + 1;
  for (int team = 0; team < 2; ++team) {
    auto &slots = navigation.slots[team];
    slots.clear();
    navigation.sources[team] = InventoryRosterSource::Unavailable;
    if (roster.available) {
      std::vector<cosmetics::Selection> equipped;
      for (const auto& slot : roster.teams[team]) {
        cosmetics::Selection entry; entry.definition = slot.definition; equipped.push_back(entry);
      }
      snapshot(slots, equipped);
      navigation.sources[team] = InventoryRosterSource::Game;
    }
  }
  for (auto& slots : navigation.slots) slots.insert(slots.begin(), {agent_slot, 0});
  if (navigation.selected_slot && !selected_inventory_slot(state)) {
    navigation.selected_slot.reset();
    navigation.restore_scroll = true;
  }
  navigation.observed_runtime_available = state.runtime_status.available;
  navigation.initialized = true;
}
void open_inventory_slot(MenuRuntime &state, const InventorySlot &slot) {
  state.inventory_navigation.route_from_picker =
      (state.inventory.browsing || (!state.apply_queue && state.inventory_navigation.selected_slot.has_value()));
  state.inventory_navigation.route_progress = 0;
  state.inventory_navigation.selected_slot = slot.slot;
  if (slot.slot == agent_slot) {
    state.inventory_slot = agent_slot;
    state.inventory_definition = state.values.cosmetics.agents[state.inventory_team];
    state.agent_drafts[state.inventory_team] = state.inventory_definition;
  } else select_slot(state, slot.slot, slot.definition);
  state.fixture_skin = 0;
  for (const auto &stage :
       state.inventory_navigation.preview_stages[state.inventory_team])
    if (stage.slot == slot.slot &&
        stage.definition == state.inventory_definition)
      state.fixture_skin = stage.paint;
  state.rarity_filter = 0;
  state.skin_search.fill(0);
}
void back_to_inventory_overview(MenuRuntime &state) {
  state.inventory_navigation.route_from_picker = true;
  state.inventory_navigation.route_progress = 0;
  state.inventory_navigation.selected_slot.reset();
  state.inventory_navigation.restore_scroll = true;
}
bool step_inventory_slot(MenuRuntime &state, int direction) {
  const auto &slots = state.inventory_navigation.slots[state.inventory_team];
  const auto *selected = selected_inventory_slot(state);
  if (!selected || slots.size() < 2)
    return false;
  const auto index = static_cast<int>(selected - slots.data());
  const auto next = (index + direction + static_cast<int>(slots.size())) %
                    static_cast<int>(slots.size());
  open_inventory_slot(state, slots[next]);
  return true;
}
const InventorySlot *selected_inventory_slot(const MenuRuntime &state) {
  const auto &navigation = state.inventory_navigation;
  if (!navigation.selected_slot)
    return nullptr;
  const auto &slots = navigation.slots[state.inventory_team];
  const auto found =
      std::find_if(slots.begin(), slots.end(), [&](const auto &s) {
        return s.slot == *navigation.selected_slot;
      });
  return found == slots.end() ? nullptr : &*found;
}
const char *inventory_category(int definition) {
  if (definition == 0 || cosmetics::is_glove(definition) || cosmetics::is_knife(definition))
    return "Equipment";
  return weapon_group(definition) == 0   ? "Pistols"
         : weapon_group(definition) == 1 ? "Mid-Tier"
                                         : "Rifles";
}
bool inventory_slot_preview_only(const MenuRuntime &state,
                           const InventorySlot &slot) {
  for (const auto &stage :
       state.inventory_navigation.preview_stages[state.inventory_team])
    if (stage.slot == slot.slot && stage.paint < 0)
      return true;
  return false;
}
bool inventory_handles_escape(const MenuRuntime &state) {
  return state.selected == Tab::Inventory &&
         state.inventory.browsing;
}
}
