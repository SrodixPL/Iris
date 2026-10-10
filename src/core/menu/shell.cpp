#include "core/menu/menu.hpp"
#include "core/menu/settings_io.hpp"
#include "core/menu/config_io.hpp"
#include "core/menu/image_cache.hpp"
#include "core/menu/theme.hpp"
#include "core/menu/widgets.hpp"
#include "core/menu/hotkeys.hpp"
#include "core/menu/tabs.hpp"
#include "core/menu/backdrop.hpp"
#include "core/menu/brand_logo.hpp"
#include "core/menu/apply_queue.hpp"
#include "core/menu/feature_switches.hpp"
#include "core/startup_status.hpp"
#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <type_traits>
#include <cstdio>
#include <string_view>
#include <imgui_internal.h>
#if defined(IRIS_DIAGNOSTICS) && !defined(INVENTORY_PREVIEW)
#error IRIS_DIAGNOSTICS is forbidden in the production menu
#endif
namespace iris::menu {
using namespace ui;
void initialize(State &state) {
  state = State{};
  state.gameplay_wired=iris::startup::is_game();
  widgets::reset();
  BOOL enabled = TRUE;
  if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0))
    state.settings.animations = enabled != FALSE;
  auto &io = ImGui::GetIO();
  wchar_t directory[MAX_PATH]{};
  if (GetWindowsDirectoryW(directory, MAX_PATH)) {
    const auto fonts = std::filesystem::path(directory) / L"Fonts";
    const auto body = fonts / L"segoeui.ttf", bold = fonts / L"segoeuib.ttf";
    if (std::filesystem::exists(body))
      state.body =
          io.Fonts->AddFontFromFileTTF(body.string().c_str(), theme::body_size);
    if (std::filesystem::exists(bold))
      state.bold = io.Fonts->AddFontFromFileTTF(bold.string().c_str(),
                                                theme::heading_size);
    const auto icons = fonts / L"segmdl2.ttf";
    if (std::filesystem::exists(icons))
      state.icons = io.Fonts->AddFontFromFileTTF(icons.string().c_str(),
                                                 theme::profile_icon);
  }
  if (!state.body)
    state.body = io.Fonts->AddFontDefault();
  if (!state.bold)
    state.bold = state.body;
  const auto brand_path=std::filesystem::path(directory)/L"Fonts"/L"seguibl.ttf";
  if(std::filesystem::exists(brand_path)) state.brand=io.Fonts->AddFontFromFileTTF(brand_path.string().c_str(),theme::brand_size);
  if(!state.brand) state.brand=state.bold;
  if(!state.icons) state.icons=state.body;
  hotkeys::icons(state.icons);
  io.FontDefault = state.body;
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  theme::mode(state.settings.color_mode);
  theme::accent=state.settings.accent;
  theme::apply(state.scale);
  state.settings_io = std::make_shared<SettingsIO>(executable_folder());
  state.settings_io->load(state.settings);
  state.color_palette.owner=&state;
  state.color_palette.save=[](void* owner,const ColorPalette& palette) {
    auto& state=*static_cast<State*>(owner);if(!state.settings_io)return false;
    state.settings.saved_colors=palette.slots;save_settings(state);return true;
  };
  state.inventory.adapter.game_roster=true;
  state.values.cosmetics.enabled = true;
  // The game uses the reviewed queue; standalone previews keep local UI state.
  if(state.gameplay_wired) {
    feature_switches::project(state.values);
    state.apply_queue=std::make_shared<ApplyQueue>(state.values);
    bind_inventory(state);
  }
  state.saved_hash = state_hash(state.values);
  state.config_io = std::make_shared<ConfigIO>(executable_folder());
  state.config_io->request(ConfigAction::List);
}
void render(State& state) {
  if(state.images)state.images->pump();
  widgets::configure(state.settings.animations);
  if(state.focus_menu) { ImGui::SetNextWindowFocus(); state.focus_menu=false; }
  const bool popup=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
  if(popup && !widgets::capturing_hotkey() && ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {
    auto& context=*ImGui::GetCurrentContext();
    ImGui::ClosePopupToLevel(context.OpenPopupStack.Size-1,true);
    ImGui::SetKeyOwner(ImGuiKey_Escape,ImHashStr("popup escape"),ImGuiInputFlags_LockThisFrame);
  }
  backdrop::begin_frame();
  const auto* viewport=ImGui::GetMainViewport();
  const auto display=viewport->WorkSize;
  const float s=state.scale;
  if(display.x<1 || display.y<1) return;
  ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop,true);
  const ImVec2 size{std::min(theme::window_width*s,std::max(1.f,display.x-16*s)),
                    std::min(theme::window_height*s,std::max(1.f,display.y-16*s))};
  const ImVec2 centered{viewport->WorkPos.x+(display.x-size.x)*.5f,viewport->WorkPos.y+(display.y-size.y)*.5f};
  if(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Home,false) && !popup && !state.key_capture)
    state.window_offset={};
  const ImVec2 origin{
    std::clamp(centered.x+state.window_offset.x,viewport->WorkPos.x+8*s,viewport->WorkPos.x+display.x-size.x-8*s),
    std::clamp(centered.y+state.window_offset.y,viewport->WorkPos.y+8*s,viewport->WorkPos.y+display.y-size.y-8*s)};
  state.window_offset={origin.x-centered.x,origin.y-centered.y};
  const float nav=std::min(theme::sidebar_width*s,size.x*.28f), pad=theme::sidebar_padding*s;
  ImGui::SetNextWindowSize(size,ImGuiCond_Always);
  ImGui::SetNextWindowPos(origin,ImGuiCond_Always);
  ImGui::Begin("IRIS",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|
    ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoNav);
  state.window={origin,{origin.x+size.x,origin.y+size.y}};
  auto* draw=ImGui::GetWindowDrawList();
  backdrop::window(draw,state.window,nav,s);
  draw->AddRect(origin,state.window.max,ImGui::GetColorU32(theme::border),theme::window_radius*s,ImDrawFlags_RoundCornersAll,ImGui::GetStyle().WindowBorderSize);
  draw->AddLine({origin.x+nav,origin.y},{origin.x+nav,origin.y+size.y},ImGui::GetColorU32(theme::border),ImGui::GetStyle().WindowBorderSize);
  // Loader aperture mark with a single, vertically centered wordmark.
  {
    const float tile=theme::brand_tile*s;
    const ImVec2 corner{origin.x+pad,origin.y+(theme::navigation_top*s-tile)*.5f};
    draw_brand_logo(draw,corner,tile);
    ImGui::PushFont(state.brand,theme::brand_size);
    float word_x=corner.x+tile+12*s;
    for(const char* letter=app_name;*letter;++letter) {
      draw->AddText({word_x,widgets::text_y(corner.y,tile)},ImGui::GetColorU32(theme::text),letter,letter+1);
      word_x+=ImGui::CalcTextSize(letter,letter+1).x+theme::brand_tracking*s;
    }
    ImGui::PopFont();
  }
  const float width=nav-2*pad;
  ImGui::SetCursorPos({pad,theme::navigation_top*s});
  ImGui::BeginChild("navigation",{width,std::max(1.f,size.y-(theme::navigation_top+theme::bottom_reserve)*s)},ImGuiChildFlags_NavFlattened);
  auto* nav_draw=ImGui::GetWindowDrawList();
  const auto nav_origin=ImGui::GetWindowPos();
  nav_draw->ChannelsSplit(2);
  nav_draw->ChannelsSetCurrent(1);
  for(int i=0;i<static_cast<int>(Page::Count);++i) {
    if(i==0 || i==2 || i==4) {
      if(i) ImGui::Dummy({0,14*s});
      widgets::section(i==0 ? "combat" : i==2 ? "visuals" : "misc",i==0 ? "COMBAT" : i==2 ? "VISUALS" : "MISC",state);
    }
    const auto page=static_cast<Page>(i);
    if(widgets::navigation(page_names[i],page_names[i],state.selected==page,
       ImGui::GetContentRegionAvail().x,state,state.navigation[i])) state.selected=page;
  }
  const auto& selected=state.navigation[static_cast<size_t>(state.selected)];
  const float target=(selected.min.y-nav_origin.y)/s;
  if(state.highlight_y<0 || !state.settings.animations) state.highlight_y=target;
  else {
    state.highlight_y+=(target-state.highlight_y)*(1-std::exp(-3*ImGui::GetIO().DeltaTime/theme::nav_seconds));
    if(std::abs(state.highlight_y-target)<.05f) state.highlight_y=target;
  }
  const float y=nav_origin.y+state.highlight_y*s;
  nav_draw->ChannelsSetCurrent(0);
  nav_draw->AddRectFilled({selected.min.x,y},{selected.max.x,y+theme::nav_height*s},ImGui::GetColorU32(theme::surface),theme::control_radius*s);
  nav_draw->ChannelsMerge();
  ImGui::EndChild();
  ImGui::SetCursorPos({pad,size.y-100*s});
  widgets::config_row(state,width);
  draw->AddLine({origin.x+pad,origin.y+size.y-56*s},{origin.x+nav-pad,origin.y+size.y-56*s},ImGui::GetColorU32(theme::border));
  ImGui::SetCursorPos({pad,size.y-46*s});
  widgets::profile(state,width);
  const float padding=theme::content_padding*s;
  ImGui::SetCursorPos({nav+padding,padding});
  const ImVec2 content_size{std::max(1.f,size.x-nav-2*padding),std::max(1.f,size.y-2*padding)};
  state.content={{origin.x+nav+padding,origin.y+padding},{origin.x+size.x-padding,origin.y+size.y-padding}};
  ImGui::BeginChild("content",content_size);
  tabs::render(state);
  ImGui::EndChild();
  auto& context=*ImGui::GetCurrentContext();
  if(!ImGui::IsMouseDown(ImGuiMouseButton_Left) || popup)state.dragging=false;
  const auto* hovered=context.HoveredWindow;
  const bool background=hovered && hovered->RootWindow==ImGui::GetCurrentWindow() &&
    context.HoveredId==0 && (context.ActiveId==0 || context.ActiveId==hovered->MoveId ||
    context.ActiveId==ImGui::GetCurrentWindow()->MoveId);
  if(!popup && background && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
     (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || context.ActiveId!=0))state.dragging=true;
  if(state.dragging) {
    state.window_offset.x+=ImGui::GetIO().MouseDelta.x;
    state.window_offset.y+=ImGui::GetIO().MouseDelta.y;
  }
  ImGui::End();
  config_overlays(state);
  if(hotkeys::take_dirty()) {
    const auto bindings=hotkeys::snapshot();
    if(bindings!=state.values.hotkeys){state.values.hotkeys=bindings;state.dirty=true;persist_features(state);}
  }
  state.key_capture=widgets::capturing_hotkey();
  ImGui::PopItemFlag();
  state.popup_was_open=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);

}
}
