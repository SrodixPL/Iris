#include "core/menu/hotkeys.hpp"
#include "core/menu/feature_catalog.hpp"
#include "core/menu/feature_switches.hpp"
#include "core/menu/widgets.hpp"
#include "core/menu/theme.hpp"
#include <imgui_internal.h>
#include "ext/json/json.hpp"
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <climits>
#include <string_view>
#include <fstream>
#include <cmath>
#include <memory>
#include <utility>
namespace iris::ui::hotkeys {
namespace {
std::map<ImGuiID,Option> data;
std::string last_error;
bool dirty{};
ImFont* icon_font{};
const char* modes[]{"Toggle","Hold","Hold Off"};
const char* key_name(ImGuiKey key) { return key==ImGuiKey_None ? "Unbound" : ImGui::GetKeyName(key); }
bool menu_row(const char* id,const char* label,bool arrow=false,bool destructive=false) {
  ImGui::PushID(id);
  const auto p=ImGui::GetCursorScreenPos();
  const float s=ImGui::GetStyle().FontScaleMain, width=ImGui::GetContentRegionAvail().x;
  ImGui::PushItemFlag(ImGuiItemFlags_NoWindowHoverableCheck,true);
  const bool clicked=widgets::button("##row",{width,theme::hotkey_row*s},ImGuiButtonFlags_NoHoveredOnFocus);
  ImGui::PopItemFlag();
  auto* draw=ImGui::GetWindowDrawList();
  const auto tint=destructive ? theme::destructive : theme::text;
  if(icon_font && std::string_view(id)!="Toggle" && std::string_view(id)!="Hold" && std::string_view(id)!="Hold Off") {
    const std::string_view name=id;
    const char* glyph=name=="all" ? "\xee\xa3\xbd" : name=="new" ? "\xee\x9c\x90" :
      name=="reset" ? "\xee\x9d\xb7" : name=="delete" ? "\xee\x9d\x8d" : "\xee\x9d\xa5";
    ImGui::PushFont(icon_font,theme::hotkey_icon);
    widgets::icon_text(draw,{p.x+theme::popup_item_padding*s+theme::hotkey_icon*s*.5f,p.y+theme::hotkey_row*s*.5f},ImGui::GetColorU32(destructive ? theme::destructive : theme::secondary),glyph);
    ImGui::PopFont();
  }
  const bool mode_row=std::string_view(id)=="Toggle" || std::string_view(id)=="Hold" || std::string_view(id)=="Hold Off";
  const ImVec2 text_at{p.x+(!mode_row && icon_font ? theme::profile_icon_column+theme::popup_item_padding : theme::popup_item_padding)*s,widgets::text_y(p.y,theme::hotkey_row*s)};
  ImGui::PushStyleColor(ImGuiCol_Text,tint);
  ImGui::RenderTextEllipsis(draw,text_at,{p.x+width-(theme::popup_item_padding+(arrow ? theme::popup_check_space : 0))*s,p.y+theme::hotkey_row*s},p.x+width-(theme::popup_item_padding+(arrow ? theme::popup_check_space : 0))*s,label,nullptr,nullptr);
  ImGui::PopStyleColor();
  if(arrow) {
    const float x=p.x+width-(theme::popup_item_padding+theme::hotkey_chevron*.5f)*s,y=p.y+theme::hotkey_row*s*.5f;
    const float half=theme::hotkey_chevron*s*.5f;
    draw->AddLine({x-half*.5f,y-half},{x+half*.5f,y},ImGui::GetColorU32(theme::secondary),1.2f*s);
    draw->AddLine({x+half*.5f,y},{x-half*.5f,y+half},ImGui::GetColorU32(theme::secondary),1.2f*s);
  }
  ImGui::PopID();
  return clicked;
}
Bounds item_bounds() { return {ImGui::GetItemRectMin(),ImGui::GetItemRectMax()}; }
float menu_width(const char* label,bool arrow=false) {
  return ImGui::CalcTextSize(label).x/ImGui::GetStyle().FontScaleMain+theme::profile_icon_column+2*theme::popup_item_padding+2*theme::popup_padding+(arrow ? theme::hotkey_chevron+theme::setting_gap : 0);
}
void divider() {
  const float s=ImGui::GetStyle().FontScaleMain;
  const auto at=ImGui::GetCursorScreenPos();
  const float y=at.y+theme::hotkey_divider_margin*s;
  ImGui::GetWindowDrawList()->AddLine({at.x,y},{at.x+ImGui::GetContentRegionAvail().x,y},ImGui::GetColorU32(theme::border),theme::hotkey_divider*s);
  ImGui::SetCursorScreenPos({at.x,at.y+(2*theme::hotkey_divider_margin+theme::hotkey_divider)*s});
}
void field_label(const char* label,ImVec2 at) {
  const float s=ImGui::GetStyle().FontScaleMain;
  ImGui::GetWindowDrawList()->AddText({at.x,widgets::text_y(at.y,theme::row_height*s)},ImGui::GetColorU32(theme::secondary),label);
  ImGui::SetCursorScreenPos({at.x+theme::hotkey_label*s,at.y+(theme::row_height-theme::hotkey_control)*s*.5f});
}
bool conflict(ImGuiID id,ImGuiKey key) {
  if(key==ImGuiKey_None) return false;
  for(const auto& [other,option] : data) if(other!=id)
    for(const auto& hotkey : option.entries) if(hotkey.key==key) return true;
  return false;
}
void discard_draft(Option& option) {
  if(!option.draft) return;
  const auto draft=std::find_if(option.entries.begin(),option.entries.end(),[](const Hotkey& hotkey) { return hotkey.key==ImGuiKey_None; });
  if(draft!=option.entries.end()) {
    const auto index=static_cast<size_t>(draft-option.entries.begin());
    if(index==option.editing) { option.last_detail=*draft; option.has_last_detail=true; }
    option.entries.erase(draft); if(option.editing>index) --option.editing;
    dirty=true;
  }
  option.draft=false; option.listen=false;
}
void submenu(ImGuiID id,Option& option,Bounds anchor,bool boolean) {
  const float s=ImGui::GetStyle().FontScaleMain;
  anchor.min.y-=theme::hotkey_padding_y*s;
  const bool warning=option.editing<option.entries.size() && conflict(id,option.entries[option.editing].key);
  const float height=(warning ? theme::body_size+theme::hotkey_spacing : 0)+2*theme::hotkey_padding_y+4*theme::row_height+2*theme::hotkey_divider_margin+theme::hotkey_divider+theme::hotkey_row;
  if(!widgets::begin_popup("hotkey_detail",anchor,theme::hotkey_detail_width,height,nullptr,true,true,true)) return;
  const bool closing=widgets::popup_closing();
  if((closing && option.has_last_detail) || option.editing<option.entries.size()) {
    auto& hotkey=closing ? option.last_detail : option.entries[option.editing];
    auto at=ImGui::GetCursorScreenPos();
    field_label("Key",at);
    ImGui::PushStyleColor(ImGuiCol_Button,theme::surface);
    if(widgets::key_selector("key",hotkey.key,false,option.listen,theme::hotkey_control)) { hotkey.active=false; option.draft=false; dirty=true; }
    option.listen=false;
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos({at.x,at.y+theme::row_height*s}); ImGui::Dummy({0,0});
    at=ImGui::GetCursorScreenPos();
    field_label("Mode",at);
    const int mode_count=boolean ? 3 : 2;
    if(widgets::begin_dropdown("###mode",modes[static_cast<int>(hotkey.mode)],mode_count,ImGui::GetContentRegionAvail().x)) {
      for(int index=0;index<mode_count;++index) {
        ImGui::PushID(index);
        if(widgets::popup_item(modes[index],static_cast<int>(hotkey.mode)==index)) { hotkey.mode=static_cast<Mode>(index); hotkey.active=false; dirty=true; }
        ImGui::PopID();
      }
      widgets::end_dropdown();
    }
    ImGui::SetCursorScreenPos({at.x,at.y+theme::row_height*s}); ImGui::Dummy({0,0});
    if(boolean) {
      if(widgets::toggle("value","Value",hotkey.value,hotkey.mode==Mode::Toggle,nullptr,false,true)) dirty=true;
    } else if(option.kind==Kind::Number) {
      float number=static_cast<float>(hotkey.number);
      const char* format=option.step>=1 || option.maximum-option.minimum>2 ? "%.0f" : "%.3f";
      if(widgets::slider("value","Value",number,static_cast<float>(option.minimum),static_cast<float>(option.maximum),static_cast<float>(option.step),format,false,false)) { hotkey.number=number; dirty=true; }
    } else {
      std::vector<widgets::Option> choices;
      for(size_t i=0;i<option.choices.size();++i) choices.push_back({option.choices[i].c_str(),i<option.choice_disabled.size() && option.choice_disabled[i]});
      if(option.kind==Kind::Selection) {
        int selected=static_cast<int>(hotkey.number);
        if(widgets::combo("value","Value",selected,choices,false,nullptr,false)) { hotkey.number=selected; dirty=true; }
      } else {
        hotkey.selection.resize(choices.size());
        auto selected=std::make_unique<bool[]>(choices.size());
        for(size_t i=0;i<choices.size();++i) selected[i]=hotkey.selection[i];
        if(widgets::multi_combo("value","Value",std::span<bool>(selected.get(),choices.size()),choices,nullptr,false,nullptr,false)) {
          for(size_t i=0;i<choices.size();++i) hotkey.selection[i]=selected[i];
          dirty=true;
        }
      }
    }
    if(widgets::toggle("visible","Visible",hotkey.visible,false,nullptr,false,true)) dirty=true;
    divider();
    if(!closing) { option.last_detail=hotkey; option.has_last_detail=true; }
    const bool shared=conflict(id,hotkey.key);
    if(menu_row("delete","Delete",false,true)) {
      option.entries.erase(option.entries.begin()+static_cast<ptrdiff_t>(option.editing)); dirty=true;
      ImGui::CloseCurrentPopup();
    }
    if(shared) { ImGui::PushFont(nullptr,theme::caption_size); ImGui::PushStyleColor(ImGuiCol_Text,theme::muted); widgets::row_text("Key used by another option",theme::caption_size*s); ImGui::PopStyleColor(); ImGui::PopFont(); }
    if(!closing && !option.detail_pinned) {
      const auto* window=ImGui::GetCurrentWindow();
      const bool inside=ImGui::IsMouseHoveringRect(window->Pos,{window->Pos.x+window->Size.x,window->Pos.y+window->Size.y},false) || ImGui::IsMouseHoveringRect(anchor.min,anchor.max,false);
      if(inside || ImGui::IsWindowAppearing() || widgets::capturing_hotkey() || ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)) option.hover_until=ImGui::GetTime()+theme::hotkey_hover_bridge;
      else if(ImGui::GetTime()>option.hover_until) { ImGui::CloseCurrentPopup(); discard_draft(option); }
    }
  }
  widgets::end_popup();
}
}
void reset() { data.clear(); dirty=false; last_error.clear(); }
void icons(ImFont* font) { icon_font=font; }
Option& option(ImGuiID id) { return data[id]; }
ImGuiID binding(const std::string& path) {
  const auto id=ImHashStr(path.c_str(),0,ImHashStr("option bindings"));
  data[id].path=path;
  return id;
}
const std::map<ImGuiID,Option>& options() { return data; }
bool owns_key(ImGuiKey key) {
  for(const auto& [id,option] : data) for(const auto& hotkey : option.entries) if(hotkey.key==key) return true;
  return false;
}
bool evaluate(ImGuiID id,bool& value,bool disabled) {
  auto& option=data[id];
  if(option.frame==ImGui::GetFrameCount()) return false;
  option.frame=ImGui::GetFrameCount();
  const bool before=value;
  if(option.kind==Kind::Boolean) {
    if(disabled || widgets::capturing_hotkey() || ImGui::GetIO().WantTextInput) return false;
    const Hotkey* idle=nullptr;
    const Hotkey* held=nullptr;
    for(auto& hotkey : option.entries) {
      if(hotkey.key==ImGuiKey_None) { hotkey.active=false; continue; }
      if(hotkey.mode==Mode::Toggle) {
        if(ImGui::IsKeyPressed(hotkey.key,false)) value=!value;
        hotkey.active=false;
      } else {
        if(!idle) idle=&hotkey;
        hotkey.active=ImGui::IsKeyDown(hotkey.key);
        if(hotkey.active) held=&hotkey;
      }
    }
    if(held) value=held->mode==Mode::HoldOff ? !held->value : held->value;
    else if(idle) value=idle->mode==Mode::HoldOff ? idle->value : !idle->value;
    option.last_value=value;
    return value!=before;
  }
  if(!option.was_active) option.baseline=value;
  bool any=false;
  const bool accepting=!disabled && !widgets::capturing_hotkey() && !ImGui::GetIO().WantTextInput;
  for(auto& hotkey : option.entries) {
    if(hotkey.key==ImGuiKey_None || disabled) hotkey.active=false;
    else if(hotkey.mode==Mode::Toggle) {
      if(accepting && ImGui::IsKeyPressed(hotkey.key,false)) {
        if(!hotkey.active) hotkey.toggled_value=!value;
        hotkey.active=!hotkey.active;
      }
    } else {
      if(!ImGui::IsKeyDown(hotkey.key)) hotkey.active=false;
      else if(accepting && ImGui::IsKeyPressed(hotkey.key,false)) hotkey.active=true;
    }
    if(hotkey.active) {
      any=true;
      value=hotkey.mode==Mode::Toggle ? hotkey.toggled_value : hotkey.mode==Mode::HoldOff ? !hotkey.value : hotkey.value;
    }
  }
  if(!any && option.was_active) value=option.baseline;
  option.was_active=any; option.last_value=value;
  return value!=before;
}
bool evaluate(ImGuiID id,double& value,bool disabled) {
  auto& option=data[id];
  if(option.frame==ImGui::GetFrameCount()) return false;
  if(disabled || widgets::capturing_hotkey() || ImGui::GetIO().WantTextInput) { option.frame=ImGui::GetFrameCount(); return false; }
  const double before=value;
  if(!option.was_active) option.baseline_number=value;
  bool dummy=false;
  evaluate(id,dummy,disabled);
  if(option.was_active) {
    for(const auto& hotkey : option.entries) if(hotkey.active)
      value=hotkey.mode==Mode::HoldOff ? option.baseline_number : std::clamp(hotkey.number,option.minimum,option.maximum);
  } else value=option.baseline_number;
  if(option.kind==Kind::Selection && value>=0 && value<option.choice_disabled.size() && option.choice_disabled[static_cast<size_t>(value)]) value=option.baseline_number;
  if(option.was_active && option.step>0) value=std::clamp(option.minimum+std::round((value-option.minimum)/option.step)*option.step,option.minimum,option.maximum);
  return before!=value;
}
bool evaluate(ImGuiID id,std::span<bool> value,bool disabled) {
  auto& option=data[id];
  if(option.frame==ImGui::GetFrameCount()) return false;
  if(disabled || widgets::capturing_hotkey() || ImGui::GetIO().WantTextInput) { option.frame=ImGui::GetFrameCount(); return false; }
  const std::vector<bool> before(value.begin(),value.end());
  if(!option.was_active) option.baseline_selection=before;
  bool dummy=false;
  evaluate(id,dummy,disabled);
  auto result=option.baseline_selection;
  if(option.was_active) for(const auto& hotkey : option.entries) if(hotkey.active && hotkey.mode!=Mode::HoldOff) result=hotkey.selection;
  if(result.size()==value.size()) for(size_t i=0;i<value.size();++i) if(i>=option.choice_disabled.size() || !option.choice_disabled[i]) value[i]=result[i];
  return !std::equal(value.begin(),value.end(),before.begin());
}
void popup(ImGuiID id,Option& option,Bounds row,Bounds button,bool disabled,bool boolean) {
  if(!disabled && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(row.min,row.max) && ImGui::IsMouseClicked(1)) {
    const auto mouse=ImGui::GetMousePos(); option.anchor={mouse,mouse};
    ImGui::OpenPopup("option_hotkeys");
  }
  if(option.anchor.max.x==0) option.anchor=button;
  if(ImGui::IsKeyPressed(ImGuiKey_Escape,false) || !ImGui::IsPopupOpen("option_hotkeys")) discard_draft(option);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize,theme::hotkey_border*ImGui::GetStyle().FontScaleMain);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,theme::hotkey_border*ImGui::GetStyle().FontScaleMain);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,theme::hotkey_rounding*ImGui::GetStyle().FontScaleMain);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{0,theme::hotkey_spacing*ImGui::GetStyle().FontScaleMain});
  const auto background=theme::popup_surface;
  ImGui::PushStyleColor(ImGuiCol_PopupBg,background);
  ImGui::PushStyleColor(ImGuiCol_Border,theme::border);
  ImGui::PushStyleColor(ImGuiCol_NavCursor,{0,0,0,0});
  const auto count=std::count_if(option.entries.begin(),option.entries.end(),[](const Hotkey& entry) { return entry.key!=ImGuiKey_None; });
  const float height=2*theme::hotkey_padding_y+(count==0 ? theme::hotkey_row : (count>1 ? 4 : 3)*(theme::hotkey_row+theme::hotkey_spacing)+2*theme::hotkey_divider_margin+theme::hotkey_divider);
  float width=menu_width("New Hotkey");
  if(count>0) width=std::max(width,menu_width(key_name(option.entries.front().key),true));
  if(count>1) width=std::max(width,menu_width("All Hotkeys",true));
  if(widgets::begin_popup("option_hotkeys",option.anchor,width,height,nullptr,true,true,true)) {
    if(!ImGui::IsPopupOpen("hotkey_detail")) discard_draft(option);
    if(!option.entries.empty() && option.entries.front().key!=ImGuiKey_None) {
      const bool open=menu_row("current",key_name(option.entries.front().key),true);
      const bool hovered=ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride);
      const bool detail_open=ImGui::IsPopupOpen("hotkey_detail");
      if(option.detail_was_open && !detail_open) option.hover_armed=false;
      if(!hovered) option.hover_armed=true;
      if(open || option.request_detail || (hovered && option.hover_armed && !detail_open)) {
        discard_draft(option);
        option.detail_pinned=open || option.request_detail;
        option.request_detail=false;
        option.editing=0; option.detail_anchor=item_bounds();
        option.detail_anchor.min.x=ImGui::GetWindowPos().x;
        option.detail_anchor.max.x=ImGui::GetWindowPos().x+ImGui::GetWindowSize().x;
        ImGui::OpenPopup("hotkey_detail");
      }
      if(count>1) {
        if(menu_row("all","All Hotkeys",true)) { discard_draft(option); ImGui::OpenPopup("all_hotkeys"); }
        auto anchor=item_bounds(); anchor.min.x=ImGui::GetWindowPos().x; anchor.max.x=anchor.min.x+ImGui::GetWindowSize().x;
        float list_width=0;
        for(const auto& hotkey : option.entries) if(hotkey.key!=ImGuiKey_None) list_width=std::max(list_width,menu_width((std::string(key_name(hotkey.key))+" / "+modes[static_cast<int>(hotkey.mode)]).c_str(),true));
        if(widgets::begin_popup("all_hotkeys",anchor,list_width,2*theme::hotkey_padding_y+std::min(6.f,static_cast<float>(count))*(theme::hotkey_row+theme::hotkey_spacing),nullptr,true,true,true,false)) {
          for(size_t index=0;index<option.entries.size();++index) {
            if(option.entries[index].key==ImGuiKey_None) continue;
            ImGui::PushID(static_cast<int>(index));
            const auto& hotkey=option.entries[index];
            const std::string label=std::string(key_name(hotkey.key))+" / "+modes[static_cast<int>(hotkey.mode)];
            if(menu_row("entry",label.c_str(),true)) { discard_draft(option); option.editing=index; option.detail_pinned=true; ImGui::OpenPopup("hotkey_detail"); }
            auto entry_anchor=item_bounds(); entry_anchor.min.x=ImGui::GetWindowPos().x; entry_anchor.max.x=entry_anchor.min.x+ImGui::GetWindowSize().x;
            submenu(id,option,entry_anchor,boolean);
            ImGui::PopID();
          }
          widgets::end_popup();
        }
      }
    }
    const bool unbound=std::any_of(option.entries.begin(),option.entries.end(),[](const Hotkey& hotkey) { return hotkey.key==ImGuiKey_None; });
    ImGui::BeginDisabled(unbound || option.entries.size()>=32);
    if(menu_row("new","New Hotkey")) {
      option.entries.emplace_back(); option.editing=option.entries.size()-1; dirty=true;
      option.entries.back().number=option.baseline_number; option.entries.back().selection=option.baseline_selection;
      option.detail_anchor=item_bounds(); option.detail_anchor.min.x=ImGui::GetWindowPos().x;
      option.detail_anchor.max.x=option.detail_anchor.min.x+ImGui::GetWindowSize().x;
      option.listen=false; option.draft=true; option.detail_pinned=true; ImGui::OpenPopup("hotkey_detail");
    }
    ImGui::EndDisabled();
    if(count>0) {
      divider();
      if(menu_row("reset","Reset",false,true)) { option.entries.clear(); dirty=true; ImGui::CloseCurrentPopup(); }
    }
    option.detail_was_open=ImGui::IsPopupOpen("hotkey_detail");
    submenu(id,option,option.detail_anchor,boolean);
    widgets::end_popup();
  }
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(4);
}

namespace {
bool known_binding(ImGuiID id) {
  const auto hash=[](const std::string& path){return ImHashStr(path.c_str(),0,ImHashStr("option bindings"));};
  for(const auto& node:feature_tree::nodes){
    const std::string_view path=node.path;
    if(node.kind==feature_tree::Kind::Panel || !feature_switches::wired(path))continue;
    if(path.starts_with("Misc/Misc/Movement/") && !feature_switches::wired(path))continue;
    if(hash(std::string("Preview/")+node.path)==id)return true;
  }
  constexpr const char* paths[]{"Camera/thirdperson","Camera/custom_fov","Camera/fov","Viewmodel/enabled","Viewmodel/x","Viewmodel/y","Viewmodel/z","Viewmodel/fov","Movement/bhop","Movement/assist","Movement/strength","Quality of life/accept","Quality of life/killfeed","HUD/scope_lines","HUD/inaccuracy_overlay","HUD/crosshair_mode","HUD/scope_gap","HUD/scope_length","HUD/scope_thickness","Environment/enabled","Color & light/night","Color & light/brightness","Color & light/vibrance","Color & light/contrast","Color & light/gamma","Color & light/warmth","Color & light/sharpness","Color & light/vignette","Color & light/night_intensity","Sky & ambient light/tint","Sky & ambient light/skybox","Sky & ambient light/ambient","Map fog/enabled","Map fog/density","Map fog/distance","Weather/weather","Weather/intensity","Weather/wind","World tint/enabled","Lighting/enabled","Post-processing/bloom_enabled","Depth of field/enabled","Weather/wetness_enabled","Smoke/enabled","Removals/fog","Removals/shadows","Removals/sky","Removals/3d_sky","Removals/flash","Removals/smoke","Removals/ragdolls","Post-processing/exposure","Sky & ambient light/brightness","Lighting/intensity","Post-processing/bloom","Weather/wetness","Map fog/start","Map fog/falloff","Depth of field/near_blurry","Depth of field/near_crisp","Depth of field/far_crisp","Depth of field/far_blurry"};
  for(const auto* path:paths)if(hash(path)==id)return true;
  for(const auto* path:{"Legit aim/hitboxes"})if(hash(path)==id)return true;
  for(const auto& t:combat::legit_toggles)if(hash(std::string("Legit aim/")+t.key)==id)return true;
  for(const auto& n:combat::legit_numbers)if(hash(std::string("Legit aim/")+n.key)==id)return true;
  for(const auto& n:movement::numbers)if(hash(std::string("Movement/")+n.key)==id)return true;
  for(int team=0;team<2;++team)for(int slot=-2;slot<=64;++slot) {
    const auto prefix="Inventory/"+std::to_string(team)+"/"+std::to_string(slot)+"/";
    if(slot!=0 && (hash(prefix+"Wear")==id || (slot!=-2 && hash(prefix+"StatTrak")==id)))return true;
  }
  return false;
}
std::map<ImGuiID,Option> decode(const std::string& snapshot) {
    const auto json=snapshot.empty() ? nlohmann::json::object() : nlohmann::json::parse(snapshot);
    if(!json.is_object() || (json.contains("hotkey_version") && (!json["hotkey_version"].is_number_integer() || json["hotkey_version"].get<int>()>2))) throw std::runtime_error("Unsupported profile");
    std::map<ImGuiID,Option> loaded;
    if(json.contains("hotkeys") && json["hotkeys"].is_object()) for(const auto& [id,entries] : json["hotkeys"].items()) {
      if(!entries.is_array()) continue;
      unsigned long number{};
      try { size_t used{}; number=std::stoul(id,&used,16); if(used!=id.size() || number>UINT_MAX) continue; } catch(...) { continue; }
      if(!known_binding(static_cast<ImGuiID>(number)))continue;
      auto& option=loaded[static_cast<ImGuiID>(number)];
      for(const auto& entry : entries) {
        if(!entry.is_object() || option.entries.size()>=32) continue;
        Hotkey hotkey;
        const auto key=entry.contains("key") && entry["key"].is_string() ? entry["key"].get<std::string>() : "Unbound";
        for(int candidate=ImGuiKey_NamedKey_BEGIN;candidate<=ImGuiKey_MouseX2;++candidate)
          if(key==ImGui::GetKeyName(static_cast<ImGuiKey>(candidate))) hotkey.key=static_cast<ImGuiKey>(candidate);
        const auto mode=entry.contains("mode") && entry["mode"].is_string() ? entry["mode"].get<std::string>() : "Toggle";
        if(mode=="Hold") hotkey.mode=Mode::Hold; else if(mode=="Hold Off") hotkey.mode=Mode::HoldOff;
        if(entry.contains("value") && entry["value"].is_boolean()) hotkey.value=entry["value"];
        if(entry.contains("visible") && entry["visible"].is_boolean()) hotkey.visible=entry["visible"];
        if(entry.contains("number") && entry["number"].is_number()) hotkey.number=entry["number"].get<double>();
        if(entry.contains("selection") && entry["selection"].is_array()) for(const auto& value : entry["selection"]) if(value.is_boolean()) hotkey.selection.push_back(value.get<bool>());
        if(hotkey.key!=ImGuiKey_None && hotkey.key!=ImGuiKey_End && std::isfinite(hotkey.number)) option.entries.push_back(hotkey);
      }
    }
    return loaded;
}
std::string encode(const std::map<ImGuiID,Option>& values) {
  nlohmann::json json={{"hotkey_version",2},{"hotkeys",nlohmann::json::object()}};
  for(const auto& [id,option] : values) {
    if(!known_binding(id))continue;
    nlohmann::json entries=nlohmann::json::array();
    for(const auto& hotkey : option.entries) if(hotkey.key!=ImGuiKey_None) entries.push_back({{"key",key_name(hotkey.key)},{"mode",modes[static_cast<int>(hotkey.mode)]},{"value",hotkey.value},{"visible",hotkey.visible},{"number",hotkey.number},{"selection",hotkey.selection}});
    if(entries.empty()) continue;
    char key[16]{}; std::snprintf(key,sizeof(key),"%08x",id);
    json["hotkeys"][key]=std::move(entries);
  }
  return json["hotkeys"].empty() ? std::string{} : json.dump();
}
}
bool restore(const std::string& snapshot) {
  try { data=decode(snapshot);dirty=true;last_error.clear();return true; }
  catch(...) {last_error="Could not load hotkey profile.";return false;}
}
std::string normalize(const std::string& snapshot) {
  try {return encode(decode(snapshot));}catch(...) {return {};}
}
std::string snapshot() {return encode(data);}
bool take_dirty(){return std::exchange(dirty,false);}
const char* error() { return last_error.c_str(); }
}
