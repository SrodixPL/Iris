#pragma once
#include <imgui.h>
namespace iris::ui::theme {
inline constexpr float dark_card_alpha=.055f, dark_control_alpha=.085f, dark_hover_alpha=.13f, dark_border_alpha=.07f, dark_popup_alpha=.96f;
inline constexpr float dark_track_alpha=.20f, dark_rail_alpha=.16f, dark_separator_alpha=.05f;
inline constexpr float light_card_alpha=.42f, light_control_alpha=.52f, light_hover_alpha=.62f, light_border_alpha=.10f, light_popup_alpha=.96f;
inline constexpr float dark_inventory_tile_alpha=.08f, light_inventory_tile_alpha=.22f;
inline ImVec4 background{13/255.f,14/255.f,18/255.f,1};
inline ImVec4 sidebar{16/255.f,17/255.f,22/255.f,1};
inline ImVec4 canvas{13/255.f,14/255.f,18/255.f,1};
inline ImVec4 surface{1,1,1,dark_control_alpha};
inline ImVec4 border{1,1,1,dark_border_alpha};
inline ImVec4 scrollbar_hover{.28f,.29f,.33f,1};
inline ImVec4 scrollbar_active{.38f,.39f,.44f,1};
inline ImVec4 text{.912f,.918f,.937f,1};
inline ImVec4 secondary{.655f,.667f,.714f,1};
inline ImVec4 muted{.451f,.463f,.514f,1};
inline constexpr ImVec4 default_accent{.86f,.52f,.67f,1};
inline ImVec4 accent=default_accent;
inline bool light_mode{};
inline constexpr ImVec4 on_accent{.12f,.10f,.13f,1};
inline constexpr ImVec4 accent_presets[]{default_accent,{.69f,.58f,.83f,1},{.49f,.61f,.77f,1},{.58f,.72f,.66f,1}};
inline ImVec4 destructive{.85f,.36f,.42f,1};
inline ImVec4 active{.24f,.19f,.28f,1}, hover{.17f,.15f,.21f,1};
inline constexpr float active_accent_blend=.18f, hover_accent_blend=.07f;
inline constexpr float window_width=900, window_height=600, sidebar_width=190;
inline constexpr float window_radius=11, control_radius=6, body_size=14;
inline ImVec4 window_gradient_start{.107f,.110f,.132f,1}, window_gradient_end{.052f,.055f,.068f,1};
inline ImVec4 sidebar_overlay{0,0,0,.32f};
inline constexpr float content_opacity=.90f, sidebar_opacity=.95f, minimum_surface_opacity=.90f;
inline constexpr ImVec4 dark_popup_tint{.079f,.082f,.098f,dark_popup_alpha};
inline constexpr ImVec4 light_popup_tint{.97f,.95f,.97f,light_popup_alpha};
inline ImVec4 control_hover{1,1,1,dark_hover_alpha}, track{1,1,1,dark_track_alpha}, rail{1,1,1,dark_rail_alpha}, inventory_tile_surface{0,0,0,dark_inventory_tile_alpha};
inline ImVec4 separator{1,1,1,dark_separator_alpha};
inline constexpr float blur_radius=12, blur_margin=320;
inline constexpr unsigned blur_downsample=4, gradient_texture_size=1024;
inline constexpr float gradient_dither=.65f/255, gradient_offset=.12f;
inline constexpr float text_center_offset=0, row_height=36, input_height=26, input_padding=10;
inline constexpr float chevron_size=8, chevron_stroke=1;
inline constexpr float slider_radius=4, slider_track=4, slider_track_inset=2, slider_stacked_height=52;
inline constexpr float slider_thumb=6;
inline constexpr float brand_size=23, heading_size=17, caption_size=12;
inline constexpr float sidebar_padding=14, content_padding=20, nav_height=38, nav_gap=3;
inline constexpr float navigation_top=84, bottom_reserve=112, column_gap=18, group_gap=12;
inline constexpr float nav_seconds=.14f, search_width=200, header_height=40;
inline constexpr float brand_tile=46, brand_tracking=4;
inline constexpr float inventory_gap=10;
inline constexpr float inventory_column_gap=12, inventory_card_height=76, inventory_card_radius=8;
inline constexpr float inventory_skin_min=90, inventory_skin_max=120, inventory_skin_gap=8, inventory_skin_height=104, inventory_skin_art=72;
inline constexpr float inventory_overview_art_fraction=.70f, inventory_tile_art_fraction=.80f, inventory_tooltip_delay=.5f;
inline constexpr float inventory_tile_inset=6, inventory_tile_radius=5, inventory_placeholder_size=20, inventory_placeholder_stroke=1.5f;
inline constexpr float inventory_selected_tint=.12f;
inline constexpr float inventory_picker_header=44, inventory_picker_pad=12, inventory_filter_height=30;
inline constexpr float inventory_type_width=170, inventory_rarity_width=120, inventory_panel_width=220, inventory_panel_pad=12;
inline constexpr float inventory_preview_height=128, inventory_action_row=40, inventory_label_height=20;
inline constexpr float inventory_spacing=8, inventory_tight_gap=6, inventory_inset=4, inventory_panel_spacing=3;
inline constexpr float inventory_art_padding=6;
inline constexpr float inventory_icon_button=28, inventory_wear_segment=24;
inline constexpr float inventory_indicator_radius=3, inventory_indicator_inset=10, inventory_rarity_thickness=2;
inline constexpr float inventory_preview_title_y=84, inventory_preview_skin_y=104;
inline constexpr float inventory_rarity_inset=3, inventory_line=1, inventory_rarity_blend=.65f;
inline constexpr ImWchar inventory_back_icon=0xE72B, inventory_next_icon=0xE72A, inventory_more_icon=0xE712, inventory_random_icon=0xE8B1, inventory_refresh_icon=0xE72C;
inline constexpr float page_transition_seconds=.16f, page_transition_slide=8, inventory_route_slide=24, inventory_slot_slide=12, inventory_panel_seconds=.18f;
inline constexpr float inventory_heading_size=14, inventory_heading_contrast=.16f;
inline constexpr float inventory_segment_width=42, inventory_segment_height=24, inventory_side_icon=12;
inline constexpr float world_popup_width=296, world_popup_height=400, world_popup_gap=column_gap, world_chevron=12, config_row_height=60;
inline constexpr float world_popup_compact=156,world_popup_medium=270,world_popup_tall=350;
inline constexpr float surface_border_width=1, selected_surface_tint=.16f, config_marker_width=3, config_marker_height=28;
inline constexpr float config_row_padding=12, config_action_width=72, secondary_height=28;
inline constexpr float world_reset_width=176, config_name_width=320, config_name_height=160;
inline constexpr float inventory_segment_inset=2, inventory_segment_gap=6;
inline constexpr ImVec4 inventory_team_t{.91f,.61f,.28f,1}, inventory_team_ct{.36f,.66f,.92f,1};
inline ImVec4 mix(ImVec4 a,ImVec4 b,float t) { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t}; }
inline ImVec4 surface_tint(ImVec4 base,ImVec4 color,float amount) {
  auto tint=mix(base,color,amount);
  // An opaque accent must not inflate a glass surface's intended alpha.
  tint.w=base.w>amount ? base.w : amount;
  return tint;
}
inline constexpr float light_label_blend=.64f, light_team_label_blend=.5f;
inline constexpr float dark_input_hint_blend=.35f;
inline ImVec4 input_hint() { return light_mode ? secondary : mix(secondary,text,dark_input_hint_blend); }
inline ImVec4 accent_label() { return light_mode ? mix(accent,text,light_label_blend) : accent; }
inline ImVec4 team_label(ImVec4 team) { return light_mode ? mix(team,text,light_team_label_blend) : team; }
inline ImVec4 raised=surface;
inline constexpr float config_action_height=30;
inline constexpr float profile_popup_width=280, profile_popup_height=300, profile_icon=14, profile_icon_column=24, profile_avatar=32;
inline constexpr float appearance_segment_width=42, appearance_height=input_height, appearance_icon=14;
inline constexpr float about_width=360, about_height=310, about_heading_height=36, about_line_height=24;
inline constexpr ImVec4 modal_dim{0,0,0,.55f};
inline constexpr float color_popup_width=224, color_popup_height=380, color_picker_width=204, color_preview=24;
inline constexpr float color_slot=24, color_slot_gap=5, color_hex_width=90;
inline constexpr float setting_row=row_height, setting_control=160, setting_gap=7;
// Value capsules hug their number (48) so the slider track stays wide; long units may need more.
inline constexpr float switch_width=36, switch_height=20, value_width=40, value_width_max=58, value_height=24, value_padding=4;
// Keybind controls are compact pills rather than full-width fields.
inline constexpr float keybind_min_width=56, keybind_max_width=96, keybind_height=24;
// Left-aligned button labels line up with the row labels above them.
inline constexpr float button_label_padding=4;
// Section captions sit in a fixed-height block so card spacing stays even.
inline constexpr float caption_height=20, caption_gap=2;
inline constexpr float highlight_inset=9, select_min_width=140;
inline constexpr float more_width=28, option_height=30, popup_padding=4, popup_padding_x=10, popup_item_padding=12, popup_check_space=24, popup_title_height=34;
inline constexpr float popup_width=296, popup_gap=6, popup_rows=6;
inline constexpr float switch_seconds=.14f, popup_open_seconds=.16f, popup_close_seconds=.16f;
inline constexpr float hotkey_main_width=170, hotkey_detail_width=210, hotkey_row=option_height, hotkey_spacing=0;
inline constexpr float hotkey_padding_y=popup_padding, hotkey_control=input_height, hotkey_label=58;
inline constexpr float hotkey_rounding=10, hotkey_border=1, hotkey_fade=.12f;
inline constexpr float hotkey_icon=14, hotkey_chevron=10, hotkey_divider=1, hotkey_divider_margin=4;
inline constexpr float hotkey_dot=5, hotkey_dot_gap=6, hotkey_hover_bridge=.15f;
inline ImVec4 popup_surface=dark_popup_tint;
inline ImVec4 settings_popup_surface=popup_surface;
inline constexpr float section_radius=10, section_padding_y=4, item_spacing_y=0, action_overhang=6;
inline ImVec4 section_surface{1,1,1,dark_card_alpha};
inline constexpr float settings_height=2*popup_padding+body_size+2*setting_row+2*item_spacing_y+2*section_padding_y;
void mode(int selected);
void apply(float scale);
inline void apply(ImVec4 color,float scale) { accent=color; apply(scale); }
inline constexpr ImVec4 fixture_colors[]{
    {.32f, .36f, .40f, 1}, {.64f, .44f, .20f, 1}, {.28f, .46f, .62f, 1},
    {.50f, .30f, .60f, 1}, {.28f, .46f, .36f, 1}, {.60f, .32f, .38f, 1}};
}
