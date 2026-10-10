#include "core/menu/inventory_model.hpp"
#include <algorithm>
namespace iris::ui {
namespace {
struct Color{int definition,paint;ImU32 color;};
constexpr Color colors[]{
#include "core/sdk/generated/finish_colors.inc"
};
}
ImU32 rarity_color(int definition,int paint){for(const auto& color:colors)if(color.definition==definition&&color.paint==paint)return color.color;return 0xffaaaaaau;}
int rarity_level(int definition,int paint){switch(rarity_color(definition,paint)){case 0xff4b4bebu:return 6;case 0xffe62cd3u:return 5;case 0xffeb4b88u:return 4;case 0xffe6694bu:return 3;case 0xfff9ca5eu:return 2;default:return 1;}}
int weapon_group(int d){switch(d){case 1:case 2:case 3:case 4:case 30:case 32:case 36:case 61:case 63:case 64:return 0;case 7:case 8:case 9:case 10:case 11:case 13:case 16:case 38:case 39:case 40:case 60:return 2;default:return 1;}}
const cosmetics::Selection* applied_selection(const MenuState& state,int team,int slot){
    if(slot==glove_slot)return state.inventory.gloves[team].enabled?&state.inventory.gloves[team]:nullptr;
    for(const auto& s:state.cosmetics.loadouts[team])if((slot==knife_slot&&cosmetics::is_knife(s.definition))||s.definition==slot)return &s;return nullptr;
}
SkinEditor& skin_editor(MenuRuntime& state,int team,int slot,int definition){
    auto& editors=state.values.inventory.editors[team];for(auto& editor:editors)if(editor.slot==slot)return editor;
    SkinEditor editor;editor.slot=slot;editor.value.definition=definition;
    if(const auto* applied=applied_selection(state.values,team,slot))editor.value=*applied;
    editor.cached_counter=std::max(0,editor.value.stattrak);editors.push_back(editor);return editors.back();
}
void select_slot(MenuRuntime& state,int slot,int definition){state.inventory_slot=slot;state.inventory_definition=skin_editor(state,state.inventory_team,slot,definition).value.definition;state.grid_dirty=true;state.grid_scroll_reset=true;}
void select_skin(MenuRuntime& state,int paint){
    auto& editor=skin_editor(state,state.inventory_team,state.inventory_slot,state.inventory_definition);editor.value.paint=paint;
    const auto* finish=cosmetics::find_finish(editor.value.definition,paint);editor.value.wear=std::clamp(editor.value.wear,finish?finish->minimum_wear:0.f,finish?finish->maximum_wear:1.f);
    state.dirty=true;
}
void stage_skin(MenuState& state,int team,int slot,const cosmetics::Selection& selection){
    if(team<0||team>1||!cosmetics::valid(selection)||(slot==glove_slot&&!cosmetics::is_glove(selection.definition)))throw std::invalid_argument("Choose a valid item and finish before applying.");
    auto value=selection;const auto* type=cosmetics::find_weapon(value.definition);
    if(slot==glove_slot){
        if(!(type->teams&(1u<<team))){value.definition=team?5029:5028;value.paint=0;value.wear=std::clamp(value.wear,0.f,1.f);}
        value.stattrak=-1;value.name.fill(0);state.inventory.gloves[team]=value;
    }else{
        if(slot==knife_slot && !(type->teams&(1u<<team))){value.definition=team?42:59;value.paint=0;value.wear=std::clamp(value.wear,0.f,1.f);}
        auto& list=state.cosmetics.loadouts[team];auto found=std::find_if(list.begin(),list.end(),[&](const auto& s){return slot==knife_slot?cosmetics::is_knife(s.definition):s.definition==slot;});
        if(found==list.end())list.push_back(value);else *found=value;
    }
}
bool commit_skin(MenuRuntime& state){
    const auto editor=skin_editor(state,state.inventory_team,state.inventory_slot,state.inventory_definition).value;
    if(!cosmetics::valid(editor) || (state.inventory_slot==glove_slot && !cosmetics::is_glove(editor.definition))){state.error="Choose a valid item and finish before applying.";return false;}
    auto previous=state.values.cosmetics;auto previous_inventory=state.values.inventory;
    try{stage_skin(state.values,state.inventory_team,state.inventory_slot,editor);if(state.values.inventory.both_teams)stage_skin(state.values,1-state.inventory_team,state.inventory_slot,editor);}
    catch(const std::exception& error){state.error=error.what();state.values.cosmetics=previous;state.values.inventory=previous_inventory;return false;}
    if(submit_features(state)){state.feedback="Applied to "+std::string(state.values.inventory.both_teams?"both teams":state.inventory_team?"CT":"T");return true;}
    state.values.cosmetics=std::move(previous);state.values.inventory=std::move(previous_inventory);return false;
}
}
