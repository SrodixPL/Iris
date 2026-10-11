#include "core/menu/feature_tree.hpp"
#include "core/menu/feature_catalog.hpp"
#include "core/menu/feature_switches.hpp"
#include "core/menu/widgets.hpp"
#include "core/menu/hotkeys.hpp"
#include "core/menu/config_io.hpp"
#include "core/menu/inventory.hpp"
#include "core/menu/inventory_model.hpp"
#include "core/menu/apply_queue.hpp"
#include "core/menu/esp_preview.hpp"
#include "core/menu/legit_profiles.hpp"
#include "core/menu/feature_bindings.hpp"
#include "ext/json/json.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <string_view>
#include <vector>
#include <cmath>
#include <imgui_internal.h>
namespace iris::ui::feature_tree {
namespace {
struct Value { bool enabled{}; float number{}; int choice{}; std::array<bool,16> selections{}; ImVec4 color{theme::default_accent}; ImGuiKey key{ImGuiKey_None}; ColorSelectorState editor; };
std::map<int,Value> values;
esp_preview::State player_preview;
MenuRuntime* player_owner{};
std::string loaded;
using Json=nlohmann::json;
Value& value(int index) {
  auto [it,inserted]=values.try_emplace(index);
  if(inserted) {it->second.number=nodes[index].initial;const auto c=feature_switches::default_color(nodes[index].path);it->second.color={c[0],c[1],c[2],c[3]};}
  if(inserted && std::string_view(nodes[index].path)=="Visuals/Player/ESP items/Flag types"){const auto defaults=visuals::Settings{}.flags;std::copy(defaults.begin(),defaults.end(),it->second.selections.begin());}
  if(inserted && std::string_view(nodes[index].path)=="Visuals/Player/Enemy model / Chams/Visible pass")it->second.enabled=true;
  return it->second;
}
std::vector<widgets::Option> options(const Node& node,std::vector<std::string>& labels) {
  std::string text=node.options;
  for(size_t start=0;start<text.size();) { auto end=text.find('|',start);labels.push_back(text.substr(start,end-start));if(end==std::string::npos)break;start=end+1; }
  std::vector<widgets::Option> out;for(auto& label:labels)out.push_back({label.c_str()});
  if(std::string_view(node.path).starts_with("Visuals/Player/Enemy model / Chams/Material:"))for(std::size_t i=4;i<out.size();++i)out[i].disabled=true;return out;
}
int find(std::string_view path) { for(int i=0;i<std::size(nodes);++i)if(path==nodes[i].path)return i;return -1; }
Page page_for(int index) {
  const std::string_view path=nodes[index].path;
  if(path=="Misc/Inventory/Equipment / item selection"||path=="Misc/Inventory/Skins for selected item")return Page::Count;

  if(path.starts_with("Combat/Legit"))return Page::Legit;

  if(path.starts_with("Visuals/Player"))return Page::Player;
  if(path.starts_with("Visuals/World"))return Page::World;
  if(path.starts_with("Misc/Inventory"))return Page::Inventory;
  if(path.starts_with("Misc/Misc"))return Page::Misc;
  return Page::Count;
}
bool reveal_route(const MenuRuntime& state,int index) {
  if(!state.search_reveal_pending)return false;
  for(int target=find(state.search_target);target>=0;target=nodes[target].parent)if(target==index)return true;
  return false;
}
bool search_match(int index,const char* query) {
  const auto& node=nodes[index];
  if(!*query || page_for(index)==Page::Count || node.parent<0 || nodes[node.parent].parent<0)return false;

  if(node.kind==Kind::Panel && (std::string_view(node.label)=="General" || std::string_view(node.label)=="Aim assist" || std::string_view(node.label)=="View" || std::string_view(node.label)=="Movement"))return false;
  if(nodes[node.parent].kind==Kind::TogglePanel && std::string_view(node.label)=="Enable")return false;
  return inventory::matches(node.path,query)||inventory::matches(node.label,query);
}
std::vector<int> children(int index) { std::vector<int> out;for(int i=0;i<std::size(nodes);++i)if(index>=0 && nodes[i].parent==index && page_for(i)!=Page::Count && !(nodes[index].kind==Kind::ToggleColor || nodes[index].kind==Kind::MultiColor))out.push_back(i);return out; }
bool matches(int index,const char* query) {
  if(inventory::matches(nodes[index].path,query)||inventory::matches(nodes[index].label,query))return true;
  for(int child:children(index))if(matches(child,query))return true;
  return false;
}
struct Context { MenuRuntime& state; int index; bool* changed; };
void row(MenuRuntime& state,int index,bool& changed);
void panel(void* data) {
  auto& context=*static_cast<Context*>(data);
  for(int child:children(context.index)) {
    if(nodes[context.index].kind==Kind::TogglePanel && std::string_view(nodes[child].label)=="Enable")continue;
    row(context.state,child,*context.changed);
  }
}
void row(MenuRuntime& state,int index,bool& changed) {
  if(index<0)return;
  const auto& node=nodes[index];if(!matches(index,state.search))return;
  auto& v=value(index);const float s=state.scale;
  const auto binding=hotkeys::binding(std::string("Preview/")+node.path);
  bool supported=feature_switches::wired(node.path);
  if(node.kind==Kind::Panel)for(int child=0;child<std::size(nodes);++child) {
    for(int parent=nodes[child].parent;parent>=0;parent=nodes[parent].parent)if(parent==index && feature_switches::wired(nodes[child].path))supported=true;
  }
  const bool unavailable=!supported;
  bool inactive{};bool& toggle_value=unavailable?inactive:v.enabled;
  bool gated=false;
  for(int owner=index;owner>=0;owner=nodes[owner].parent)if(nodes[owner].gate){const int gate=find(nodes[owner].gate);gated|=gate<0 || !feature_switches::wired(nodes[owner].gate) || !value(gate).enabled;}
  const bool locked=(node.kind!=Kind::Panel && unavailable) || gated;
  ImGui::PushID(node.path);
  const bool reveal=reveal_route(state,index);
  Bounds rendered{};
  Context context{state,index,&changed};widgets::PopupContents contents{node.label,panel,&context,430,reveal};
  switch(node.kind) {
  case Kind::Toggle:changed|=widgets::toggle("value",node.label,toggle_value,locked,nullptr,true,true,binding);break;
  case Kind::Number:changed|=widgets::slider("value",node.label,v.number,node.low,node.high,0,node.format,locked,true,true,false,nullptr,binding);break;
  case Kind::Key:changed|=widgets::key_row("value",node.label,v.key,locked);break;
  case Kind::Color:changed|=widgets::color_selector("value",node.label,v.color,v.editor,state.color_palette,locked,true,(!std::string_view(node.path).starts_with("Visuals/World/")||std::string_view(node.path).starts_with("Visuals/World/World ESP/")||std::string_view(node.path).starts_with("Visuals/World/HUD/")));break;
  case Kind::ToggleColor: {
    const int color_index=find(node.color_path);auto& color_value=value(color_index);
    color_value.editor.request_open|=reveal;
    changed|=widgets::toggle_color("value",node.label,toggle_value,color_value.color,color_value.editor,state.color_palette,locked,binding);
    break;
  }
  case Kind::MultiColor: {
    std::vector<std::string> labels;const auto choices=options(node,labels);
    auto& color_value=value(find(node.color_path));color_value.editor.request_open|=reveal;
    const widgets::ColorAffordance color{color_value.color,color_value.editor,state.color_palette};
    changed|=widgets::multi_combo("value",node.label,std::span(v.selections).first(choices.size()),choices,node.label,locked,nullptr,true,binding,&color);
    break;
  }
  case Kind::Choice:case Kind::Multi: {
    std::vector<std::string> labels;const auto choices=options(node,labels);
    if(node.kind==Kind::Choice) {v.choice=std::clamp(v.choice,0,std::max(0,static_cast<int>(choices.size())-1));changed|=widgets::combo("value",node.label,v.choice,choices,locked,nullptr,true,true,binding);}
    else changed|=widgets::multi_combo("value",node.label,std::span(v.selections).first(std::min(choices.size(),v.selections.size())),choices,node.label,locked,nullptr,true,binding);
    break;
  }
  case Kind::TogglePanel: changed|=widgets::toggle("value",node.label,toggle_value,locked,&contents,true,true,binding);break;
  case Kind::Panel: {
    const auto opened=widgets::panel_row("value",node.label,locked);
    rendered=opened.bounds;
    if(!locked && (opened.clicked || (reveal && !ImGui::IsPopupOpen("details"))))ImGui::OpenPopup("details");
    if(widgets::begin_popup("details",opened.bounds,theme::popup_width,430,nullptr,true,true)) {ImGui::BeginDisabled(locked);panel(&context);ImGui::EndDisabled();widgets::end_popup();}
    break;
  }
  }
  if(unavailable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Unavailable: this control has no implemented runtime mapping.");
  if(state.search_target==node.path || (node.color_path && state.search_target==node.color_path)) {
    if(node.kind!=Kind::Panel)rendered={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
    if(state.search_reveal_pending && !ImGui::GetCurrentWindow()->SkipItems) {
      ImGui::SetScrollFromPosY((rendered.min.y+rendered.max.y)*.5f-ImGui::GetWindowPos().y,.5f);state.search_reveal_pending=false;
    }
    auto tint=theme::accent;tint.w*=.4f;
    ImGui::GetWindowDrawList()->AddRect(rendered.min,rendered.max,ImGui::GetColorU32(tint),theme::control_radius*s,0,s);
  }
  ImGui::PopID();
}
void group(MenuRuntime& state,const char* id,const char* title,const std::vector<int>& items,bool& changed,bool trailing_gap=true) {
  if(!std::any_of(items.begin(),items.end(),[&](int i){return matches(i,state.search);}))return;
  if(widgets::begin_group(id,title))for(int index:items)row(state,index,changed);
  widgets::end_group();if(trailing_gap)ImGui::Dummy({0,theme::group_gap*state.scale});
}
void load(const MenuRuntime& state) {
  if(loaded==state.values.preview_tree)return;
  values.clear();loaded=state.values.preview_tree;
  try {
    const auto json=Json::parse(loaded);
    for(int i=0;i<std::size(nodes);++i) {
      if(page_for(i)==Page::Count || !feature_switches::wired(nodes[i].path))continue;
      if(!json.contains(nodes[i].path)||!json[nodes[i].path].is_object())continue;
      const auto& item=json[nodes[i].path];auto& v=value(i);
      if(item.contains("enabled")&&item["enabled"].is_boolean())v.enabled=item["enabled"].get<bool>();
      if(item.contains("number")&&item["number"].is_number()) {const float number=item["number"].get<float>();if(std::isfinite(number))v.number=std::clamp(number,nodes[i].low,nodes[i].high);}
      if(item.contains("choice")&&item["choice"].is_number_integer())v.choice=std::clamp(item["choice"].get<int>(),0,15);
      if(item.contains("key")&&item["key"].is_number_integer()) {auto key=item["key"].get<int>();if(key>=ImGuiKey_NamedKey_BEGIN&&key<ImGuiKey_NamedKey_END)v.key=static_cast<ImGuiKey>(key);}
      if(item.contains("color")&&item["color"].is_array()&&item["color"].size()==4)for(int c=0;c<4;++c)if(item["color"][c].is_number()) {auto n=item["color"][c].get<float>();if(std::isfinite(n))(&v.color.x)[c]=std::clamp(n,0.f,1.f);}
      if(item.contains("selected")&&item["selected"].is_array())for(int c=0;c<16&&c<item["selected"].size();++c)if(item["selected"][c].is_boolean())v.selections[c]=item["selected"][c].get<bool>();
    }
  }catch(const Json::exception&) { /* Older and absent preview state uses tolerant defaults. */ }
  const int master=find("Visuals/Player/Enabled");
  const auto json=Json::parse(loaded,nullptr,false);
  if(master>=0 && (!json.is_object() || !json.contains(nodes[master].path))){auto projected=state.values;feature_switches::project(projected);value(master).enabled=projected.cosmetics.player_visuals.enabled;}
  auto projected=state.values;feature_switches::project(projected);const auto target=visuals::teammate_options(projected.cosmetics.player_visuals);
  for(const auto& option:visuals::esp_toggles){const auto path=std::string("Visuals/Player/Teammates/")+option.label;const int index=find(path);if(index>=0&&(!json.is_object()||!json.contains(path)))value(index).enabled=target.*option.target;}
  const int flags=find("Visuals/Player/Teammates/Flags");if(flags>=0&&(!json.is_object()||!json.contains(nodes[flags].path)))std::copy(target.flags.begin(),target.flags.end(),value(flags).selections.begin());

}
Json controls_json() {
  Json json=Json::parse(loaded,nullptr,false);if(!json.is_object())json=Json::object();
  for(auto it=json.begin();it!=json.end();)if(it.key().starts_with("Combat/Rage"))it=json.erase(it);else ++it;
  for(const auto& [i,v]:values)if(feature_switches::wired(nodes[i].path))json[nodes[i].path]={{"enabled",v.enabled},{"number",v.number},{"choice",v.choice},{"key",static_cast<int>(v.key)},{"color",{v.color.x,v.color.y,v.color.z,v.color.w}},{"selected",v.selections}};
  return json;
}
void save(MenuRuntime& state,bool changed) {
  if(!changed)return;
  const auto json=controls_json();
  const auto before=state.values;
  loaded=state.values.preview_tree=json.dump();state.dirty=true;
  state.apply_context="Feature";
  const auto mirror=json.find("Misc/Inventory/Apply to both teams");
  const bool both=mirror!=json.end()&&mirror->value("enabled",false);
  const bool accepted=mirror!=json.end()&&both!=before.inventory.both_teams ? set_both_teams(state,state.inventory.side,state.inventory.selected[state.inventory.side],both) : submit_features(state);
  if(!accepted){state.values=before;loaded.clear();}
  state.apply_context.clear();

}
}
void evaluate(MenuRuntime& state) {
  if(!state.gameplay_wired)return;
  load(state);bool changed=false;
  for(int i=0;i<std::size(nodes);++i) {
    const auto& n=nodes[i];if(!feature_switches::wired(n.path) || page_for(i)==Page::Count)continue;
    if(std::string_view(n.path).starts_with("Combat/Legit/")&&std::string_view(n.path)!="Combat/Legit/General/Master switch")continue;
    auto& v=value(i);const auto id=hotkeys::binding(std::string("Preview/")+n.path);auto& binding=hotkeys::option(id);
    bool disabled=false;
    for(int owner=i;owner>=0;owner=nodes[owner].parent)if(nodes[owner].gate){const int gate=find(nodes[owner].gate);disabled|=gate<0 || !feature_switches::wired(nodes[owner].gate) || !value(gate).enabled;}
    if(n.kind==Kind::Toggle || n.kind==Kind::TogglePanel || n.kind==Kind::ToggleColor){binding.kind=hotkeys::Kind::Boolean;changed|=hotkeys::evaluate(id,v.enabled,disabled);}
    else if(n.kind==Kind::Number){binding.kind=hotkeys::Kind::Number;binding.minimum=n.low;binding.maximum=n.high;double number=v.number;if(hotkeys::evaluate(id,number,disabled)){v.number=static_cast<float>(number);changed=true;}}
    else if(n.kind==Kind::Choice || n.kind==Kind::Multi){std::vector<std::string> labels;const auto list=options(n,labels);binding.choices=labels;binding.choice_disabled.assign(labels.size(),false);for(std::size_t option_index=0;option_index<list.size();++option_index)binding.choice_disabled[option_index]=list[option_index].disabled;
      if(n.kind==Kind::Multi){binding.kind=hotkeys::Kind::Multi;changed|=hotkeys::evaluate(id,std::span(v.selections).first(std::min(labels.size(),v.selections.size())),disabled);}
      else{binding.kind=hotkeys::Kind::Selection;binding.minimum=0;binding.maximum=labels.empty()?0:static_cast<double>(labels.size()-1);binding.step=1;double number=v.choice;if(hotkeys::evaluate(id,number,disabled)){v.choice=static_cast<int>(number);changed=true;}}
    }
  }
  save(state,changed);
  legit_profiles::evaluate(state);
}
int search_result_count(const MenuRuntime& state) {
  int count=0;for(int i=0;i<std::size(nodes);++i)if(search_match(i,state.search))++count;return count;
}
bool search_results(MenuRuntime& state,bool select_first) {
  bool found=false,selected=false;
  for(int i=0;i<std::size(nodes);++i) {
    const auto& node=nodes[i];
    const Page page=page_for(i);
    if(!search_match(i,state.search))continue;
    found=true;ImGui::PushID(node.path);
    std::string location;
    for(int owner=node.parent;owner>=0;owner=nodes[owner].parent)location=std::string(nodes[owner].label)+(location.empty()?"":" / "+location);
    const bool clicked=widgets::popup_item(node.label,false,false,location.c_str());
    if(!selected && (clicked || select_first)) {
      state.selected=page;state.search_target=node.path;state.search_reveal_pending=true;state.search[0]='\0';
      ImGui::CloseCurrentPopup();selected=true;select_first=false;
    }
    ImGui::PopID();
    if(selected)break;
  }
  return found;
}
void player_selector(MenuRuntime& state,float width){
  if(player_owner!=&state){player_owner=&state;player_preview={};}
  if(widgets::begin_dropdown("##player_target",player_preview.target==2?"Teammates":"Enemies",2,width)){
    ImGui::PushID("Enemies");if(widgets::popup_item("Enemies",player_preview.target!=2)){player_preview.target=0;player_preview.selected=esp_preview::Element::Count;}
    ImGui::PopID();ImGui::PushID("Teammates");if(widgets::popup_item("Teammates",player_preview.target==2)){player_preview.target=2;player_preview.selected=esp_preview::Element::Count;}
    ImGui::PopID();widgets::end_dropdown();
  }
  state.title_bounds={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
}
void render(MenuRuntime& state) {
  load(state);bool changed=false;const float s=state.scale;
  ImGui::BeginChild("tree_controls",{0,0},ImGuiChildFlags_NavFlattened);
  if(state.selected==Page::Legit){legit_profiles::render(state);ImGui::EndChild();return;}
  const int columns=ImGui::GetContentRegionAvail().x>=440*s?2:1;
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,{theme::column_gap*s*.5f,0});
  if(ImGui::BeginTable("tree_columns",columns,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_NoPadOuterX)) {
    ImGui::TableNextColumn();
    const auto page=state.selected;
    if(page==Page::World) {
      group(state,"view","View",children(find("Visuals/World/View")),changed);
      if(columns==2){
        ImGui::TableNextColumn();group(state,"hud","HUD",children(find("Visuals/World/HUD")),changed);
        ImGui::TableNextRow();ImGui::TableNextColumn();
      }
      group(state,"world_esp","World ESP",children(find("Visuals/World/World ESP")),changed,columns==1);
      ImGui::TableNextColumn();
      if(columns==1)group(state,"hud","HUD",children(find("Visuals/World/HUD")),changed);
      group(state,"world_misc","Misc",children(find("Visuals/World/Miscellaneous")),changed,false);
    } else if(page==Page::Player) {
      auto& preview=player_preview;
      constexpr const char* toggle_paths[]{"Visuals/Player/ESP items/Box","Visuals/Player/ESP items/Name","Visuals/Player/ESP items/Health bar","Visuals/Player/ESP items/Health number","Visuals/Player/ESP items/Ammo bar","Visuals/Player/ESP items/Armor bar","Visuals/Player/ESP items/Weapon","Visuals/Player/ESP items/Distance","Visuals/Player/ESP items/Flags","Visuals/Player/ESP items/Snapline","Visuals/Player/ESP items/Skeleton","Visuals/Player/ESP items/Hitboxes","Visuals/Player/Off-screen arrows","Visuals/Player/Sound ESP"};
      constexpr const char* color_keys[]{"box_enemy_color","name_color","health_low_color","health_number_color","ammo_color","armor_color","weapon_color","distance_color","flags_color","snapline_color","skeleton_color","hitbox_color","arrow_color","sound_color"};
      if(state.search_reveal_pending&&state.search_target.starts_with("Visuals/Player/"))preview.target=state.search_target.starts_with("Visuals/Player/Teammates/")?2:0;
      const bool teammate=preview.target==2;
      const int esp=find(teammate?"Visuals/Player/Teammates":"Visuals/Player/ESP items");
      const int master=find(teammate?"Visuals/Player/Teammates/Enabled":"Visuals/Player/Enabled");
      const bool disabled=!value(master).enabled;
      std::optional<visuals::EspLayout> popup_layout;
      if(widgets::begin_group("player_main",teammate?"Teammates":"Enemies")){
        row(state,master,changed);
        ImGui::BeginDisabled(teammate);
        row(state,find("Visuals/Player/Off-screen arrows"),changed);row(state,find("Visuals/Player/Sound ESP"),changed);
        ImGui::EndDisabled();
        const auto opened=widgets::panel_row("esp_options_button","ESP options",disabled);
        if(opened.clicked||state.search_reveal_pending)preview.element_options=false;
        const char* popup_id=preview.element_options?"player_esp_element":"player_esp_options";
        const Bounds popup_anchor=preview.element_options?preview.options_anchor:opened.bounds;
        if(!disabled&&(opened.clicked||preview.options_open||(state.search_reveal_pending&&(reveal_route(state,esp)||(!teammate&&state.search_target.starts_with("Visuals/Player/Element colors/"))))))ImGui::OpenPopup(popup_id);
        preview.options_open=false;
        if(widgets::begin_popup(popup_id,popup_anchor,theme::popup_width,430,preview.element_options?esp_preview::label(preview.selected):"ESP options",true,true,false,preview.element_options)){
          ImGui::BeginDisabled(disabled);
          constexpr std::array<esp_preview::Element,visuals::esp_element_count> movable{esp_preview::Element::Name,esp_preview::Element::Health,esp_preview::Element::HealthNumber,esp_preview::Element::Ammo,esp_preview::Element::Armor,esp_preview::Element::Weapon,esp_preview::Element::Distance,esp_preview::Element::Flags};
          const auto selected_element=std::find(movable.begin(),movable.end(),preview.selected);
          if(selected_element!=movable.end()){
            const auto index=static_cast<std::size_t>(selected_element-movable.begin());
            auto layout=teammate?visuals::teammate_options(state.values.cosmetics.player_visuals).layout:state.values.cosmetics.player_visuals.esp_layout;
            int side=static_cast<int>(layout[index].side),anchor=static_cast<int>(layout[index].anchor);
            const bool flags=preview.selected==esp_preview::Element::Flags;
            const std::array sides{widgets::Option{"Top",flags},widgets::Option{"Bottom",flags},widgets::Option{"Left"},widgets::Option{"Right"}};
            const std::array anchors{widgets::Option{"Automatic"},widgets::Option{"Start"},widgets::Option{"Center"},widgets::Option{"End"}};
            bool edited=widgets::combo("element_side","Placement",side,sides,false,nullptr,false,true);
            edited|=widgets::combo("element_anchor","Alignment",anchor,anchors,false,nullptr,false,true);
            if(edited){visuals::move_esp_element(layout,static_cast<visuals::EspElement>(index),static_cast<visuals::EspSide>(side),layout[index].order);layout[index].anchor=static_cast<visuals::EspAnchor>(anchor);popup_layout=layout;}
            ImGui::Separator();
          }

          if(preview.element_options){
            const auto selected=static_cast<std::size_t>(preview.selected);
            if(selected<12){
              const auto path=teammate?std::string("Visuals/Player/Teammates/")+(selected==0?"Bounding box":nodes[find(toggle_paths[selected])].label):std::string(toggle_paths[selected]);
              if(selected==8&&teammate)row(state,find("Visuals/Player/Teammates/Flags"),changed);
              else row(state,find(path),changed);
              ImGui::BeginDisabled(teammate);
              const char* key=selected==0&&teammate?"box_ally_color":color_keys[selected];row(state,find(std::string("Visuals/Player/Element colors/")+key),changed);
              if(selected==2)row(state,find("Visuals/Player/Element colors/health_full_color"),changed);
              if(selected==0)for(const auto* option:{"Visuals/Player/ESP items/Box style","Visuals/Player/ESP items/Box opacity","Visuals/Player/ESP items/Thickness"})row(state,find(option),changed);
              if(selected==6)row(state,find("Visuals/Player/ESP items/Weapon style"),changed);
              if(selected==8&&!teammate)row(state,find("Visuals/Player/ESP items/Flag types"),changed);
              ImGui::EndDisabled();
            }
          }else if(teammate){for(int child:children(esp))if(child!=master){
            if(nodes[child].kind==Kind::Toggle&&!reveal_route(state,child)){
              ImGui::PushID(nodes[child].path);ImGui::BeginDisabled(!feature_switches::wired(nodes[child].path));
              if(widgets::popup_item(nodes[child].label,value(child).enabled,true)){value(child).enabled=!value(child).enabled;changed=true;}
              ImGui::EndDisabled();ImGui::PopID();
            }else row(state,child,changed);
          }}
          else {
            for(int element=0;element<12;++element){const int control=find(toggle_paths[element]);ImGui::PushID(toggle_paths[element]);
              const bool selected=value(control).enabled;
              ImGui::BeginDisabled(!feature_switches::wired(toggle_paths[element]));
              if(widgets::popup_item(element==0?"Bounding box":nodes[control].label,selected,true)){value(control).enabled=!selected;preview.selected=static_cast<esp_preview::Element>(element);changed=true;}
              ImGui::EndDisabled();ImGui::PopID();
            }
            const auto colors=widgets::panel_row("esp_colors","Colors");
            const bool reveal_colors=state.search_reveal_pending&&state.search_target.starts_with("Visuals/Player/Element colors/");
            if(colors.clicked||reveal_colors)ImGui::OpenPopup("esp_colors");
            if(widgets::begin_popup("esp_colors",colors.bounds,theme::popup_width,430,"Colors",true,true,false,false)){
              for(const auto* key:color_keys)row(state,find(std::string("Visuals/Player/Element colors/")+key),changed);
              row(state,find("Visuals/Player/Element colors/health_full_color"),changed);
              row(state,find("Visuals/Player/Element colors/box_ally_color"),changed);
              widgets::end_popup();
            }
            for(const auto* path:{"Visuals/Player/ESP items/Flag types","Visuals/Player/ESP items/Visible only","Visuals/Player/ESP items/Distance limit","Visuals/Player/ESP items/Thickness"})row(state,find(path),changed);
            if(state.search_reveal_pending)for(int child:children(esp))if(state.search_target==nodes[child].path)row(state,child,changed);
          }
          ImGui::EndDisabled();widgets::end_popup();
        }
      }widgets::end_group();ImGui::Dummy({0,theme::group_gap*s});
      ImGui::BeginDisabled(teammate);
      group(state,"player_model","Player model",{find("Visuals/Player/Visible chams"),find("Visuals/Player/Hidden chams"),find("Visuals/Player/Ragdolls"),find("Visuals/Player/Glow")},changed);
      ImGui::EndDisabled();
      ImGui::TableNextColumn();
      auto projected=state.values;projected.preview_tree=controls_json().dump();feature_switches::project(projected);
      const auto config=visuals::for_target(projected.cosmetics.player_visuals,!teammate);auto layout=config.esp_layout;
      ImGui::BeginDisabled(disabled);const bool dragged=esp_preview::draw(state,config,preview,&layout);
      if(popup_layout)layout=*popup_layout;const bool layout_changed=dragged||popup_layout.has_value();ImGui::EndDisabled();
      if(layout_changed&&!disabled){const auto before=state.values;
        if(teammate){state.values.cosmetics.player_visuals.separate_teammate_esp=true;state.values.cosmetics.player_visuals.teammate_esp=visuals::teammate_options(projected.cosmetics.player_visuals);state.values.cosmetics.player_visuals.teammate_esp.layout=layout;}
        else state.values.cosmetics.player_visuals.esp_layout=layout;
        state.dirty=true;commit_feature_edit(state,before);
      }
    } else if(page==Page::Misc) {
      group(state,"movement","Movement",children(find("Misc/Misc/Movement")),changed);
      ImGui::TableNextColumn();group(state,"misc","Other",children(find("Misc/Misc/Other")),changed);
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();ImGui::EndChild();save(state,changed);
}
void inventory_options(MenuRuntime& state) {
  load(state);bool changed=false;
  const int index=find("Misc/Inventory");
  ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,{0,.5f});
  const bool preview_options=widgets::button("Inventory options",{160*state.scale,theme::input_height*state.scale});
  ImGui::PopStyleVar();
  const Bounds anchor{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  if(preview_options || (reveal_route(state,index) && !ImGui::IsPopupOpen("inventory_tree_options")))ImGui::OpenPopup("inventory_tree_options");
  if(widgets::begin_popup("inventory_tree_options",anchor,theme::popup_width,430,"Inventory",true,true)) {
    for(int child:children(index)) {
      if(std::string_view(nodes[child].path)=="Misc/Inventory/Apply to both teams") {
        const int team=state.inventory.side,slot=state.inventory.selected[team];ItemDraft draft;
        if(!read_inventory(state,team,slot,draft) || !inventory_can_mirror(slot,draft.definition))continue;
      }
      row(state,child,changed);
    }
    widgets::end_popup();
  }
  save(state,changed);
}
}
