#include "core/menu/feature_tree.hpp"
#include "core/menu/feature_catalog.hpp"
#include "core/menu/feature_switches.hpp"
#include "core/menu/widgets.hpp"
#include "core/menu/hotkeys.hpp"
#include "core/menu/config_io.hpp"
#include "core/menu/inventory.hpp"
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
std::string loaded;
using Json=nlohmann::json;
Value& value(int index) {
  auto [it,inserted]=values.try_emplace(index);
  if(inserted) it->second.number=nodes[index].initial;
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
  if(path.starts_with("Combat/Rage/Anti-Aim"))return Page::Count;
  constexpr std::string_view movement="Misc/Misc/Movement/";
  if(path.starts_with(movement) && path!=std::string(movement)+"Bunny hop" && path!=std::string(movement)+"Strafe assist" && path!=std::string(movement)+"Bounce chance" && path!=std::string(movement)+"Bounce limit" && path!=std::string(movement)+"Assist strength")return Page::Count;
  if(path.starts_with("Combat/Legit"))return Page::Legit;
  if(path.starts_with("Combat/Rage"))return Page::Rage;
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
  return inventory::matches(node.path,query);
}
std::vector<int> children(int index) { std::vector<int> out;for(int i=0;i<std::size(nodes);++i)if(nodes[i].parent==index && page_for(i)!=Page::Count)out.push_back(i);return out; }
bool matches(int index,const char* query) {
  if(inventory::matches(nodes[index].path,query))return true;
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
  const auto& node=nodes[index];if(!matches(index,state.search))return;
  auto& v=value(index);const float s=state.scale;
  const auto binding=hotkeys::binding(std::string("Preview/")+node.path);
  const bool locked=state.gameplay_wired && std::string_view(node.path).starts_with("Combat/Rage/") && state.settings.safe_mode!=2;
  ImGui::PushID(node.path);
  const bool reveal=reveal_route(state,index);
  Bounds rendered{};
  Context context{state,index,&changed};widgets::PopupContents contents{node.label,panel,&context,430,reveal};
  switch(node.kind) {
  case Kind::Toggle:changed|=widgets::toggle("value",node.label,v.enabled,locked,nullptr,true,true,binding);break;
  case Kind::Number:changed|=widgets::slider("value",node.label,v.number,node.low,node.high,0,node.format,locked,true,true,false,nullptr,binding);break;
  case Kind::Key:changed|=widgets::key_row("value",node.label,v.key);break;
  case Kind::Color:changed|=widgets::color_selector("value",node.label,v.color,v.editor,state.color_palette,false,true);break;
  case Kind::Choice:case Kind::Multi: {
    std::vector<std::string> labels;const auto choices=options(node,labels);
    if(node.kind==Kind::Choice) {v.choice=std::clamp(v.choice,0,std::max(0,static_cast<int>(choices.size())-1));changed|=widgets::combo("value",node.label,v.choice,choices,locked,nullptr,true,true,binding);}
    else changed|=widgets::multi_combo("value",node.label,std::span(v.selections).first(std::min(choices.size(),v.selections.size())),choices,node.label,locked,nullptr,true,binding);
    break;
  }
  case Kind::TogglePanel: changed|=widgets::toggle("value",node.label,v.enabled,locked,&contents,true,true,binding);break;
  case Kind::Panel: {
    const auto opened=widgets::panel_row("value",node.label);
    rendered=opened.bounds;
    if(opened.clicked || (reveal && !ImGui::IsPopupOpen("details")))ImGui::OpenPopup("details");
    if(widgets::begin_popup("details",opened.bounds,theme::popup_width,430,nullptr,true,true)) {panel(&context);widgets::end_popup();}
    break;
  }
  }
  if(state.search_target==node.path) {
    if(node.kind!=Kind::Panel)rendered={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
    if(state.search_reveal_pending && !ImGui::GetCurrentWindow()->SkipItems) {
      ImGui::SetScrollFromPosY((rendered.min.y+rendered.max.y)*.5f-ImGui::GetWindowPos().y,.5f);state.search_reveal_pending=false;
    }
    auto tint=theme::accent;tint.w*=.4f;
    ImGui::GetWindowDrawList()->AddRect(rendered.min,rendered.max,ImGui::GetColorU32(tint),theme::control_radius*s,0,s);
  }
  ImGui::PopID();
}
void group(MenuRuntime& state,const char* id,const char* title,const std::vector<int>& items,bool& changed) {
  if(!std::any_of(items.begin(),items.end(),[&](int i){return matches(i,state.search);}))return;
  if(widgets::begin_group(id,title))for(int index:items)row(state,index,changed);
  widgets::end_group();ImGui::Dummy({0,theme::group_gap*state.scale});
}
void load(const MenuRuntime& state) {
  if(loaded==state.values.preview_tree)return;
  values.clear();loaded=state.values.preview_tree;
  try {
    const auto json=Json::parse(loaded);
    for(int i=0;i<std::size(nodes);++i) {
      if(page_for(i)==Page::Count)continue;
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
}
void save(MenuRuntime& state,bool changed) {
  if(!changed)return;
  Json json=Json::object();
  for(const auto& [i,v]:values)json[nodes[i].path]={{"enabled",v.enabled},{"number",v.number},{"choice",v.choice},{"key",static_cast<int>(v.key)},{"color",{v.color.x,v.color.y,v.color.z,v.color.w}},{"selected",v.selections}};
  const auto before=state.values;
  loaded=state.values.preview_tree=json.dump();state.dirty=true;
  state.apply_context="Feature";
  if(!submit_features(state)){state.values=before;loaded.clear();}
  state.apply_context.clear();

}
}
void evaluate(MenuRuntime& state) {
  if(!state.gameplay_wired)return;
  load(state);bool changed=false;
  for(int i=0;i<std::size(nodes);++i) {
    const auto& n=nodes[i];if(!feature_switches::wired(n.path) || page_for(i)==Page::Count)continue;
    auto& v=value(i);const auto id=hotkeys::binding(std::string("Preview/")+n.path);auto& binding=hotkeys::option(id);
    const bool disabled=std::string_view(n.path).starts_with("Combat/Rage/") && state.settings.safe_mode!=2;
    if(n.kind==Kind::Toggle || n.kind==Kind::TogglePanel){binding.kind=hotkeys::Kind::Boolean;changed|=hotkeys::evaluate(id,v.enabled,disabled);}
    else if(n.kind==Kind::Number){binding.kind=hotkeys::Kind::Number;binding.minimum=n.low;binding.maximum=n.high;double number=v.number;if(hotkeys::evaluate(id,number,disabled)){v.number=static_cast<float>(number);changed=true;}}
    else if(n.kind==Kind::Choice || n.kind==Kind::Multi){std::vector<std::string> labels;const auto list=options(n,labels);binding.choices=labels;binding.choice_disabled.assign(labels.size(),false);for(std::size_t option_index=0;option_index<list.size();++option_index)binding.choice_disabled[option_index]=list[option_index].disabled;
      if(n.kind==Kind::Multi){binding.kind=hotkeys::Kind::Multi;changed|=hotkeys::evaluate(id,std::span(v.selections).first(std::min(labels.size(),v.selections.size())),disabled);}
      else{binding.kind=hotkeys::Kind::Selection;binding.minimum=0;binding.maximum=labels.empty()?0:static_cast<double>(labels.size()-1);binding.step=1;double number=v.choice;if(hotkeys::evaluate(id,number,disabled)){v.choice=static_cast<int>(number);changed=true;}}
    }
  }
  save(state,changed);
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
    std::string location=node.path;location.resize(location.find_last_of('/'));
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
void render(MenuRuntime& state) {
  load(state);bool changed=false;const float s=state.scale;
  ImGui::BeginChild("tree_controls",{0,0},ImGuiChildFlags_NavFlattened);
  const int columns=ImGui::GetContentRegionAvail().x>=440*s?2:1;
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,{theme::column_gap*s*.5f,0});
  if(ImGui::BeginTable("tree_columns",columns,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_NoPadOuterX)) {
    ImGui::TableNextColumn();
    const auto page=state.selected;
    if(page==Page::Rage) {
      const auto aim=children(find("Combat/Rage/Aimbot"));
      std::vector<int> main,selection,other;
      for(int i=0;i<aim.size();++i)(i<=6?main:i<=15?selection:other).push_back(aim[i]);
      group(state,"rage_main","Main",main,changed);group(state,"rage_selection","Selection",selection,changed);
      ImGui::TableNextColumn();group(state,"rage_other","Other",other,changed);
      const int peek=find("Combat/Rage/Quick peek assist");group(state,"peek","Quick peek assist",{peek},changed);

    } else if(page==Page::Legit) {
      group(state,"general","General",children(find("Combat/Legit/General")),changed);
      group(state,"aim","Aim assist",children(find("Combat/Legit/Aim assist")),changed);
      ImGui::TableNextColumn();group(state,"trigger","Triggerbot",children(find("Combat/Legit/Triggerbot")),changed);

    } else if(page==Page::World) {
      group(state,"view","View",children(find("Visuals/World/View")),changed);
      group(state,"world_esp","World ESP",children(find("Visuals/World/World ESP")),changed);
      ImGui::TableNextColumn();group(state,"hud","HUD",children(find("Visuals/World/HUD")),changed);
      group(state,"world_misc","Miscellaneous",children(find("Visuals/World/Miscellaneous")),changed);
    } else if(page==Page::Player) {
      auto items=children(find("Visuals/Player"));std::vector<int> left,right;
      for(int i:items)(std::string_view(nodes[i].label)=="ESP items"||std::string_view(nodes[i].label)=="ESP item editor"?right:left).push_back(i);
      group(state,"players","Player",left,changed);ImGui::TableNextColumn();
      group(state,"esp","ESP elements",right,changed);
      if(!state.search[0]) {
        const auto p=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x;const ImVec2 end{p.x+width,p.y+245*s};
        auto* draw=ImGui::GetWindowDrawList();draw->AddRectFilled(p,end,ImGui::GetColorU32(theme::section_surface),theme::section_radius*s);
        const float x=p.x+width*.5f,y=p.y+55*s;const auto color=ImGui::GetColorU32(theme::secondary);
        draw->AddCircleFilled({x,y},14*s,color);draw->AddRectFilled({x-21*s,y+21*s},{x+21*s,y+98*s},color,12*s);
        draw->AddLine({x-19*s,y+35*s},{x-40*s,y+95*s},color,10*s);draw->AddLine({x+19*s,y+35*s},{x+40*s,y+95*s},color,10*s);
        draw->AddLine({x-10*s,y+90*s},{x-17*s,y+155*s},color,12*s);draw->AddLine({x+10*s,y+90*s},{x+17*s,y+155*s},color,12*s);
        const auto accent=ImGui::GetColorU32(theme::accent);draw->AddRect({x-52*s,y-22*s},{x+52*s,y+168*s},accent,2*s);
        widgets::caption({p.x+14*s,p.y+14*s},"ESP PREVIEW",s);
        ImGui::Dummy({width,245*s});
      }
    } else if(page==Page::Misc) {
      const int misc=find("Misc/Misc");const auto all=children(misc);std::vector<int> rest;
      for(int i:all)if(std::string_view(nodes[i].label)!="Movement")rest.push_back(i);
      group(state,"movement","Movement",children(find("Misc/Misc/Movement")),changed);
      ImGui::TableNextColumn();group(state,"misc","Features",rest,changed);
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
    for(int child:children(index))row(state,child,changed);
    widgets::end_popup();
  }
  save(state,changed);
}
}
