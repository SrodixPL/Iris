#pragma once
#include "core/menu/theme.hpp"
#include "core/menu/text.hpp"
#include "core/menu/backdrop.hpp"
#include <array>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include "core/menu/transition.hpp"

namespace iris::ui {
struct InventoryArt {
  ImTextureRef texture{};
  ImVec2 uv0{}, uv1{1,1};
  float aspect{1};
  bool interactive{};
};
struct InventoryEntry {
  int slot{}, definition{}, paint{};
  std::string name;
  bool equipped{}, enabled{true};
  InventoryArt art;
  std::string skin_name;
  ImU32 rarity_color{};
};
struct InventorySide {
  std::array<InventoryEntry,5> special;
  std::array<std::vector<InventoryEntry>,3> weapons;
  InventoryArt icon;
};
struct ItemDraft {
  int definition{}, paint{}, seed{}, counter{};
  float wear{.01f};
  bool stattrak{}, applied{};
  std::string custom_model;
  std::array<char,161> name{};
  bool operator==(const ItemDraft&) const = default;
};
struct InventoryAdapter {
  // The host owns loaded art and keeps it alive through submission of the frame.
  void* owner{};
  bool (*change_side)(void*,int){};
  bool (*open_slot)(void*,int,int){};
  InventoryArt (*resolve_art)(void*,int,int){};
  bool (*apply)(void*,int,int,const ItemDraft&,bool){};
  bool (*slot_action)(void*,int,int,int){};
  bool (*read_selection)(void*,int,int,ItemDraft&){};
  InventoryArt (*preview)(void*,int,int){};
  bool (*refresh_loadout)(void*,int,InventorySide&){};
  void (*request_refresh)(void*){};
  bool game_roster{};
  std::array<InventorySide,2> sides;
};
struct InventoryCanvas {
  InventoryAdapter adapter;
  int side{};
  float team_tint{};
  std::array<int,2> selected{-999,-999};
  std::array<std::array<ImVec2,2>,2> side_bounds{};
  std::array<ImVec2,2> divider_bounds{};
  std::array<std::array<ImVec2,2>,20> tile_bounds{};
  std::string error;
  std::string empty_detail;
  std::string query;
  bool reset_scroll{};
  bool browsing{}, both_teams{}, request_options{};
  int rarity_filter{};
  std::array<char,128> skin_search{};
  std::array<char,128> previous_search{};
  ItemDraft previous_item, detail_draft, last_selection;
  int draft_team{-1}, draft_slot{-999};
  float wear_value{};
  bool wear_editing{};
  int previous_slot{-999}, previous_rarity{};
  float grid_scroll{}, previous_scroll{};
  std::array<float,2> overview_scroll{};
  bool restore_overview{}, observed_browsing{}, observed_panel{};
  int observed_side{-1}, observed_slot{-999}, travel{1};
  float panel_fraction{}, team_from{};
  transitions::Motion team_motion, pill_motion, route_motion, slot_motion, panel_motion;
  float panel_from{}, panel_target{};
  std::array<ImVec2,2> both_bounds{}, panel_bounds{}, search_bounds{}, options_bounds{};
  std::array<std::array<ImVec2,2>,2> navigation_bounds{};
  std::array<std::array<ImVec2,2>,4> action_bounds{};
  std::array<ImVec2,2> picker_back{};
  std::array<ImVec2,2> preview_bounds{};
  std::array<float,3> preview_pose{};
  int preview_definition{};
  bool preview_dragged{};
  double preview_click_time{-1};
  ImVec2 preview_click_position{};
  std::string picker_context;

  InventoryCanvas() = default;

};
namespace inventory {
inline bool matches(const std::string& label,const char* query) {
  return std::search(label.begin(),label.end(),query,query+std::char_traits<char>::length(query),
    [](unsigned char a,unsigned char b) { return std::tolower(a)==std::tolower(b); })!=label.end();
}
inline bool image(const InventoryArt& art,ImVec2 p,ImVec2 size,bool inset=true) {
  if(!art.texture.GetTexID() || !std::isfinite(art.aspect) || art.aspect<=0) return false;
  const float padding=inset ? std::min(theme::inventory_art_padding*ImGui::GetStyle().FontScaleMain,std::max(0.f,size.y*.2f)) : 0;
  p.y+=padding;size.y=std::max(1.f,size.y-2*padding);
  const float width=std::min(size.x,size.y*art.aspect), height=width/art.aspect;
  const ImVec2 start{p.x+(size.x-width)*.5f,p.y+(size.y-height)*.5f};
  ImGui::GetWindowDrawList()->AddImage(art.texture,start,{start.x+width,start.y+height},art.uv0,art.uv1,ImGui::GetColorU32(ImVec4{1,1,1,1}));
  return true;
}
inline void image_box(const InventoryArt& art,ImVec2 p,ImVec2 size,float fraction,float s) {
  const ImVec2 box{size.x*fraction,size.y*fraction};
  if(image(art,{p.x+(size.x-box.x)*.5f,p.y+(size.y-box.y)*.5f},box,false)) return;
  auto* draw=ImGui::GetWindowDrawList();
  const float half=std::min(theme::inventory_placeholder_size*s,std::min(box.x,box.y))*.5f;
  const ImVec2 center{p.x+size.x*.5f,p.y+size.y*.5f};
  const auto color=ImGui::GetColorU32(theme::muted);
  draw->AddRect({center.x-half,center.y-half},{center.x+half,center.y+half},color,theme::inventory_tile_radius*s,0,theme::inventory_placeholder_stroke*s);
  draw->AddLine({center.x-half*.65f,center.y+half*.45f},{center.x-half*.1f,center.y-half*.1f},color,theme::inventory_placeholder_stroke*s);
  draw->AddLine({center.x-half*.1f,center.y-half*.1f},{center.x+half*.65f,center.y+half*.45f},color,theme::inventory_placeholder_stroke*s);
  draw->AddCircleFilled({center.x+half*.4f,center.y-half*.4f},theme::inventory_placeholder_stroke*s,color);
}
inline void image_tile(const InventoryArt& art,ImVec2 p,ImVec2 size,float s,float fraction=theme::inventory_tile_art_fraction) {
  ImGui::GetWindowDrawList()->AddRectFilled(p,{p.x+size.x,p.y+size.y},ImGui::GetColorU32(theme::inventory_tile_surface),theme::inventory_tile_radius*s);
  image_box(art,p,size,fraction,s);
}
inline void caption(const char* label,ImVec2 p,ImVec2 end,ImU32 color) {
  auto* draw=ImGui::GetWindowDrawList();
  draw->PushClipRect(p,end,true);
  draw->AddText(p,color,label);
  draw->PopClipRect();
}
inline void ellipsized_caption(const char* label,ImVec2 p,ImVec2 end,ImU32 color) {
  const float width=end.x-p.x;
  if(ImGui::CalcTextSize(label).x<=width) { caption(label,p,end,color); return; }
  size_t count=std::char_traits<char>::length(label);
  const float suffix=ImGui::CalcTextSize("...").x;
  while(count && ImGui::CalcTextSize(label,label+count).x+suffix>width) {
    do { --count; } while(count && (static_cast<unsigned char>(label[count])&0xc0)==0x80);
  }
  auto* draw=ImGui::GetWindowDrawList(); draw->PushClipRect(p,end,true);
  draw->AddText(p,color,label,label+count);
  draw->AddText({p.x+ImGui::CalcTextSize(label,label+count).x,p.y},color,"...");
  draw->PopClipRect();
}
inline void rarity_edge(ImDrawList* draw,ImVec2 p,ImVec2 end,ImU32 color,float s) {
  const float inset=theme::inventory_card_radius*s;
  const ImVec4 blended=theme::mix(theme::surface,ImGui::ColorConvertU32ToFloat4(color),theme::inventory_rarity_blend);
  const float y=end.y-theme::inventory_rarity_inset*s;
  draw->AddLine({p.x+inset,y},{end.x-inset,y},ImGui::GetColorU32(blended),theme::inventory_rarity_thickness*s);
}
inline void side_selector(InventoryCanvas& canvas,float s,bool animations=true) {
  auto* draw=ImGui::GetWindowDrawList();
  const float gap=theme::inventory_gap*s;

  canvas.team_tint=canvas.team_from+(canvas.side-canvas.team_from)*canvas.pill_motion.step(animations,theme::page_transition_seconds);
  const float segment_width=std::min(theme::inventory_segment_width*s,ImGui::GetContentRegionAvail().x*.5f);
  const auto segment_origin=ImGui::GetCursorScreenPos();
  std::array<bool,2> hovered{};
  for(int side=0;side<2;++side) {
    ImGui::PushID(side);
    if(side) ImGui::SameLine(0,0);
    const auto p=ImGui::GetCursorScreenPos();
    const ImVec2 size{segment_width,theme::inventory_segment_height*s}, end{p.x+size.x,p.y+size.y};
    const bool clicked=ImGui::InvisibleButton("##inventory_side",size,ImGuiButtonFlags_EnableNav);
    canvas.side_bounds[side]={p,end}; hovered[side]=ImGui::IsItemHovered();
    if(clicked && canvas.side!=side) {
      if(!canvas.adapter.change_side || canvas.adapter.change_side(canvas.adapter.owner,side)) {
        canvas.team_from=canvas.team_tint; canvas.side=side; canvas.reset_scroll=true;
        canvas.team_motion.start(animations); canvas.pill_motion.start(animations); canvas.error.clear();
        if(!animations) canvas.team_tint=static_cast<float>(side);
      } else canvas.error="Could not change side.";
    }
    ImGui::PopID();
  }
  const auto active_tint=theme::mix(theme::inventory_team_t,theme::inventory_team_ct,canvas.team_tint);
  draw->AddRectFilled(segment_origin,{segment_origin.x+segment_width*2,segment_origin.y+theme::inventory_segment_height*s},ImGui::GetColorU32(theme::surface),theme::control_radius*s);
  draw->AddRect(segment_origin,{segment_origin.x+segment_width*2,segment_origin.y+theme::inventory_segment_height*s},ImGui::GetColorU32(theme::border),theme::control_radius*s);
  const float pill_from=segment_origin.x+segment_width*canvas.team_tint+theme::inventory_segment_inset*s;
  draw->AddRectFilled({pill_from,segment_origin.y+theme::inventory_segment_inset*s},{pill_from+segment_width-2*theme::inventory_segment_inset*s,segment_origin.y+(theme::inventory_segment_height-theme::inventory_segment_inset)*s},ImGui::GetColorU32(theme::surface_tint(theme::surface,active_tint,.18f)),theme::control_radius*s);
  for(int side=0;side<2;++side) {
    const auto p=canvas.side_bounds[side][0],end=canvas.side_bounds[side][1];
    const ImVec2 size{end.x-p.x,end.y-p.y};
    const float inset=theme::inventory_segment_inset*s;
    if(canvas.side!=side && hovered[side])
      draw->AddRectFilled({p.x+inset,p.y+inset},{end.x-inset,end.y-inset},ImGui::GetColorU32(theme::surface_tint(theme::surface,active_tint,canvas.side==side ? .18f : .06f)),(theme::control_radius-theme::inventory_segment_inset)*s);
    const float segment_gap=theme::inventory_segment_gap*s;
    const float icon_size=theme::inventory_side_icon*s;
    const char* label=side ? "CT" : "T";
    const auto text=ImGui::CalcTextSize(label);
    const bool has_icon=canvas.adapter.sides[side].icon.texture.GetTexID()!=0;
    const float content_width=text.x+(has_icon ? icon_size+segment_gap : 0);
    const float content_x=p.x+(size.x-content_width)*.5f;
    if(has_icon) image(canvas.adapter.sides[side].icon,{content_x,p.y+(size.y-icon_size)*.5f},{icon_size,icon_size});
    caption(label,{content_x+(has_icon ? icon_size+segment_gap : 0),widgets::text_y(p.y,size.y)},end,ImGui::GetColorU32(canvas.side==side ? theme::team_label(active_tint) : theme::secondary));
  }
  ImGui::Dummy({0,gap});
}
inline void render(InventoryCanvas& canvas,float s,const char* query,bool animations=true) {
  if(canvas.query!=query) { canvas.query=query; canvas.reset_scroll=true; }
  side_selector(canvas,s,animations);
  {
    // Quiet team-tinted hairline; the shell divider treatment stays neutral.
    auto divider=theme::mix(theme::inventory_team_t,theme::inventory_team_ct,canvas.team_tint);
    divider.w*=.5f;
    ImGui::GetWindowDrawList()->AddLine(canvas.divider_bounds[0],canvas.divider_bounds[1],ImGui::GetColorU32(divider),theme::inventory_line*s);
  }
  const float phase=canvas.team_motion.step(animations,theme::page_transition_seconds);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*phase);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY()+(1-phase)*theme::page_transition_slide*s);
  const float gap=theme::inventory_column_gap*s;
  std::array<std::array<const InventoryEntry*,5>,4> visible{};
  std::array<int,4> counts{};
  const auto add=[&](int column,const InventoryEntry& item) {
    if(!item.name.empty() && counts[column]<5 && (matches(item.name,query) || matches(item.skin_name,query))) visible[column][counts[column]++]=&item;
  };
  for(const auto& item:canvas.adapter.sides[canvas.side].special) add(0,item);
  for(int c=0;c<3;++c) for(const auto& item:canvas.adapter.sides[canvas.side].weapons[c]) add(c+1,item);
  int columns=0; for(int count:counts) columns+=count>0;
  canvas.tile_bounds={};
  const auto area=ImGui::GetContentRegionAvail();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
  ImGui::BeginChild("inventory_items",{0,std::max(1.f,area.y)},ImGuiChildFlags_NavFlattened);
  if(canvas.restore_overview || canvas.reset_scroll) {
    ImGui::SetScrollY(canvas.restore_overview ? canvas.overview_scroll[canvas.side] : 0);
    canvas.restore_overview=canvas.reset_scroll=false;
  }
  const char* headings[]{"Special Items","Pistols","Mid-Tier","Rifles"};
  if(!columns) {
    const char* text=query[0] ? "No matching slots" : "Loadout unavailable";
    const auto size=ImGui::CalcTextSize(text), p=ImGui::GetCursorScreenPos();
    const float width=std::max(1.f,area.x-2*theme::inventory_picker_pad*s);
    const bool detail=!query[0] && !canvas.empty_detail.empty();
    const float detail_height=detail ? ImGui::CalcTextSize(canvas.empty_detail.c_str(),nullptr,false,width).y+theme::inventory_spacing*s : 0;
    const float top=p.y+(area.y-size.y-detail_height)*.5f;
    ImGui::GetWindowDrawList()->AddText({p.x+(area.x-size.x)*.5f,top},ImGui::GetColorU32(theme::secondary),text);
    if(detail) {
      ImGui::SetCursorScreenPos({p.x+theme::inventory_picker_pad*s,top+size.y+theme::inventory_spacing*s});
      ImGui::PushStyleColor(ImGuiCol_Text,theme::secondary);ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+width);
      ImGui::TextUnformatted(canvas.empty_detail.c_str());ImGui::PopTextWrapPos();ImGui::PopStyleColor();
    }
  } else {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,{gap*.5f,0});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{gap,theme::inventory_spacing*s});
    if(ImGui::BeginTable("inventory_grid",columns,ImGuiTableFlags_SizingStretchSame)) {
      for(int c=0;c<4;++c) if(counts[c]) {
        ImGui::TableNextColumn();
        ImGui::PushFont(nullptr,theme::inventory_heading_size);
        ImGui::TextColored(theme::mix(theme::secondary,theme::text,theme::inventory_heading_contrast),"%s",headings[c]); ImGui::PopFont();
        for(int row=0;row<counts[c];++row) {
          const auto& item=*visible[c][row];
          const auto p=ImGui::GetCursorScreenPos();
          const ImVec2 size{ImGui::GetContentRegionAvail().x,theme::inventory_card_height*s}, end{p.x+size.x,p.y+size.y};
          ImGui::PushID(canvas.side); ImGui::PushID(item.slot);
          ImGui::BeginDisabled(!item.enabled);
          const bool click=ImGui::InvisibleButton("inventory_tile",size,ImGuiButtonFlags_EnableNav);
          canvas.tile_bounds[row*4+c]={p,end};
          auto* draw=ImGui::GetWindowDrawList();
          auto tint=ImGui::IsItemHovered() ? theme::control_hover : theme::section_surface;
          draw->AddRectFilled(p,end,ImGui::GetColorU32(tint),theme::inventory_card_radius*s);
          draw->AddRect(p,end,ImGui::GetColorU32(theme::border),theme::inventory_card_radius*s,ImDrawFlags_RoundCornersAll,theme::surface_border_width*s);
          const bool applied=item.equipped && (!item.skin_name.empty() || item.paint!=0 || item.slot==-3);
          auto art=item.art;
          if(!art.texture.GetTexID() && item.definition>0 && canvas.adapter.resolve_art) art=canvas.adapter.resolve_art(canvas.adapter.owner,item.definition,item.paint);
          image_box(art,p,size,theme::inventory_overview_art_fraction,s);
          if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal|ImGuiHoveredFlags_NoSharedDelay|ImGuiHoveredFlags_AllowWhenDisabled))
            widgets::tooltip(item.name.c_str(),applied ? item.skin_name.c_str() : nullptr);
          if(applied) {
            rarity_edge(draw,p,end,item.rarity_color ? item.rarity_color : ImGui::ColorConvertFloat4ToU32(theme::accent),s);
            draw->AddCircleFilled({end.x-theme::inventory_indicator_inset*s,p.y+theme::inventory_indicator_inset*s},theme::inventory_indicator_radius*s,ImGui::GetColorU32(theme::accent));
          }
          ImGui::EndDisabled();
          if(click && (!canvas.adapter.open_slot || canvas.adapter.open_slot(canvas.adapter.owner,canvas.side,item.slot))) {
            canvas.selected[canvas.side]=item.slot; canvas.browsing=true; canvas.error.clear();
          } else if(click) canvas.error="Could not open item.";
          ImGui::PopID(); ImGui::PopID();
        }
      }
      ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
  }
  canvas.overview_scroll[canvas.side]=ImGui::GetScrollY();
  ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleVar();
}
}
}
