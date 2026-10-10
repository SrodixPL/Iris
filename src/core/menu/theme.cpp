#include "core/menu/theme.hpp"
#include <array>
namespace iris::ui::theme {
void mode(int selected) {
  light_mode=selected==1;
  static const std::array dark{background,sidebar,canvas,surface,border,scrollbar_hover,scrollbar_active,text,secondary,muted,destructive,active,hover,window_gradient_start,window_gradient_end,sidebar_overlay,raised,section_surface};
  static constexpr std::array light{
    ImVec4{.90f,.88f,.92f,1}, ImVec4{.87f,.84f,.90f,1}, ImVec4{.95f,.93f,.96f,1}, ImVec4{1,1,1,light_control_alpha},
    ImVec4{.20f,.16f,.23f,light_border_alpha}, ImVec4{.63f,.58f,.68f,1}, ImVec4{.49f,.43f,.56f,1}, ImVec4{.16f,.14f,.18f,1},
    ImVec4{.34f,.30f,.38f,1}, ImVec4{.47f,.43f,.51f,1}, ImVec4{.66f,.24f,.36f,1}, ImVec4{.88f,.80f,.85f,1},
    ImVec4{.92f,.87f,.91f,1}, ImVec4{.99f,.97f,1,1}, ImVec4{.88f,.84f,.91f,1}, ImVec4{.62f,.57f,.67f,.36f},
    ImVec4{1,1,1,light_control_alpha}, ImVec4{1,1,1,light_card_alpha}
  };
  ImVec4* colors[]{&background,&sidebar,&canvas,&surface,&border,&scrollbar_hover,&scrollbar_active,&text,&secondary,&muted,&destructive,&active,&hover,&window_gradient_start,&window_gradient_end,&sidebar_overlay,&raised,&section_surface};
  const auto& palette=selected==1 ? light : dark;
  for(size_t i=0;i<palette.size();++i) *colors[i]=palette[i];
  surface={1,1,1,light_mode ? light_control_alpha : dark_control_alpha};
  raised=surface;
  // Cards and grouped surfaces lift slightly above the page instead of darkening it.
  section_surface={1,1,1,light_mode ? light_card_alpha : dark_card_alpha};
  border=light_mode ? ImVec4{0,0,0,light_border_alpha} : ImVec4{1,1,1,dark_border_alpha};
  control_hover={1,1,1,light_mode ? light_hover_alpha : dark_hover_alpha};
  track=light_mode ? ImVec4{0,0,0,dark_track_alpha} : ImVec4{1,1,1,dark_track_alpha};
  rail=light_mode ? ImVec4{0,0,0,dark_rail_alpha} : ImVec4{1,1,1,dark_rail_alpha};
  separator=light_mode ? ImVec4{0,0,0,dark_separator_alpha} : ImVec4{1,1,1,dark_separator_alpha};
  inventory_tile_surface=light_mode ? ImVec4{1,1,1,light_inventory_tile_alpha} : ImVec4{0,0,0,dark_inventory_tile_alpha};
  popup_surface=light_mode ? light_popup_tint : dark_popup_tint; settings_popup_surface=popup_surface;
}
void apply(float scale) {
  active=mix(sidebar,accent,active_accent_blend);
  hover=mix(sidebar,accent,hover_accent_blend);
  auto& style=ImGui::GetStyle();
  style=ImGuiStyle{};
  style.WindowPadding={0,0};
  style.WindowRounding=window_radius;
  style.WindowBorderSize=1;
  style.ChildBorderSize=0;
  style.FrameRounding=control_radius;
  style.FrameBorderSize=0;
  style.FramePadding={input_padding,(input_height-body_size)*.5f};
  style.ItemSpacing={8,item_spacing_y};
  style.ScrollbarSize=6;
  style.ScrollbarRounding=6;
  style.FontSizeBase=body_size;
  style.HoverDelayNormal=inventory_tooltip_delay;
  style.Colors[ImGuiCol_Text]=text;
  style.Colors[ImGuiCol_TextDisabled]=secondary;
  style.Colors[ImGuiCol_WindowBg]=canvas;
  style.Colors[ImGuiCol_ChildBg]={0,0,0,0};
  style.Colors[ImGuiCol_Border]=border;
  style.Colors[ImGuiCol_Button]={0,0,0,0};
  style.Colors[ImGuiCol_ButtonHovered]=hover;
  style.Colors[ImGuiCol_ButtonActive]=active;
  style.Colors[ImGuiCol_FrameBg]=raised;
  style.Colors[ImGuiCol_FrameBgHovered]=control_hover;
  style.Colors[ImGuiCol_FrameBgActive]=raised;
  style.Colors[ImGuiCol_PopupBg]=popup_surface;
  style.Colors[ImGuiCol_Header]=active;
  style.Colors[ImGuiCol_HeaderHovered]=hover;
  style.Colors[ImGuiCol_HeaderActive]=active;
  style.Colors[ImGuiCol_CheckMark]=accent;
  style.Colors[ImGuiCol_SliderGrab]=text;
  style.Colors[ImGuiCol_SliderGrabActive]=accent;
  style.Colors[ImGuiCol_TitleBg]=sidebar;
  style.Colors[ImGuiCol_TitleBgActive]=sidebar;
  style.Colors[ImGuiCol_Separator]=separator;
  style.Colors[ImGuiCol_TextSelectedBg]=active;
  style.PopupRounding=hotkey_rounding;
  style.PopupBorderSize=1;
  style.Colors[ImGuiCol_ScrollbarBg]={0,0,0,0};
  style.Colors[ImGuiCol_NavCursor]=accent;
  style.Colors[ImGuiCol_ModalWindowDimBg]=modal_dim;
  style.Colors[ImGuiCol_ScrollbarGrab]={1,1,1,.075f};
  style.Colors[ImGuiCol_ScrollbarGrabHovered]={1,1,1,.16f};
  style.Colors[ImGuiCol_ScrollbarGrabActive]={1,1,1,.24f};
  style.ScaleAllSizes(scale);
  style.FontScaleMain=scale;
}
}
