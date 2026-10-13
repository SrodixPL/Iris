#include "core/menu/tabs.hpp"
#include "core/menu/feature_tree.hpp"
#include "core/menu/legit_profiles.hpp"
#include "core/features/combat/nospread.hpp"
#include "core/features/world/skyboxes.hpp"
#include "core/menu/widgets.hpp"
#include "core/menu/hotkeys.hpp"
#include "core/menu/config_io.hpp"
#include "core/menu/settings_io.hpp"
#include "core/menu/apply_queue.hpp"
#include "core/menu/inventory_model.hpp"
#include "core/menu/feature_bindings.hpp"
#include <imgui_internal.h>
#include <string_view>
#include <random>
#include <set>
#include <cfloat>
#include <type_traits>
#include <cstdio>
namespace iris::ui::tabs {
namespace {
bool panel_popup(const char* id,Bounds anchor,float width,float height,const char* title=nullptr,bool beside=false) { return widgets::begin_popup(id,anchor,width,height,title,beside); }
struct Weapon { int definition; const char* name; const char* model; bool knife; int teams; bool glove; };
struct Finish { int definition,paint; const char* weapon; const char* name; float minimum,maximum; bool legacy; };
struct Agent { int definition; const char* name; const char* model; int teams; };
struct FinishColor { int definition,paint; ImU32 color; };
constexpr Weapon weapons[]{
#include "core/sdk/generated/weapon_catalog.inc"
};
constexpr Finish finishes[]{
#include "core/sdk/generated/skin_catalog.inc"
};
constexpr Agent agents[]{
#include "core/sdk/generated/agent_catalog.inc"
};
constexpr FinishColor finish_colors[]{
#include "core/sdk/generated/finish_colors.inc"
};
const Finish* finish(int definition,int paint) {
  for(const auto& f:finishes) if(f.definition==definition && f.paint==paint) return &f;
  return nullptr;
}
int rarity(int definition,int paint) {
  for(const auto& color:finish_colors) if(color.definition==definition && color.paint==paint) {
    switch(color.color) { case 0xff4b4bebu:return 6; case 0xffe62cd3u:return 5; case 0xffeb4b88u:return 4; case 0xffe6694bu:return 3; case 0xfff9ca5eu:return 2; default:return 1; }
  }
  return 1;
}
InventoryEntry* entry(InventoryCanvas& canvas,int side,int slot) {
  auto& team=canvas.adapter.sides[side];
  for(auto& item:team.special) if(item.slot==slot) return &item;
  for(auto& column:team.weapons) for(auto& item:column) if(item.slot==slot) return &item;
  return nullptr;
}
void sync_item(InventoryCanvas& canvas,int side,int slot) {
  auto* tile=entry(canvas,side,slot);
  if(!tile) return;
  ItemDraft value;
  if(canvas.adapter.read_selection) canvas.adapter.read_selection(canvas.adapter.owner,side,slot,value);
  tile->paint=value.paint;
  tile->equipped=value.applied;
  tile->skin_name.clear(); tile->rarity_color=0;
  if(const auto* f=finish(value.definition,value.paint)) tile->skin_name=f->name;
  for(const auto& color:finish_colors) if(color.definition==value.definition && color.paint==value.paint) tile->rarity_color=color.color;
  if(slot<0) {
    tile->definition=value.definition;
    if(slot==-3 && !value.custom_model.empty())tile->skin_name=value.custom_model;
    tile->name=slot==-3 ? "Agent" : slot==-2 ? "Gloves" : slot==-1 ? "Knife" : "Music Kit";
    for(const auto& weapon:weapons) if(weapon.definition==value.definition) tile->skin_name=value.paint ? tile->skin_name : weapon.name;
    if(slot==-3) for(const auto& agent:agents) if(agent.definition==value.definition) tile->skin_name=agent.name;
    if(slot==-4)if(const auto* kit=cosmetics::find_music_kit(value.definition))tile->skin_name=kit->name;
  }
  tile->art=canvas.adapter.resolve_art ? canvas.adapter.resolve_art(canvas.adapter.owner,slot==-4?-4:tile->definition,slot==-4?value.definition:tile->paint) : InventoryArt{};
}
void sync_items(InventoryCanvas& canvas) {
  for(int side=0;side<2;++side) {
    for(auto& item:canvas.adapter.sides[side].special) sync_item(canvas,side,item.slot);
    for(auto& column:canvas.adapter.sides[side].weapons) for(auto& item:column) sync_item(canvas,side,item.slot);
  }
}
ImU32 skin_color(int definition,int paint) {
  for(const auto& color:finish_colors) if(color.definition==definition && color.paint==paint) return ImGui::GetColorU32(ImGui::ColorConvertU32ToFloat4(color.color));
  return ImGui::GetColorU32(theme::secondary);
}
ItemDraft applied_item(InventoryCanvas& canvas,int side,int slot,const InventoryEntry& item) {
  ItemDraft value;
  if(canvas.adapter.read_selection) canvas.adapter.read_selection(canvas.adapter.owner,side,slot,value);
  if(slot>=0 && !value.definition)value.definition=item.definition;
  return value;
}
bool submit_item(InventoryCanvas& canvas,int side,int slot,const ItemDraft& value,bool both) {
  if(!canvas.adapter.apply || !canvas.adapter.apply(canvas.adapter.owner,side,slot,value,both)) {
    canvas.error="Could not apply item. Previous selection kept.";return false;
  }
  sync_item(canvas,side,slot);if(both)sync_item(canvas,1-side,slot);
  canvas.error.clear();return true;
}
void random_seed(ItemDraft& value) {
  static std::mt19937 random{std::random_device{}()};
  value.seed=std::uniform_int_distribution<int>(0,1000)(random);
}
bool inventory_icon_button(const char* id,ImWchar code,ImFont* font,float size) {
  const bool pressed=widgets::button(id,{size,size});
  const auto p=ImGui::GetItemRectMin(), end=ImGui::GetItemRectMax();
  char text[5]{}; ImTextCharToUtf8(text,code);
  ImGui::PushFont(font,theme::body_size);
  auto* baked=ImGui::GetFontBaked(); const auto* glyph=baked->FindGlyphNoFallback(code);
  if(glyph) {
    const float ratio=ImGui::GetFontSize()/baked->Size;
    const ImVec2 position{(p.x+end.x-(glyph->X1-glyph->X0)*ratio)*.5f-glyph->X0*ratio,(p.y+end.y-(glyph->Y1-glyph->Y0)*ratio)*.5f-glyph->Y0*ratio};
    ImGui::GetWindowDrawList()->AddText(position,ImGui::GetColorU32(theme::text),text);
  }
  ImGui::PopFont(); return pressed;
}
void inventory_picker(MenuRuntime& state) {
  auto& canvas=state.inventory;
  const float s=state.scale;
  const int side=canvas.side,slot=canvas.selected[side];
  auto* tile=entry(canvas,side,slot);
  if(!tile) { canvas.browsing=false; canvas.restore_overview=true; return; }
  if(slot==-3) {
    if(!state.models_scanned && !state.model_scan.valid())state.model_scan=std::async(std::launch::async,[]{return models::scan(models::game_content());}).share();
    if(state.model_scan.valid() && state.model_scan.wait_for(std::chrono::seconds(0))==std::future_status::ready){state.model_catalog=state.model_scan.get();state.model_scan={};state.models_scanned=true;}
  }
  const auto actual=applied_item(canvas,side,slot,*tile);
  if(canvas.draft_team!=side || canvas.draft_slot!=slot || !(canvas.last_selection==actual)) {
    canvas.detail_draft=actual;canvas.last_selection=actual;canvas.draft_team=side;canvas.draft_slot=slot;
  }
  auto& value=canvas.detail_draft;
  const bool both_allowed=inventory_can_mirror(slot,value.definition ? value.definition : tile->definition);
  const bool available_apply=tile->enabled && (canvas.adapter.apply || !canvas.adapter.game_roster);
  const auto apply=[&](const ItemDraft& next,bool mirror) {
    const bool accepted=submit_item(canvas,side,slot,next,mirror);
    value=applied_item(canvas,side,slot,*tile);canvas.last_selection=value; canvas.wear_editing=false;
    return accepted;
  };
  const auto auto_apply=[&](const ItemDraft& next) { return apply(next,canvas.both_teams && inventory_can_mirror(slot,next.definition ? next.definition : tile->definition)); };
  canvas.preview_bounds={}; canvas.both_bounds={};
  if(canvas.observed_slot!=slot || canvas.observed_side!=side) {
    canvas.skin_search={}; canvas.rarity_filter=0; canvas.picker_context.clear();
    canvas.observed_slot=slot; canvas.observed_side=side;
    canvas.wear_editing=false;
    canvas.panel_fraction=(slot==-3 ? value.definition!=0||!value.custom_model.empty() : slot==-4?value.definition!=0:value.paint!=0) ? 1.f : 0.f;
    canvas.observed_panel=canvas.panel_fraction>0; canvas.panel_target=canvas.panel_from=canvas.panel_fraction;
    canvas.panel_motion.progress=1;
  }
  const auto team=side ? theme::inventory_team_ct : theme::inventory_team_t;
  const auto origin=ImGui::GetCursorScreenPos();
  const auto region=ImGui::GetContentRegionAvail();
  const auto close=[&] { canvas.browsing=false; canvas.restore_overview=true; };
  ImGui::PushStyleColor(ImGuiCol_NavCursor,{0,0,0,0});
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
  const float button=theme::inventory_filter_height*s;
  if(inventory_icon_button("##inventory_back",theme::inventory_back_icon,state.icons,button)) close();
  canvas.picker_back={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  ImGui::SameLine(0,theme::inventory_spacing*s);
  ImGui::PushStyleColor(ImGuiCol_Button,theme::surface_tint(theme::surface,team,.22f));
  ImGui::PushStyleColor(ImGuiCol_Text,team);
  const auto chip=ImGui::GetCursorScreenPos();
  ImGui::Dummy({button,button});
  auto* chip_draw=ImGui::GetWindowDrawList();
  chip_draw->AddRectFilled(chip,{chip.x+button,chip.y+button},ImGui::GetColorU32(theme::surface_tint(theme::raised,team,.22f)),theme::control_radius*s);
  const auto chip_label=slot==-4?"ALL":side ? "CT" : "T";
  chip_draw->AddText({chip.x+(button-ImGui::CalcTextSize(chip_label).x)*.5f,widgets::text_y(chip.y,button)},ImGui::GetColorU32(theme::team_label(team)),chip_label);
  ImGui::PopStyleColor(2); ImGui::SameLine(0,theme::inventory_spacing*s);
  const char* category="Special Items";
  for(int c=0;c<3;++c) for(const auto& item:canvas.adapter.sides[side].weapons[c]) if(item.slot==slot) category=c==0 ? "Pistols" : c==1 ? "Mid-Tier" : "Rifles";
  if(widgets::button(category,{0,button})) close();
  ImGui::SameLine(0,theme::inventory_tight_gap*s); ImGui::PushStyleColor(ImGuiCol_Text,theme::secondary); widgets::row_text(">",button); ImGui::PopStyleColor(); ImGui::SameLine(0,theme::inventory_tight_gap*s);
  ImGui::PushFont(state.bold,theme::body_size); widgets::row_text(tile->name.c_str(),button); ImGui::PopFont();
  const auto navigate=[&](int direction) {
    std::array<int,20> slots{}; int count=0;
    for(const auto& item:canvas.adapter.sides[side].special) if(item.enabled && !item.name.empty() && count<20) slots[count++]=item.slot;
    for(const auto& column:canvas.adapter.sides[side].weapons) for(const auto& item:column) if(item.enabled && count<20) slots[count++]=item.slot;
    int current=0; for(int i=0;i<count;++i) if(slots[i]==slot) current=i;
    if(!count) return;
    const int next=slots[(current+direction+count)%count];
    if(!canvas.adapter.open_slot || canvas.adapter.open_slot(canvas.adapter.owner,side,next)) {
      canvas.previous_item=value; canvas.previous_slot=slot;
      canvas.previous_rarity=canvas.rarity_filter; canvas.previous_search=canvas.skin_search; canvas.previous_scroll=canvas.grid_scroll;
      canvas.selected[side]=next; canvas.travel=direction; canvas.slot_motion.start(state.settings.animations);
    } else canvas.error="Could not open item.";
  };
  const int header_buttons=3;
  ImGui::SetCursorScreenPos({origin.x+region.x-header_buttons*button-(header_buttons-1)*theme::inventory_tight_gap*s,origin.y});
  if(inventory_icon_button("##previous",theme::inventory_back_icon,state.icons,button)) navigate(-1);
  canvas.navigation_bounds[0]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  ImGui::SameLine(0,theme::inventory_tight_gap*s); if(inventory_icon_button("##next",theme::inventory_next_icon,state.icons,button)) navigate(1);
  canvas.navigation_bounds[1]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  canvas.both_bounds={};
  ImGui::SameLine(0,theme::inventory_tight_gap*s); if(inventory_icon_button("##slot_options",theme::inventory_more_icon,state.icons,button)) ImGui::OpenPopup("slot_options");
  if(canvas.request_options) { ImGui::OpenPopup("slot_options"); canvas.request_options=false; }
  const Bounds options{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()}; canvas.options_bounds={options.min,options.max};
  const bool mirrored=both_allowed && canvas.both_teams;
  const char* labels[]{mirrored?"Reset slot for both teams":"Reset slot for this team","Copy slot to other team",mirrored?"Remove skin from both teams":"Remove applied skin","Randomize seed"};
  const char* hints[]{mirrored?"Restore this slot on both teams.":"Restore this slot's default item.","Copy the applied item to the other side.",mirrored?"Remove the finish on both teams.":"Remove the applied finish.","Apply a different pattern seed."};
  float options_width=0;
  for(const auto* label:labels) options_width=std::max(options_width,ImGui::CalcTextSize(label).x/s);
  ImGui::PushFont(nullptr,theme::caption_size);
  for(const auto* hint:hints) options_width=std::max(options_width,ImGui::CalcTextSize(hint).x/s);
  ImGui::PopFont();
  options_width+=2*(theme::popup_padding+theme::popup_item_padding);
  const Bounds options_anchor{{options.max.x-options_width*s,options.min.y},options.max};
  if(panel_popup("slot_options",options_anchor,options_width,4*theme::inventory_action_row+2*theme::popup_padding)) {
    for(int action=0;action<4;++action) {
      const bool unsupported=action<3 ? ((!canvas.adapter.slot_action || !canvas.adapter.read_selection) && canvas.adapter.game_roster) || (!both_allowed && action==1) : !available_apply || slot==-3 || slot==-4;
      ImGui::BeginDisabled(unsupported);
      ImGui::PushID(action);
      if(widgets::popup_item(labels[action],false,false,hints[action])) {
        if(action==3) { auto next=value; random_seed(next); auto_apply(next); }
        else if(canvas.adapter.slot_action && canvas.adapter.read_selection) {
          if(!canvas.adapter.slot_action(canvas.adapter.owner,side,slot,action)) canvas.error="Slot action failed.";
          else {
            const bool copied=action==1 || (both_allowed && canvas.both_teams);
            canvas.error.clear();
            value=applied_item(canvas,side,slot,*tile); sync_item(canvas,side,slot); canvas.wear_editing=false;
            if(copied) if(const auto* other=entry(canvas,1-side,slot)) {
              applied_item(canvas,1-side,slot,*other); sync_item(canvas,1-side,slot);
            }
          }
        } else {
          auto next=value;
          if(action==0) { next={}; next.definition=slot>=0 ? tile->definition : 0; }
          if(action==2) { next.paint=0; if(slot==-3) next.definition=0; }
          if(action==1) apply(next,true); else auto_apply(next);
        }
      }
      canvas.action_bounds[action]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
      ImGui::PopID(); ImGui::EndDisabled();
    }
    widgets::end_popup();
  }
  ImGui::SetCursorScreenPos({origin.x,origin.y+theme::inventory_picker_header*s});
  ImGui::GetWindowDrawList()->AddLine({origin.x,origin.y+theme::inventory_picker_header*s},{origin.x+region.x,origin.y+theme::inventory_picker_header*s},ImGui::GetColorU32(team));
  ImGui::Dummy({0,theme::inventory_picker_pad*s});
  if(!canvas.error.empty()) { ImGui::PushFont(nullptr,theme::caption_size); ImGui::TextColored(theme::destructive,"%s",canvas.error.c_str()); ImGui::PopFont(); }
  const bool selected=slot==-3 ? value.definition!=0||!value.custom_model.empty() : slot==-4?value.definition!=0:value.paint!=0;
  if(selected!=canvas.observed_panel) {
    canvas.panel_from=canvas.panel_fraction; canvas.panel_target=selected ? 1.f : 0.f;
    canvas.panel_motion.start(state.settings.animations); canvas.observed_panel=selected;
  }
  canvas.panel_fraction=canvas.panel_from+(canvas.panel_target-canvas.panel_from)*canvas.panel_motion.step(state.settings.animations,theme::inventory_panel_seconds);
  const auto body=ImGui::GetCursorScreenPos();
  const auto area=ImGui::GetContentRegionAvail();
  const float panel_width=std::min(theme::inventory_panel_width*s,area.x*.48f);
  const float reserve=canvas.panel_fraction*(panel_width+theme::inventory_picker_pad*s);
  const float grid_width=std::max(1.f,area.x-reserve);
  ImGui::BeginChild("picker_catalog",{grid_width,area.y},ImGuiChildFlags_NavFlattened);
  const auto remember=[&](const char*) {};
  if(slot==-1 || slot==-2) {
    ImGui::BeginDisabled(!available_apply);
    ImGui::SetNextItemWidth(std::min(theme::inventory_type_width*s,grid_width*.42f));
    const char* type=slot==-1 ? "Knife" : "Gloves";
    for(const auto& weapon:weapons) if(weapon.definition==value.definition) type=weapon.name;
    std::vector<widgets::Option> type_options;
    for(const auto& weapon:weapons) if((slot==-1 ? weapon.knife : weapon.glove) && (weapon.teams & (side ? 2 : 1))) type_options.push_back({weapon.name});
    if(widgets::begin_dropdown("##item_type","",static_cast<int>(type_options.size()),0)) {
      for(const auto& weapon:weapons) if((slot==-1 ? weapon.knife : weapon.glove) && (weapon.teams & (side ? 2 : 1))) {
        ImGui::PushID(weapon.definition);
        const bool current=value.definition==weapon.definition;
        if(widgets::popup_item(weapon.name,current)) { auto next=value; next.definition=weapon.definition; next.paint=0; auto_apply(next); }
        ImGui::PopID();
      }
      widgets::end_dropdown();
    }
    remember("Type");
    const auto type_p=ImGui::GetItemRectMin();
    const auto type_art=value.definition>0 && canvas.adapter.resolve_art ? canvas.adapter.resolve_art(canvas.adapter.owner,value.definition,0) : InventoryArt{};
    inventory::image(type_art,{type_p.x+theme::inventory_inset*s,type_p.y+theme::inventory_spacing*s},{theme::inventory_side_icon*s,theme::inventory_side_icon*s});
    inventory::caption(type,{type_p.x+(theme::inventory_side_icon+theme::inventory_spacing)*s,widgets::text_y(type_p.y,theme::input_height*s)},{ImGui::GetItemRectMax().x-theme::inventory_icon_button*s,ImGui::GetItemRectMax().y},ImGui::GetColorU32(theme::text));
    ImGui::EndDisabled();
    ImGui::SameLine(0,theme::inventory_spacing*s);
  }
  constexpr const char* rarities[]{"All rarities","Consumer","Industrial","Mil-Spec","Restricted","Classified","Covert"};
  ImGui::SetNextItemWidth(std::min(theme::inventory_rarity_width*s,ImGui::GetContentRegionAvail().x*.45f));
  ImGui::BeginDisabled(slot==-3 || slot==-4);
  if(widgets::begin_dropdown("##rarity",rarities[canvas.rarity_filter],7,0)) {
    for(int i=0;i<7;++i) { ImGui::PushID(i); if(widgets::popup_item(rarities[i],canvas.rarity_filter==i)) canvas.rarity_filter=i; ImGui::PopID(); }
    widgets::end_dropdown();
  }
  ImGui::EndDisabled(); remember("Rarity");
  ImGui::SameLine(0,theme::inventory_spacing*s); ImGui::SetNextItemWidth(-1);
  widgets::input_text("##skin_search",slot==-3 ? "Search agents" : slot==-4 ? "Search music kits" : "Search skins",canvas.skin_search.data(),canvas.skin_search.size());
  canvas.search_bounds={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  if(slot==-3) {
    ImGui::BeginDisabled(state.model_scan.valid());
    if(widgets::button("Refresh custom models"))state.models_scanned=false;
    ImGui::EndDisabled();
    if(!state.model_catalog.detail.empty())ImGui::TextWrapped("%s",state.model_catalog.detail.c_str());
  }
  ImGui::Dummy({0,theme::inventory_inset*s});
  const auto grid_origin=ImGui::GetCursorScreenPos(), grid_area=ImGui::GetContentRegionAvail();
  const float phase=canvas.slot_motion.step(state.settings.animations,theme::page_transition_seconds);
  const auto draw_grid=[&](ItemDraft& grid_value,int grid_slot,const char* query,int filter,float alpha,float offset,bool interactive) {
    ImGui::SetCursorScreenPos({grid_origin.x+offset,grid_origin.y});
    ImGui::BeginChild(interactive ? "skin_grid_scroll" : "skin_grid_previous",grid_area,ImGuiChildFlags_NavFlattened,interactive ? 0 : ImGuiWindowFlags_NoInputs);
    if(interactive) {
      const auto context=std::to_string(side)+":"+std::to_string(grid_slot)+":"+std::to_string(grid_value.definition)+":"+std::to_string(filter)+":"+query;
      if(context!=canvas.picker_context) { canvas.picker_context=context; ImGui::SetScrollY(0); }
    } else ImGui::SetScrollY(canvas.previous_scroll);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*alpha);
    const float available=ImGui::GetContentRegionAvail().x;
    const int columns=std::max(1,static_cast<int>((available+theme::inventory_skin_gap*s)/((theme::inventory_skin_min+theme::inventory_skin_gap)*s)));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,{theme::inventory_skin_gap*s*.5f,theme::inventory_skin_gap*s*.5f});
    if(ImGui::BeginTable("skin_grid",columns,ImGuiTableFlags_SizingStretchSame)) {
      const auto choose=[&](int definition,int paint,const char* name,bool supported=true,const char* resource=nullptr) {
        ImGui::TableNextColumn(); ImGui::PushID(resource ? resource : name); ImGui::PushID(paint);
        const auto p=ImGui::GetCursorScreenPos(); const ImVec2 size{std::min(theme::inventory_skin_max*s,ImGui::GetContentRegionAvail().x),theme::inventory_skin_height*s};
        ImGui::BeginDisabled(!supported || !available_apply);
        if(ImGui::InvisibleButton("skin",size,ImGuiButtonFlags_EnableNav)) {
          auto next=grid_value; next.definition=definition; next.paint=paint;next.custom_model=resource ? resource : "";
          const auto* f=finish(definition,paint); next.wear=std::clamp(next.wear,f ? f->minimum : 0,f ? f->maximum : 1);
          if(interactive) auto_apply(next);
        }
        const bool chosen=grid_value.definition==definition && grid_value.paint==paint && grid_value.custom_model==(resource ? resource : "");
        auto* draw=ImGui::GetWindowDrawList();
        const auto card=ImGui::IsItemHovered() ? theme::control_hover : theme::section_surface;
        auto tint=chosen ? theme::surface_tint(card,team,theme::inventory_selected_tint) : card;
        draw->AddRectFilled(p,{p.x+size.x,p.y+size.y},ImGui::GetColorU32(tint),theme::inventory_card_radius*s);
        const auto art=supported && definition>0 && canvas.adapter.resolve_art ? canvas.adapter.resolve_art(canvas.adapter.owner,grid_slot==-4?-4:definition,grid_slot==-4?definition:paint) : InventoryArt{};
        const float inset=theme::inventory_tile_inset*s;
        inventory::image_tile(art,{p.x+inset,p.y+inset},{size.x-2*inset,theme::inventory_skin_art*s},s);
        ImGui::PushFont(nullptr,theme::caption_size);
        inventory::ellipsized_caption(name,{p.x+inset,widgets::text_y(p.y+(theme::inventory_skin_art+theme::inventory_tile_inset)*s,theme::inventory_label_height*s)},{p.x+size.x-inset,p.y+size.y-theme::inventory_rarity_inset*s},ImGui::GetColorU32(chosen ? theme::text : theme::secondary)); ImGui::PopFont();
        inventory::rarity_edge(draw,p,{p.x+size.x,p.y+size.y},skin_color(definition,paint),s);
        ImGui::EndDisabled(); ImGui::PopID(); ImGui::PopID();
      };
      choose(grid_slot==-3||grid_slot==-4 ? 0 : grid_value.definition,0,grid_slot==-4?"Default music kit":"No skin / default");
      int matches=0;
      if(grid_slot==-3) {
        std::set<std::string_view> models;
        for(const auto& agent:agents) if((agent.teams & (side ? 2 : 1)) && !std::string_view(agent.name).starts_with("Default ") && models.insert(agent.model).second && inventory::matches(agent.name,query)) { choose(agent.definition,0,agent.name); ++matches; }
        for(const auto& model:state.model_catalog.entries)if(inventory::matches(model.name.c_str(),query)){choose(0,0,model.name.c_str(),true,model.resource.c_str());++matches;}
      } else if(grid_slot==-4) {for(const auto& kit:cosmetics::music_kits())if(inventory::matches(kit.name,query)){choose(kit.id,0,kit.name);++matches;}}
      else {
        bool catalog=false;
        for(const auto& f:finishes) if(f.definition==grid_value.definition) {
          catalog=true;
          if(inventory::matches(f.name,query) && (!filter || rarity(f.definition,f.paint)==filter)) { choose(f.definition,f.paint,f.name); ++matches; }
        }
        if(!catalog && grid_value.definition!=31) choose(grid_value.definition,-1,"Finishes unavailable",false);
      }
      ImGui::EndTable();
      if(!matches && (query[0] || filter)) {
        const auto p=ImGui::GetCursorScreenPos(), size=ImGui::GetContentRegionAvail(); const auto text=ImGui::CalcTextSize("No matching items");
        ImGui::GetWindowDrawList()->AddText({p.x+(size.x-text.x)*.5f,p.y+size.y*.5f},ImGui::GetColorU32(theme::secondary),"No matching items");
      }
    }
    if(interactive) canvas.grid_scroll=ImGui::GetScrollY();
    ImGui::PopStyleVar(2); ImGui::EndChild();
  };
  if(phase<1 && canvas.previous_slot!=-999) draw_grid(canvas.previous_item,canvas.previous_slot,canvas.previous_search.data(),canvas.previous_rarity,1-phase,-phase*theme::inventory_slot_slide*s*canvas.travel,false);
  draw_grid(value,slot,canvas.skin_search.data(),canvas.rarity_filter,phase,(1-phase)*theme::inventory_slot_slide*s*canvas.travel,true);
  ImGui::EndChild();
  canvas.panel_bounds={};
  if(canvas.panel_fraction>0) {
    const auto position=ImVec2{body.x+area.x-panel_width+(1-canvas.panel_fraction)*panel_width,body.y};
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*canvas.panel_fraction);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{theme::inventory_spacing*s,theme::inventory_panel_spacing*s});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{theme::inventory_spacing*s,theme::inventory_inset*s});
    ImGui::BeginChild("skin_settings",{panel_width,area.y},ImGuiChildFlags_AlwaysUseWindowPadding|ImGuiChildFlags_NavFlattened);
    canvas.panel_bounds={ImGui::GetWindowPos(),{ImGui::GetWindowPos().x+panel_width,ImGui::GetWindowPos().y+area.y}};
    auto* draw=ImGui::GetWindowDrawList(); draw->AddLine(position,{position.x,position.y+area.y},ImGui::GetColorU32(theme::border));
    ImGui::Indent(theme::inventory_panel_pad*s);
    const float width=ImGui::GetContentRegionAvail().x;
    const float preview_inset=theme::inventory_tile_inset*s;
    const float preview_art=std::min(theme::inventory_preview_art*s,std::max(1.f,width-2*preview_inset)*.75f);
    const float preview_title_y=preview_art+2*preview_inset;
    const float preview_skin_y=preview_title_y+theme::inventory_label_height*s;
    const float preview_height=preview_skin_y+(theme::inventory_preview_height-theme::inventory_preview_skin_y)*s;
    const auto preview=ImGui::GetCursorScreenPos();
    auto preview_tint=theme::section_surface;
    draw->AddRectFilled(preview,{preview.x+width,preview.y+preview_height},ImGui::GetColorU32(preview_tint),theme::inventory_card_radius*s);
    draw->AddRect(preview,{preview.x+width,preview.y+preview_height},ImGui::GetColorU32(theme::border),theme::inventory_card_radius*s,ImDrawFlags_RoundCornersAll,theme::surface_border_width*s);
    if(canvas.preview_definition!=value.definition){canvas.preview_definition=value.definition;canvas.preview_pose={0,0,theme::inventory_preview_zoom};canvas.preview_dragged=false;canvas.preview_click_time=-1;}
    auto art=slot!=-4&&value.definition>0 && canvas.adapter.preview ? canvas.adapter.preview(canvas.adapter.owner,value.definition,value.paint) : InventoryArt{};
    if(!art.texture.GetTexID() && value.definition>0 && canvas.adapter.resolve_art) art=canvas.adapter.resolve_art(canvas.adapter.owner,slot==-4?-4:value.definition,slot==-4?value.definition:value.paint);
    inventory::image_tile(art,{preview.x+preview_inset,preview.y+preview_inset},{width-2*preview_inset,preview_art},s,1.f);
    const auto* f=finish(value.definition,value.paint);
    const char* preview_name=tile->name.c_str();
    for(const auto& weapon:weapons) if(weapon.definition==value.definition) preview_name=weapon.name;
    inventory::ellipsized_caption(preview_name,{preview.x+preview_inset,widgets::text_y(preview.y+preview_title_y,preview_skin_y-preview_title_y)},{preview.x+width-preview_inset,preview.y+preview_skin_y},ImGui::GetColorU32(theme::text));
    const char* selection_name=f ? f->name : "Default";
    if(slot==-4)if(const auto* kit=cosmetics::find_music_kit(value.definition))selection_name=kit->name;
    if(slot==-3) { for(const auto& agent:agents) if(agent.definition==value.definition) selection_name=agent.name; if(!value.custom_model.empty())selection_name=value.custom_model.c_str(); }
    inventory::ellipsized_caption(selection_name,{preview.x+preview_inset,widgets::text_y(preview.y+preview_skin_y,preview_height-preview_skin_y)},{preview.x+width-preview_inset,preview.y+preview_height},ImGui::GetColorU32(theme::secondary));
    if(art.interactive) {
      ImGui::InvisibleButton("##skin_preview",{width,preview_height});
      const bool blocked=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
      const bool hovered=!blocked && ImGui::IsItemHovered();
      if(ImGui::IsItemDeactivated()) {
        // Count completed clicks rather than press events: a drag must not
        // become the first click of a reset when another drag starts quickly.
        if(blocked || canvas.preview_dragged)canvas.preview_click_time=-1;
        else {
          const auto& io=ImGui::GetIO();const double now=ImGui::GetTime();
          const float dx=io.MousePos.x-canvas.preview_click_position.x,dy=io.MousePos.y-canvas.preview_click_position.y;
          if(canvas.preview_click_time>=0 && now-canvas.preview_click_time<=io.MouseDoubleClickTime &&
             dx*dx+dy*dy<=io.MouseDoubleClickMaxDist*io.MouseDoubleClickMaxDist) {
            canvas.preview_pose={0,0,theme::inventory_preview_zoom};canvas.preview_click_time=-1;
          } else {canvas.preview_click_time=now;canvas.preview_click_position=io.MousePos;}
        }
      }
      if(blocked || !ImGui::IsItemActive() || ImGui::IsItemActivated())canvas.preview_dragged=false;
      if(hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if(ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY))
          canvas.preview_pose[2]=std::clamp(canvas.preview_pose[2]+ImGui::GetIO().MouseWheel*.12f,-1.1f,1.5f);
        if(!ImGui::IsItemActive() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
          widgets::tooltipf("Drag to rotate. Scroll to zoom. Double-click to reset.");
      }
      if(!blocked && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left,4*s)) {
        canvas.preview_dragged=true;
        const auto delta=ImGui::GetIO().MouseDelta;
        canvas.preview_pose[0]=std::remainder(canvas.preview_pose[0]-delta.x*theme::inventory_preview_rotation/s,360.f);
        canvas.preview_pose[1]=std::clamp(canvas.preview_pose[1]+delta.y*theme::inventory_preview_rotation/s,-75.f,75.f);
      }
    } else {canvas.preview_dragged=false;canvas.preview_click_time=-1;ImGui::Dummy({width,preview_height});}
    canvas.preview_bounds={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
    ImGui::BeginDisabled(!available_apply);
    if(slot!=-3 && slot!=-4) {
      float wear=canvas.wear_editing ? canvas.wear_value : value.wear;
      widgets::SliderEdit edit;
      widgets::slider("##wear","Wear",wear,f ? f->minimum : 0,f ? f->maximum : 1,0,"%.3f",false,true,true,true,&edit,hotkeys::binding("Inventory/"+std::to_string(side)+"/"+std::to_string(slot)+"/Wear"));
      remember("Wear");
      if(edit.active) { canvas.wear_value=wear; canvas.wear_editing=true; }
      if(edit.finished) { auto next=value; next.wear=wear; auto_apply(next); }
      else if(!edit.active) canvas.wear_editing=false;
      constexpr const char* wears[]{"FN","MW","FT","WW","BS"}; constexpr float floats[]{.02f,.1f,.25f,.4f,.65f};
      const float segment=(width-4*theme::inventory_rarity_thickness*s)/5;
      const int wear_segment=wear<.07f ? 0 : wear<.15f ? 1 : wear<.38f ? 2 : wear<.45f ? 3 : 4;
      for(int i=0;i<5;++i) {
        if(i) ImGui::SameLine(0,theme::inventory_rarity_thickness*s);
        ImGui::PushStyleColor(ImGuiCol_Button,i==wear_segment ? theme::active : theme::surface);
        if(widgets::button(wears[i],{segment,theme::inventory_wear_segment*s})) { auto next=value; next.wear=std::clamp(floats[i],f ? f->minimum : 0,f ? f->maximum : 1); auto_apply(next); }
        remember(wears[i]);
        ImGui::PopStyleColor();
      }
      widgets::row_text("Pattern seed",theme::inventory_label_height*s);
      ImGui::SetNextItemWidth(width-(theme::input_height+theme::inventory_tight_gap)*s);
      if(widgets::input_int("##seed",&value.seed)) { auto next=value; next.seed=std::clamp(next.seed,0,1000); auto_apply(next); }
      remember("Seed"); ImGui::SameLine(0,theme::inventory_tight_gap*s);
      if(inventory_icon_button("##random",theme::inventory_random_icon,state.icons,theme::input_height*s)) { auto next=value; random_seed(next); auto_apply(next); }
      remember("Randomize");
    }
    if(slot!=-3 && slot!=-2 && slot!=-4) {
      const auto binding=hotkeys::binding("Inventory/"+std::to_string(side)+"/"+std::to_string(slot)+"/StatTrak");
      if(widgets::toggle("stattrak","StatTrak",value.stattrak,slot==-4,nullptr,true,true,binding)) auto_apply(value);
      remember("StatTrak");
      if(value.stattrak && slot!=-4) {
        widgets::row_text("Counter",theme::inventory_label_height*s); ImGui::SetNextItemWidth(-1);
        if(widgets::input_int("##counter",&value.counter)) { auto next=value; next.counter=std::max(0,next.counter); auto_apply(next); }
        remember("Counter");
      }
    }
    if(slot!=-3 && slot!=-4 && slot!=-2) {
      widgets::row_text("Nametag",theme::inventory_label_height*s); ImGui::SetNextItemWidth(-1);
      const bool entered=widgets::input_text("##name","None",value.name.data(),value.name.size(),ImGuiInputTextFlags_EnterReturnsTrue);
      if(entered || ImGui::IsItemDeactivatedAfterEdit()) auto_apply(value);
      remember("Nametag");
    }
    ImGui::EndDisabled();
    if(both_allowed) {
      ImGui::Dummy({0,theme::setting_gap*s});
      ImGui::PushStyleColor(ImGuiCol_Button,canvas.both_teams ? theme::surface_tint(theme::raised,theme::accent,theme::selected_surface_tint) : theme::raised);
      ImGui::PushStyleColor(ImGuiCol_Text,canvas.both_teams ? theme::accent_label() : theme::text);
      ImGui::BeginDisabled(!available_apply);
      if(widgets::button("Apply to both teams##both",{width,theme::input_height*s})) {
        set_both_teams(state,side,slot,!canvas.both_teams);
        canvas.both_teams=state.values.inventory.both_teams;
      }
      ImGui::EndDisabled();
      canvas.both_bounds={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()}; ImGui::PopStyleColor(2);
      for(int i=0;i<2;++i) {
        const auto q=ImVec2{canvas.both_bounds[0].x+(theme::inventory_spacing+i*theme::inventory_inset)*s,(canvas.both_bounds[0].y+canvas.both_bounds[1].y)*.5f};
        const auto color=ImGui::GetColorU32(canvas.both_teams ? theme::accent_label() : theme::secondary);
        draw->AddLine(q,{q.x+theme::inventory_inset*s,q.y+theme::inventory_inset*s},color,s);
        draw->AddLine({q.x+theme::inventory_inset*s,q.y+theme::inventory_inset*s},{q.x+theme::inventory_side_icon*s,q.y-theme::inventory_inset*s},color,s);
      }
    }
    ImGui::Unindent(theme::inventory_panel_pad*s);
    ImGui::EndChild(); ImGui::PopStyleVar(4);
  }
  ImGui::PopStyleVar(); ImGui::PopStyleColor();
}
struct Controls {
  MenuRuntime& state;
  const char* category;
  const char* binding_category{};
  std::string binding_path(const char* id) const {return std::string(binding_category?binding_category:category)+"/"+id;}
  bool visible(const char* label) const { return inventory::matches(category,state.search) || inventory::matches(label,state.search); }
  void toggle(const char* id,const char* label,bool& value) { if(visible(label)) { widgets::toggle(id,label,value,false,nullptr,true,true,hotkeys::binding(binding_path(id))); } }
  void slider(const char* id,const char* label,float& value,float low,float high,const char* format="%.2f") {
    if(visible(label)) { widgets::slider(id,label,value,low,high,0,format,false,true,true,false,nullptr,hotkeys::binding(binding_path(id))); }
  }
  void percent(const char* id,const char* label,float& value) {if(visible(label))widgets::percent(id,label,value,true,hotkeys::binding(binding_path(id)));}
  void combo(const char* id,const char* label,int& value,std::span<const widgets::Option> options) {
    if(visible(label)) { widgets::combo(id,label,value,options,false,nullptr,true,true,hotkeys::binding(binding_path(id))); }
  }
  void choice(const char* id,const char* label,int& value,std::span<const char* const> names) {
    std::vector<widgets::Option> options;for(std::size_t i=0;i<names.size();++i)options.push_back({names[i],std::string_view(id)=="landing"&&i==3});combo(id,label,value,options);
  }
  void color(const char* id,const char* label,ImVec4& value,ColorSelectorState& editor) {
    if(visible(label)) { widgets::color_selector(id,label,value,editor,state.color_palette,false,true); }
  }
};
template<class Draw> void group(MenuRuntime& state,const char* id,const char* title,const char* keywords,Draw draw,bool inline_world=false) {
  if(!inventory::matches(std::string(title)+" "+keywords,state.search)) return;
  if(inline_world) {
    ImGui::PushID(id);
    ImGui::TextColored(theme::secondary,"%s",title);
    ImGui::Separator();
    Controls controls{state,title}; draw(controls);
    ImGui::Dummy({0,theme::group_gap*state.scale});
    ImGui::PopID();
    return;
  }
  if((state.selected==Page::World && std::string_view(id)!="environment") || (state.selected==Page::Player && std::string_view(id)!="visual_enemy" && std::string_view(id)!="visual_main") || std::string_view(id)=="anti_aim" || std::string_view(id)=="movement") {
    ImGui::PushID(id);
    const bool selected=ImGui::IsPopupOpen("category");
    ImGui::PushStyleColor(ImGuiCol_Button,selected ? theme::surface_tint(theme::surface,theme::accent,theme::inventory_selected_tint) : ImVec4{0,0,0,0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,theme::control_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,theme::surface_tint(theme::section_surface,theme::accent,theme::inventory_selected_tint));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,{0,.5f});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{theme::popup_item_padding*state.scale,ImGui::GetStyle().FramePadding.y});
    if(widgets::button(title,{-1,theme::hotkey_row*state.scale})) ImGui::OpenPopup("category");
    if(state.request_world_preview && std::string_view(id)=="color_light") { ImGui::OpenPopup("category"); state.request_world_preview=false; }
    ImGui::PopStyleVar(3); ImGui::PopStyleColor(3);
    const Bounds anchor{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
    auto* section_draw=ImGui::GetWindowDrawList();
    const float center_y=(anchor.min.y+anchor.max.y)*.5f;
    widgets::chevron({anchor.max.x-(theme::input_padding+6)*state.scale,center_y},selected ? .5f : -.5f);
    section_draw->AddLine({anchor.min.x,anchor.max.y},{anchor.max.x,anchor.max.y},ImGui::GetColorU32(theme::border),state.scale);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize,theme::hotkey_border*state.scale);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,theme::hotkey_rounding*state.scale);
    ImGui::PushStyleColor(ImGuiCol_PopupBg,theme::popup_surface);
    if(widgets::begin_popup("category",anchor,theme::world_popup_width,theme::world_popup_height,title,true,true,true,false)) {
      Controls controls{state,title}; draw(controls);
      widgets::end_popup();
    }
    ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
    ImGui::PopID();
    return;
  }
  if(widgets::begin_group(id,title)) {
    Controls controls{state,title};
    draw(controls);
  }
  widgets::end_group();
  ImGui::Dummy({0,theme::group_gap*state.scale});
}
bool world_reset(MenuRuntime& state,const char* label) {
  const float s=state.scale,available=ImGui::GetContentRegionAvail().x;
  const float width=std::min(available,std::max(theme::world_reset_width*s,ImGui::CalcTextSize(label).x+2*theme::input_padding*s));
  const auto at=ImGui::GetCursorScreenPos();
  ImGui::SetCursorScreenPos({at.x+(available-width)*.5f,at.y+theme::setting_gap*s});
  ImGui::PushStyleColor(ImGuiCol_Button,theme::raised);
  const bool clicked=widgets::button(label,{width,theme::secondary_height*s});
  ImGui::PopStyleColor();
  return clicked;
}
}
void page_header(MenuRuntime& state) {
  const float s=state.scale;
  const float header_width=ImGui::GetContentRegionAvail().x;
  const bool inventory_header=state.selected==Page::Inventory;
  const float refresh_space=inventory_header ? (theme::inventory_icon_button+theme::inventory_tight_gap)*s : 0;
  const float search_width=std::min(theme::search_width*s,std::max(80*s,header_width-theme::inventory_gap*s-refresh_space));
  const auto header_origin=ImGui::GetCursorScreenPos();
  const float header_row=theme::input_height*s;
  ImGui::SetCursorScreenPos(header_origin);
  state.title_bounds={};
  if(state.selected==Page::Legit)legit_profiles::selector(state,210*s);
  else if(state.selected==Page::Player)feature_tree::player_selector(state,210*s);
  else if(inventory_header)inventory_config_selector(state,210*s);
  ImGui::SetCursorScreenPos({header_origin.x+header_width-search_width-refresh_space,header_origin.y});
  ImGui::SetNextItemWidth(search_width-(state.search[0] ? (theme::more_width+theme::inventory_inset)*s : 0));
  if(state.focus_search) { ImGui::SetKeyboardFocusHere(); state.focus_search=false; }
  const float field_padding=(theme::input_padding+16)*s;
  const bool changed_search=widgets::input_text("##shell_search",state.search[0] ? nullptr : "Search",state.search,sizeof(state.search),0,0,field_padding);
  const bool select_first=(ImGui::IsItemActive() || ImGui::IsItemDeactivated()) && ImGui::IsKeyPressed(ImGuiKey_Enter,false);
  if(changed_search) {state.search_target.clear();state.search_reveal_pending=false;}
  if((changed_search || select_first) && state.search[0] && !ImGui::IsPopupOpen("option_search"))ImGui::OpenPopup("option_search");
  state.search_bounds={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  {
    ImGui::PushFont(state.icons,12);
    widgets::icon_text(ImGui::GetWindowDrawList(),{state.search_bounds.min.x+theme::input_padding*s+7*s,(state.search_bounds.min.y+state.search_bounds.max.y)*.5f},ImGui::GetColorU32(theme::muted),"\xee\x9c\xa1");
    ImGui::PopFont();
  }
  if(state.search[0]) {
    ImGui::SetCursorScreenPos({header_origin.x+header_width-theme::more_width*s-refresh_space,state.search_bounds.min.y});
    if(widgets::button("X##clear_search",{theme::more_width*s,state.search_bounds.max.y-state.search_bounds.min.y})) {
      state.search[0]='\0';state.search_target.clear();state.search_reveal_pending=false;
      state.focus_search=true;
    }
    state.clear_search={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  }
  if(inventory_header) {
    const float size=theme::inventory_icon_button*s;
    ImGui::SetCursorScreenPos({header_origin.x+header_width-size,header_origin.y+(header_row-size)*.5f});
    if(inventory_icon_button("##refresh_roster",theme::inventory_refresh_icon,state.icons,size) && state.inventory.adapter.request_refresh)state.inventory.adapter.request_refresh(state.inventory.adapter.owner);
    if(ImGui::IsItemHovered())widgets::tooltipf("Refresh equipped loadout");
  }
  const float result_rows=std::clamp(static_cast<float>(feature_tree::search_result_count(state)),1.f,theme::popup_rows);
  if(widgets::begin_popup("option_search",state.search_bounds,320,theme::popup_padding*2+result_rows*theme::inventory_action_row,nullptr,false,false,false,false,false,true)) {
    if(!feature_tree::search_results(state,select_first))ImGui::TextColored(theme::secondary,"No matching options.");
    if(!state.search[0])ImGui::CloseCurrentPopup();
    widgets::end_popup();
  }
  ImGui::SetCursorScreenPos({header_origin.x,header_origin.y+theme::header_height*s});
  if(state.selected==Page::Inventory) {
    const auto p=ImGui::GetCursorScreenPos();
    state.inventory.divider_bounds={p,{p.x+header_width,p.y}};
    ImGui::Dummy({header_width,s});
  } else ImGui::Separator();
  ImGui::Dummy({0,theme::group_gap*s});
}
void configs(MenuRuntime& state) {
  const float s=state.scale;
  ImGui::PushID("config_preferences");
  constexpr widgets::Option sort_options[]{{"Newest first"},{"Recently modified"},{"Alphabetical"}};
  if(widgets::begin_dropdown("##sort_profiles",sort_options[state.config_sort].label,3,160*s)) {
    for(int i=0;i<3;++i)if(widgets::popup_item(sort_options[i].label,state.config_sort==i))state.config_sort=i;
    widgets::end_dropdown();
  }
  ImGui::PopID();
  std::stable_sort(state.configs.begin(),state.configs.end(),[&](const ConfigEntry& a,const ConfigEntry& b) {
    return state.config_sort==2 ? a.name<b.name : a.modified==b.modified ? a.name<b.name : a.modified>b.modified;
  });
  ImGui::Dummy({0,8*s});
  const auto button=[&](const char* label,bool menu=false) {
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,menu ? ImVec2{0,.5f} : ImVec2{.5f,.5f});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{(menu ? theme::popup_item_padding : theme::input_padding)*s,ImGui::GetStyle().FramePadding.y});
    const bool clicked=widgets::button(label,menu ? ImVec2{-1,theme::option_height*s} : ImVec2{});
    ImGui::PopStyleVar(2);
    return clicked;
  };
  ImGui::PushStyleColor(ImGuiCol_Button,theme::accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,theme::mix(theme::accent,theme::text,.12f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,theme::mix(theme::accent,theme::text,.06f));
  ImGui::PushStyleColor(ImGuiCol_Text,theme::on_accent);
  ImGui::BeginDisabled(state.config_busy || state.apply_pending || state.persist_pending);
  if(button("New")){state.name_action="Create";state.request_config_popup=true;state.config_name={};state.config_anchor={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};}
  ImGui::EndDisabled();
  ImGui::PopStyleColor(4);
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Button,theme::raised);
  ImGui::BeginDisabled(state.config_busy || state.apply_pending || state.persist_pending);
  if(button("Import")){state.name_action="Import";state.request_config_popup=true;state.config_name={};state.config_anchor={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};}
  ImGui::EndDisabled();
  ImGui::PopStyleColor();
  ImGui::Dummy({0,theme::inventory_gap*s});
  ImGui::BeginChild("config_rows",{0,0},ImGuiChildFlags_NavFlattened);
  const float width=ImGui::GetContentRegionAvail().x;
  for(int i=0;i<static_cast<int>(state.configs.size());++i) {
    const auto& profile=state.configs[i];
    if(!inventory::matches(profile.name,state.search)) continue;
    ImGui::PushID(i);
    const auto p=ImGui::GetCursorScreenPos();
    const ImVec2 end{p.x+width,p.y+theme::config_row_height*s};
    auto* draw=ImGui::GetWindowDrawList();
    auto tint=profile.name==state.active_config ? theme::surface_tint(theme::section_surface,theme::accent,theme::selected_surface_tint) : theme::section_surface;
    draw->AddRectFilled(p,end,ImGui::GetColorU32(tint),theme::control_radius*s);
    if(profile.name==state.active_config) {
      const float y=p.y+(theme::config_row_height-theme::config_marker_height)*s*.5f;
      draw->AddRectFilled({p.x,y},{p.x+theme::config_marker_width*s,y+theme::config_marker_height*s},ImGui::GetColorU32(theme::accent),theme::config_marker_width*s*.5f);
    } else draw->AddRect(p,end,ImGui::GetColorU32(theme::border),theme::control_radius*s,ImDrawFlags_RoundCornersAll,theme::surface_border_width*s);
    const float controls=(theme::more_width+theme::setting_gap+theme::config_action_width+theme::config_row_padding)*s;
    const bool dirty=profile.name==state.active_config && state.dirty;
    std::string title=profile.name+(dirty ? " *" : "");
    const float line=(theme::config_row_height-2*theme::config_row_padding)*s*.5f;
    const float text_top=p.y+theme::config_row_padding*s;
    inventory::caption(title.c_str(),{p.x+theme::config_row_padding*s,widgets::text_y(text_top,line)},{end.x-controls,text_top+line},ImGui::GetColorU32(theme::text));
    ImGui::PushFont(nullptr,theme::caption_size);
    const std::string detail="Modified: "+profile.modified+"   Author: Local"+(profile.name==state.settings.autoload ? "   Default" : "");
    inventory::caption(detail.c_str(),{p.x+theme::config_row_padding*s,widgets::text_y(text_top+line,line)},{end.x-controls,end.y-theme::config_row_padding*s},ImGui::GetColorU32(theme::secondary));
    ImGui::PopFont();
    ImGui::SetCursorScreenPos({end.x-controls,p.y+(theme::config_row_height-theme::more_width)*s*.5f});
    ImGui::PushStyleColor(ImGuiCol_Text,{0,0,0,0});
    ImGui::PushStyleColor(ImGuiCol_Button,ImGui::IsPopupOpen("actions") ? theme::surface_tint(theme::surface,theme::text,.05f) : ImVec4{0,0,0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
    const bool more=widgets::button("...",{theme::more_width*s,theme::more_width*s});
    ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
    const Bounds more_bounds{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
    ImGui::PushFont(state.icons,theme::profile_icon);
    widgets::icon_text(draw,{(more_bounds.min.x+more_bounds.max.x)*.5f,(more_bounds.min.y+more_bounds.max.y)*.5f},ImGui::GetColorU32(theme::secondary),"\xee\x9c\x92");
    ImGui::PopFont();
    if(more) ImGui::OpenPopup("actions");
    ImGui::SetCursorScreenPos({more_bounds.max.x+theme::setting_gap*s,p.y+(theme::config_row_height-theme::config_action_height)*s*.5f});
    ImGui::PushStyleColor(ImGuiCol_Button,theme::active);
    ImGui::PushStyleColor(ImGuiCol_Text,theme::accent_label());
    ImGui::BeginDisabled(state.apply_pending || state.config_busy);
    if(widgets::button(profile.name==state.active_config ? "Save" : "Load",{theme::config_action_width*s,theme::config_action_height*s})) {
      if(profile.name==state.active_config) save_config(state); else request_load(state,profile.name);
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor(2);
    const Bounds anchor{{end.x-controls,p.y},{end.x,end.y}};
    const float menu_width=ImGui::CalcTextSize("Make default").x/s+2*(theme::popup_padding+theme::popup_item_padding)+theme::popup_check_space;
    if(panel_popup("actions",anchor,menu_width,5*theme::option_height+2*theme::popup_padding+theme::hotkey_divider+2*theme::hotkey_divider_margin)) {
      const auto entry=[&](const char* label,bool disabled=false,const char* reason=nullptr,bool selected=false,bool destructive=false) {
        ImGui::PushID(label);
        ImGui::BeginDisabled(disabled);
        if(destructive) ImGui::PushStyleColor(ImGuiCol_Text,theme::destructive);
        const bool clicked=widgets::popup_item(label,selected);
        if(destructive) ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if(reason && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) widgets::tooltipf("%s",reason);
        ImGui::PopID();
        return clicked;
      };
      if(profile.name==state.active_config && entry("Reload",state.apply_pending || state.persist_pending || state.config_busy))request_load(state,profile.name);
      const bool busy=state.config_busy || state.apply_pending || state.persist_pending;
      if(entry("Make default",busy,nullptr,profile.name==state.settings.autoload)){state.settings.autoload=profile.name;save_settings(state);ImGui::CloseCurrentPopup();}
      const auto name_action=[&](const char* action){state.name_action=action;state.request_config_popup=true;state.selected_config=profile.name;state.config_name={};if(std::string_view(action)=="Rename")strncpy_s(state.config_name.data(),state.config_name.size(),profile.name.c_str(),_TRUNCATE);state.config_anchor=anchor;ImGui::CloseCurrentPopup();};
      if(entry("Rename",busy || default_config(profile.name),default_config(profile.name) ? "The protected default profile cannot be renamed." : nullptr))name_action("Rename");
      if(entry("Duplicate",busy))name_action("Duplicate");
      if(entry("Share",busy)){state.config_busy=true;state.config_io->request(ConfigAction::Export,profile.name);ImGui::CloseCurrentPopup();}
      ImGui::Dummy({0,theme::hotkey_divider_margin*s});ImGui::Separator();ImGui::Dummy({0,theme::hotkey_divider_margin*s});
      if(entry("Delete",busy || default_config(profile.name),default_config(profile.name) ? "The protected default profile cannot be deleted." : nullptr,false,true))name_action("Delete");
      widgets::end_popup();
    }
    ImGui::SetCursorScreenPos({p.x,end.y+theme::inventory_gap*s});
    ImGui::Dummy({width,0});
    ImGui::PopID();
  }
  ImGui::EndChild();
}
void inventory_page(MenuRuntime& state) {
  const bool opening = !state.inventory_trace_started;
  if (opening && state.trace) {
    char message[192]{};
    std::snprintf(message, sizeof(message), "Opening Inventory: roster=%s slots=%zu/%zu skins=%zu/%zu.",
                  state.equipped_roster.available ? "ready" : "unavailable",
                  state.equipped_roster.teams[0].size(), state.equipped_roster.teams[1].size(),
                  state.values.cosmetics.loadouts[0].size(), state.values.cosmetics.loadouts[1].size());
    state.trace(message);
  }
  auto& canvas=state.inventory;
  if(canvas.browsing && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel) && !state.key_capture && ImGui::IsKeyPressed(ImGuiKey_Escape,false)){canvas.browsing=false;canvas.restore_overview=true;}
  if(canvas.adapter.refresh_loadout) {
    if(!canvas.adapter.game_roster) { canvas.adapter.sides={}; canvas.adapter.game_roster=true; }
    InventorySide roster;
    if(canvas.adapter.refresh_loadout(canvas.adapter.owner,canvas.side,roster)) canvas.adapter.sides[canvas.side]=std::move(roster);
    else canvas.adapter.sides[canvas.side]={};
  }
  canvas.empty_detail=state.runtime_status.available ? state.equipped_roster.detail : state.runtime_status.detail;
  sync_items(canvas);
  if(canvas.observed_browsing!=canvas.browsing) {
    canvas.observed_browsing=canvas.browsing;
    canvas.wear_editing=false;
    canvas.route_motion.start(state.settings.animations);
    if(canvas.browsing) canvas.slot_motion.progress=1;
  }
  const bool picker=canvas.browsing;
  const float phase=canvas.route_motion.step(state.settings.animations,theme::page_transition_seconds);
  const auto origin=ImGui::GetCursorScreenPos(), size=ImGui::GetContentRegionAvail();
  const auto view=[&](bool open,float alpha,float offset,bool interactive) {
    ImGui::SetCursorScreenPos({origin.x+offset,origin.y});
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::BeginChild(open ? "inventory_picker_page" : "inventory_overview",size,ImGuiChildFlags_NavFlattened,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|(interactive ? 0 : ImGuiWindowFlags_NoInputs));
    if(open) inventory_picker(state);
    else { page_header(state); if(!canvas.error.empty())ImGui::TextColored(theme::destructive,"%s",canvas.error.c_str()); inventory::render(canvas,state.scale,state.search,state.settings.animations); }
    ImGui::EndChild(); ImGui::PopStyleVar(2);
  };
  if(phase<1) view(!picker,1-phase,(picker ? -1 : 1)*phase*theme::inventory_route_slide*state.scale,false);
  view(picker,phase,(picker ? 1 : -1)*(1-phase)*theme::inventory_route_slide*state.scale,true);
  if(picker) state.title_bounds=state.search_bounds=state.clear_search={};
  if(opening && state.trace) state.trace("Inventory frame rendered successfully.");
  state.inventory_trace_started = true;
}
void render(MenuRuntime& state) {
  if(state.page_motion.page!=static_cast<int>(state.selected)) { state.page_motion.page=static_cast<int>(state.selected);state.page_motion.motion.start(state.settings.animations); }
  const float phase=state.page_motion.motion.step(state.settings.animations,theme::page_transition_seconds);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*phase);
  ImGui::BeginDisabled(state.startup_loading);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY()+(1-phase)*theme::page_transition_slide*state.scale);
  if(state.selected==Page::Inventory) { inventory_page(state);ImGui::EndDisabled();ImGui::PopStyleVar();return; }
  state.inventory_trace_started = false;
  page_header(state);
  if(state.selected==Page::Config) configs(state);
  else feature_tree::render(state);
  ImGui::EndDisabled();
  if(!state.error.empty()) ImGui::TextWrapped("%s",state.error.c_str());
  ImGui::PopStyleVar();
}
}
