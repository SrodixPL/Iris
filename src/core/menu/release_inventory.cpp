#include "core/menu/apply_queue.hpp"
#include "core/menu/inventory_actions.hpp"
#include "core/menu/inventory_model.hpp"
#include "core/menu/config_io.hpp"
#include "core/menu/feature_switches.hpp"
#include "core/menu/image_cache.hpp"
#include <algorithm>
namespace iris::ui {
namespace {
int route(int definition){return cosmetics::is_glove(definition) ? glove_slot : cosmetics::is_knife(definition) ? knife_slot : definition;}
int equipped_definition(const MenuRuntime& state,int team,int slot){for(const auto& entry:state.equipped_roster.teams[team])if(route(entry.definition)==slot)return entry.definition;return slot==knife_slot ? (team?42:59) : slot>=0 ? slot : 0;}
bool apply_one(MenuRuntime& state,int team,int slot,const ItemDraft& draft) {
  const auto previous=state.values;
  state.inventory_team=team;state.inventory_slot=slot;state.apply_context=std::string(team?"CT":"T")+" / "+(slot==agent_slot ? "Agent" : slot==glove_slot ? "Gloves" : slot==knife_slot ? "Knife" : cosmetics::find_weapon(slot) ? cosmetics::find_weapon(slot)->name : "Item");
  if(slot==agent_slot) {
    state.values.cosmetics.agents[team]=draft.definition;state.values.cosmetics.custom_models[team]=draft.custom_model;
    if(submit_features(state))return true;state.values=previous;return false;
  }
  if(slot==glove_slot && !draft.definition)return remove_inventory_skin(state);
  const int definition=draft.definition ? draft.definition : equipped_definition(state,team,slot);
  state.inventory_definition=definition;
  auto& editor=skin_editor(state,team,slot,definition);editor.value.definition=definition;editor.value.paint=draft.paint;
  editor.value.wear=draft.wear;editor.value.seed=std::clamp(draft.seed,0,1000);editor.value.stattrak=draft.stattrak ? std::max(0,draft.counter) : -1;
  editor.value.name=draft.name;editor.value.enabled=true;editor.cached_counter=std::max(0,draft.counter);state.fixture_skin=0;
  if(commit_inventory_selection(state))return true;state.values=previous;return false;
}
}
bool read_inventory(MenuRuntime& state,int team,int slot,ItemDraft& value) {
  value={};
  if(team<0||team>1)return false;
  if(slot==-4){value.definition=state.values.cosmetics.music_kit;value.applied=value.definition!=0;return true;}
  if(slot==agent_slot){value.definition=state.values.cosmetics.agents[team];value.custom_model=state.values.cosmetics.custom_models[team];value.applied=value.definition!=0 || !value.custom_model.empty();return true;}
  const auto* applied=applied_selection(state.values,team,slot);
  if(applied){value.definition=applied->definition;value.paint=applied->paint;value.seed=applied->seed;value.wear=applied->wear;value.stattrak=applied->stattrak>=0;value.counter=std::max(0,applied->stattrak);value.name=applied->name;value.applied=applied->enabled;
    if(applied->stattrak<0)for(const auto& editor:state.values.inventory.editors[team])if(editor.slot==slot)value.counter=editor.cached_counter;}
  else value.definition=equipped_definition(state,team,slot);
  return true;
}
bool apply_inventory(MenuRuntime& state,int team,int slot,const ItemDraft& value,bool both) {
  if(team<0||team>1)return false;
  const int definition=value.definition?value.definition:equipped_definition(state,team,slot);
  if(both && !inventory_can_mirror(slot,definition)){
    state.error="This item is not available for both teams.";state.inventory.error=state.error;return false;
  }
  const auto previous=state.values;
  try{
    if(slot==-4){
      if(value.definition && !cosmetics::find_music_kit(value.definition))throw std::invalid_argument("Choose a music kit from the current catalog.");
      state.values.cosmetics.music_kit=value.definition;
    }
    else if(slot==agent_slot){state.values.cosmetics.agents[team]=value.definition;state.values.cosmetics.custom_models[team]=value.custom_model;}
    else {
      cosmetics::Selection selection;selection.definition=value.definition?value.definition:equipped_definition(state,team,slot);selection.paint=value.paint;selection.seed=std::clamp(value.seed,0,1000);selection.wear=value.wear;selection.stattrak=value.stattrak?std::max(0,value.counter):-1;selection.name=value.name;selection.enabled=true;
      for(int target=0;target<2;++target){if(target!=team&&!both)continue;
        if(slot==glove_slot&&!value.definition)state.values.inventory.gloves[target].enabled=false;
        else {
          stage_skin(state.values,target,slot,selection);
          const auto* committed=applied_selection(state.values,target,slot);
          auto& editor=skin_editor(state,target,slot,committed->definition);editor.value=*committed;editor.cached_counter=std::max(0,value.counter);
        }
      }
    }
    state.apply_context=std::string(team?"CT":"T")+" / Inventory";
    if(submit_features(state)){state.apply_context.clear();return true;}
  }catch(const std::exception& error){state.error=error.what();state.inventory.error=state.error;}
  state.values=previous;state.apply_context.clear();return false;
}
bool set_both_teams(MenuRuntime& state,int team,int slot,bool enabled) {
  const auto previous=state.values;
  ItemDraft value;
  if(enabled && (!read_inventory(state,team,slot,value)||!inventory_can_mirror(slot,value.definition))){
    state.error="This item is not available for both teams.";return false;
  }
  state.values.inventory.both_teams=enabled;
  feature_switches::set_toggle(state.values,"Misc/Inventory/Apply to both teams",enabled);
  const bool accepted=enabled ? apply_inventory(state,team,slot,value,true) : submit_features(state);
  if(!accepted)state.values=previous;
  return accepted;
}
void bind_inventory(MenuRuntime& state) {
  auto& adapter=state.inventory.adapter;adapter.owner=&state;adapter.game_roster=true;
  adapter.change_side=[](void* owner,int team){auto& state=*static_cast<MenuRuntime*>(owner);state.inventory_team=team;return true;};
  adapter.open_slot=[](void* owner,int team,int slot){auto& state=*static_cast<MenuRuntime*>(owner);state.inventory_team=team;state.inventory_slot=slot;state.inventory_definition=equipped_definition(state,team,slot);state.inventory_navigation.selected_slot=slot;return true;};
  adapter.read_selection=[](void* owner,int team,int slot,ItemDraft& value){return read_inventory(*static_cast<MenuRuntime*>(owner),team,slot,value);};
  adapter.apply=[](void* owner,int team,int slot,const ItemDraft& value,bool both){return apply_inventory(*static_cast<MenuRuntime*>(owner),team,slot,value,both);};
  adapter.resolve_art=[](void* owner,int definition,int paint){auto& state=*static_cast<MenuRuntime*>(owner);return state.images ? state.images->art(definition,paint) : InventoryArt{};};
  adapter.preview=[](void* owner,int definition,int paint){
    auto& state=*static_cast<MenuRuntime*>(owner);
    if(!state.gameplay_wired || !state.apply_queue)return InventoryArt{};
    const auto& draft=state.inventory.detail_draft;cosmetics::Selection selection;selection.definition=definition;selection.paint=paint;selection.wear=draft.wear;selection.seed=draft.seed;selection.stattrak=draft.stattrak?draft.counter:-1;selection.name=draft.name;
    if(!cosmetics::valid(selection))return state.images ? state.images->last_preview(definition) : InventoryArt{};
    const auto& camera=state.inventory.preview_pose;
    state.apply_queue->preview(selection,{camera[0],camera[1],camera[2]});
    if(!state.images)return InventoryArt{};
    return state.preview_image && state.preview_image->selection.definition==selection.definition ? state.images->preview(*state.preview_image) : state.images->last_preview(definition);
  };
  adapter.request_refresh=[](void* owner){auto& state=*static_cast<MenuRuntime*>(owner);if(state.apply_queue)state.apply_queue->refresh();};
  adapter.refresh_loadout=[](void* owner,int team,InventorySide& output) {
    auto& state=*static_cast<MenuRuntime*>(owner);if(!state.equipped_roster.available)return false;
    output={};output.special[0]={agent_slot,0,0,"Agent"};
    output.special[1]={31,31,0,"Zeus",false,cosmetics::find_weapon(31)!=nullptr};output.special[2]={glove_slot,team?5029:5028,0,"Gloves"};output.special[3]={knife_slot,team?42:59,0,"Knife"};output.special[4]={-4,state.values.cosmetics.music_kit,0,"Music Kit"};
    state.inventory_navigation.slots[team].clear();state.inventory_navigation.slots[team].push_back({agent_slot,0});
    for(const auto& equipped:state.equipped_roster.teams[team]) {
      const auto* weapon=cosmetics::find_weapon(equipped.definition);if(!weapon)continue;
      // Release selection adapters address firearm definitions rather than native loadout indices.
      InventoryEntry item;item.slot=route(equipped.definition);item.definition=equipped.definition;item.name=weapon->name;
      state.inventory_navigation.slots[team].push_back({item.slot,item.definition});
      if(item.slot==knife_slot){item.name="Knife";output.special[3]=item;}
      else if(item.slot==glove_slot){item.name="Gloves";output.special[2]=item;}
      else if(equipped.definition==31){item.name="Zeus";output.special[1]=item;}
      else {auto& column=output.weapons[weapon_group(equipped.definition)];if(column.size()<5)column.push_back(std::move(item));}
    }
    for(const auto& item:output.special)if(item.enabled && std::none_of(state.inventory_navigation.slots[team].begin(),state.inventory_navigation.slots[team].end(),[&](const auto& slot){return slot.slot==item.slot;}))state.inventory_navigation.slots[team].push_back({item.slot,item.definition});
    return true;
  };
  adapter.slot_action=[](void* owner,int team,int slot,int action) {
    auto& state=*static_cast<MenuRuntime*>(owner);const int old_team=state.inventory_team,old_slot=state.inventory_slot,old_definition=state.inventory_definition;const auto old_selected=state.inventory_navigation.selected_slot;
    state.inventory_team=team;state.inventory_slot=slot;state.inventory_definition=equipped_definition(state,team,slot);
    state.inventory_navigation.selected_slot=slot;state.apply_context=std::string(team?"CT":"T")+" / slot action";
    bool success=false;
    if(slot==-4 && (action==0 || action==2)){ItemDraft value;success=apply_inventory(state,team,slot,value,false);}
    else if(slot==-4){state.error="This action is not available for music kits.";}
    else if(slot==agent_slot){ItemDraft value;success=apply_one(state,team,slot,value);}
    else if(action==0)success=reset_inventory_slot(state);
    else if(action==2)success=remove_inventory_skin(state);
    else if(action==1){success=copy_inventory_slot(state);}
    else if(action==3){randomize_inventory_seed(state);success=state.error.empty();}
    state.inventory_navigation.selected_slot=old_selected;state.inventory_team=old_team;state.inventory_slot=old_slot;state.inventory_definition=old_definition;state.apply_context.clear();return success;
  };
}
}
