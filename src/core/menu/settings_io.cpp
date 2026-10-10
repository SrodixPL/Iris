#include "core/menu/settings_io.hpp"
#include "core/menu/theme.hpp"
#include "ext/json/json.hpp"
#include <algorithm>
#include <cmath>
#include <Windows.h>
#include <imgui_internal.h>
#include "core/menu/widgets.hpp"
namespace iris::ui {
using Json=nlohmann::json;
int virtual_key(ImGuiKey key) {
  if (key >= ImGuiKey_A && key <= ImGuiKey_Z)
    return 'A' + key - ImGuiKey_A;
  if (key >= ImGuiKey_0 && key <= ImGuiKey_9)
    return '0' + key - ImGuiKey_0;
  if (key >= ImGuiKey_F1 && key <= ImGuiKey_F24)
    return VK_F1 + key - ImGuiKey_F1;
  switch (key) {
  case ImGuiKey_Escape:
    return VK_ESCAPE;
  case ImGuiKey_Delete:
    return VK_DELETE;
  case ImGuiKey_Insert:
    return VK_INSERT;
  case ImGuiKey_Home:
    return VK_HOME;
  case ImGuiKey_PageUp:
    return VK_PRIOR;
  case ImGuiKey_PageDown:
    return VK_NEXT;
  case ImGuiKey_Space:
    return VK_SPACE;
  case ImGuiKey_Tab:
    return VK_TAB;
  case ImGuiKey_Enter:
    return VK_RETURN;
  case ImGuiKey_Backspace:
    return VK_BACK;
  case ImGuiKey_LeftArrow:
    return VK_LEFT;
  case ImGuiKey_RightArrow:
    return VK_RIGHT;
  case ImGuiKey_UpArrow:
    return VK_UP;
  case ImGuiKey_DownArrow:
    return VK_DOWN;
  default:
    return 0;
  }
}
ImGuiKey imgui_key(int vk) {
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_MouseLeft; ++k)
    if (virtual_key(static_cast<ImGuiKey>(k)) == vk)
      return static_cast<ImGuiKey>(k);
  return ImGuiKey_None;
}

std::string encode_settings(const MenuSettings& s){Json colors=Json::array();for(const auto& color:s.saved_colors)colors.push_back(color ? Json::array({color->x,color->y,color->z,color->w}) : Json(nullptr));return Json{{"version",1},{"scale",s.scale},{"appearance",s.color_mode},{"safe_mode",s.safe_mode},{"hotkey",s.hotkey},{"close_key",s.close_key},{"animations",s.animations},{"autosave",s.autosave},{"accent",{s.accent.x,s.accent.y,s.accent.z,s.accent.w}},{"autoload",s.autoload},{"saved_colors",colors}}.dump(2);}
MenuSettings decode_settings(const std::string& text,MenuSettings s){
    auto j=Json::parse(text);if(!j.is_object())throw std::runtime_error("Settings must be a JSON object");
    s.scale=std::clamp(j.value("scale",s.scale),0,6);s.color_mode=std::clamp(j.value("appearance",s.color_mode),0,1);s.safe_mode=std::clamp(j.value("safe_mode",s.safe_mode),0,2);s.hotkey=j.value("hotkey",s.hotkey);s.close_key=j.value("close_key",s.close_key);s.animations=j.value("animations",s.animations);s.autosave=j.value("autosave",s.autosave);s.autoload=j.value("autoload",s.autoload);
    if(s.hotkey<=0||s.hotkey>=256||s.hotkey==0x23||s.hotkey==s.close_key)s.hotkey=0x2e;
    if(s.close_key<=0||s.close_key>=256||s.close_key==0x23||s.close_key==s.hotkey)s.close_key=0x1b;
    if(j.contains("accent")&&j["accent"].is_array()&&j["accent"].size()==4)for(int i=0;i<4;++i){float value=j["accent"][i].get<float>();(&s.accent.x)[i]=std::isfinite(value)?std::clamp(value,0.f,1.f):(&s.accent.x)[i];}
    if(j.contains("saved_colors") && j["saved_colors"].is_array()) {
    s.saved_colors.resize(std::min<size_t>(256,j["saved_colors"].size()));
    for(size_t slot=0;slot<s.saved_colors.size();++slot) {
      const auto& entry=j["saved_colors"][slot];if(!entry.is_array() || entry.size()!=4)continue;
      ImVec4 color{};bool valid=true;
      for(int n=0;n<4;++n) { if(!entry[n].is_number()){valid=false;break;}const float value=entry[n].get<float>();if(!std::isfinite(value)){valid=false;break;}(&color.x)[n]=std::clamp(value,0.f,1.f); }
      if(valid)s.saved_colors[slot]=color;
    }
    }
    s.accent.w=1;return s;
}
SettingsIO::SettingsIO(std::filesystem::path folder):path_(std::move(folder)/"menu-settings.json"){}
void SettingsIO::complete(SettingsResult result){std::lock_guard lock(mutex_);completed_.push_back(std::move(result));}
void SettingsIO::load(MenuSettings defaults){worker_.push([this,defaults]{SettingsResult result;result.loaded=true;result.settings=defaults;try{if(std::filesystem::exists(path_))result.settings=decode_settings(read_file(path_),defaults);}catch(const std::exception& e){result.error=e.what();}complete(std::move(result));});}
void SettingsIO::save(MenuSettings settings){if(!worker_.push([this,settings]{SettingsResult result;try{atomic_write(path_,encode_settings(settings));}catch(const std::exception& e){result.error=e.what();}complete(std::move(result));},1))complete({false,{},"Settings queue is full; try again"});}
std::optional<SettingsResult> SettingsIO::poll(){std::unique_lock lock(mutex_,std::try_to_lock);if(!lock||completed_.empty())return {};auto result=std::move(completed_.front());completed_.pop_front();return result;}
void save_settings(MenuRuntime& state){if(state.settings_io)state.settings_io->save(state.settings);}
void tick_settings(MenuRuntime& state,float auto_scale){
    if(state.settings_io)while(auto result=state.settings_io->poll()){
        if(result->loaded){state.settings=result->settings;state.color_palette.slots=state.settings.saved_colors;state.settings_ready=true;state.theme_pending=true;}
        if(!result->error.empty()){state.error=result->error;state.settings.error=result->error;}
    }
    constexpr float scales[]{1,.75f,1,1.25f,1.5f,1.75f,2};
    const float wanted=state.settings.scale==0?std::clamp(auto_scale,.75f,3.f):scales[std::clamp(state.settings.scale,1,6)];
    const auto display=ImGui::GetIO().DisplaySize;
    const float fitted=display.x>0 && display.y>0 ? std::max(.5f,std::min({wanted,display.x/(theme::window_width+16),display.y/(theme::window_height+16)})) : wanted;
    if(state.theme_pending||std::abs(fitted-state.scale)>.001f){state.scale=fitted;theme::accent=state.settings.accent;theme::mode(state.settings.color_mode);theme::apply(state.scale);state.theme_pending=false;}
    auto& io=ImGui::GetIO();
    io.ConfigFlags&=~(ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad);
    auto& context=*ImGui::GetCurrentContext();context.ConfigNavEnableTabbing=false;context.ConfigNavWindowingKeyNext=context.ConfigNavWindowingKeyPrev=0;
    widgets::configure(state.settings.animations);
    state.menu_toggle_key=state.settings.hotkey;state.menu_close_key=state.settings.close_key;
}
}
