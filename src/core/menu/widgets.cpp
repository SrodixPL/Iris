#include "core/menu/widgets.hpp"
#include "core/menu/theme.hpp"
#include "core/menu/hotkeys.hpp"
#include "core/menu/transition.hpp"
#include "core/menu/backdrop.hpp"
#include <string_view>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>
#include <cctype>
#include <unordered_map>
#include <vector>
#include <memory>
#include <charconv>
#include "core/menu/settings_io.hpp"
#include "core/menu/config_io.hpp"
#include <Windows.h>
namespace iris::ui::widgets {
namespace {
bool animate_widgets=true;
struct PopupMotion {
  float progress{}, closing_alpha{1}, closing{};
  float content_height{};
  bool was_open{}, fading{};
  int frame{-1}, depth{}, order{};
  float close_seconds{theme::popup_close_seconds};
  std::string window_name;
  ImVec2 position{}, size{};
  ImTextureRef font_texture;
  ImVec2 font_texture_size{};
  float snapshot_scale{};
  bool snapshot_valid{true};
  std::vector<std::unique_ptr<ImDrawList>> snapshot;
};
std::unordered_map<ImGuiID,PopupMotion> popup_motions;
std::vector<ImGuiID> popup_motion_stack;
std::vector<ImGuiLastItemData> dropdown_items;
std::vector<std::string> row_labels;
// Tracks the previous row per container so hairlines land between rows only.
struct RowFlow { ImGuiID container{}; float bottom{}; bool valid{}; int frame{-1}; float drawn_y{-1}; };
std::unordered_map<ImGuiID,RowFlow> row_flow;
// Hairline above a row that directly follows another row in the same container. Rows drawn
// outside the shared Row helper call this too, so every row keeps the same rhythm and a card
// never ends on a rule.
void row_rule(ImVec2 origin,float width,float height) {
  const ImGuiID container=ImGui::GetCurrentWindow()->ID;
  auto& flow=row_flow[container];
  if(flow.frame!=ImGui::GetFrameCount()) { flow.frame=ImGui::GetFrameCount(); flow.valid=false; flow.drawn_y=-1; }
  if(flow.valid && std::abs(origin.y-flow.bottom)<1.f && flow.drawn_y!=origin.y) {
    ImGui::GetWindowDrawList()->AddLine({origin.x,origin.y},{origin.x+width,origin.y},ImGui::GetColorU32(theme::separator));
    flow.drawn_y=origin.y;
  }
  flow.bottom=origin.y+height; flow.valid=true;
}
ImGuiID capturing_key{};
bool capture_reserved{};
int capture_frame{-1};
int capture_seen{-1};
using transitions::ease;
void snapshot_popup(ImGuiWindow* window,PopupMotion& motion) {
  const auto* atlas=ImGui::GetIO().Fonts;
  for(const auto& command:window->DrawList->CmdBuffer)
    for(const auto* texture:atlas->TexList)
      if(command.ElemCount && command.TexRef._TexData==texture && texture!=atlas->TexData)
        motion.snapshot_valid=false;
  motion.font_texture=atlas->TexRef;
  motion.font_texture_size={static_cast<float>(atlas->TexData->Width),static_cast<float>(atlas->TexData->Height)};
  motion.snapshot_scale=ImGui::GetStyle().FontScaleMain;
  motion.snapshot.emplace_back(window->DrawList->CloneOutput());
  for(auto* child : window->DC.ChildWindows)
    if(child->Active && !child->Hidden && !(child->Flags&ImGuiWindowFlags_Popup)) snapshot_popup(child,motion);
}
void replay_popup(PopupMotion& motion,float alpha) {
  const auto* atlas=ImGui::GetIO().Fonts;
  // Cloned commands and UVs cannot survive font-atlas replacement or resizing.
  if(!motion.snapshot_valid || motion.font_texture!=atlas->TexRef || !atlas->TexData ||
     motion.font_texture_size.x!=atlas->TexData->Width || motion.font_texture_size.y!=atlas->TexData->Height ||
     motion.snapshot_scale!=ImGui::GetStyle().FontScaleMain) {
    motion.snapshot.clear();
    motion.closing=1;
    return;
  }
  auto* out=ImGui::GetForegroundDrawList();
  for(const auto& saved : motion.snapshot) {
    const int vertex_base=out->VtxBuffer.Size,index_base=out->IdxBuffer.Size;
    out->VtxBuffer.resize(vertex_base+saved->VtxBuffer.Size);
    for(int i=0;i<saved->VtxBuffer.Size;++i) {
      auto vertex=saved->VtxBuffer[i];
      vertex.col=(vertex.col&~IM_COL32_A_MASK)|(static_cast<ImU32>(((vertex.col>>IM_COL32_A_SHIFT)&255)*alpha)<<IM_COL32_A_SHIFT);
      out->VtxBuffer[vertex_base+i]=vertex;
    }
    out->IdxBuffer.resize(index_base+saved->IdxBuffer.Size);
    std::copy(saved->IdxBuffer.begin(),saved->IdxBuffer.end(),out->IdxBuffer.begin()+index_base);
    for(auto command : saved->CmdBuffer) {
      if(!command.ElemCount || command.UserCallback) continue;
      command.IdxOffset+=index_base; command.VtxOffset+=vertex_base;
      out->CmdBuffer.push_back(command);
    }
  }
  out->AddDrawCmd();
  out->_VtxCurrentIdx=out->VtxBuffer.Size-out->_CmdHeader.VtxOffset;
  out->_VtxWritePtr=out->VtxBuffer.Data+out->VtxBuffer.Size;
  out->_IdxWritePtr=out->IdxBuffer.Data+out->IdxBuffer.Size;
}
void bring_popup_to_front(ImGuiWindow* window) {
  ImGui::BringWindowToDisplayFront(window);
  for(auto* child : window->DC.ChildWindows)
    if(child->Active && !child->Hidden && !(child->Flags&ImGuiWindowFlags_Popup)) bring_popup_to_front(child);
}
void finish_popup_fades(ImGuiContext*,ImGuiContextHook*) {
  std::vector<PopupMotion*> pending;
  const int frame=ImGui::GetFrameCount();
  for(auto& [id,motion] : popup_motions) {
    if(motion.frame==frame) {
      // Mouse dismissal changes display order after widgets have rendered.
      if(animate_widgets && motion.was_open && !ImGui::IsPopupOpen(id,ImGuiPopupFlags_AnyPopupLevel)) pending.push_back(&motion);
      continue;
    }
    if(motion.snapshot.empty() || (!motion.was_open && motion.closing>=1)) continue;
    motion.frame=frame;
    if(motion.was_open) { motion.closing=0; motion.closing_alpha=ease(motion.progress); }
    motion.was_open=false;
    motion.closing=animate_widgets ? std::min(1.f,motion.closing+ImGui::GetIO().DeltaTime/motion.close_seconds) : 1;
    if(motion.closing<1) pending.push_back(&motion);
  }
  std::sort(pending.begin(),pending.end(),[](const PopupMotion* a,const PopupMotion* b) {
    return a->depth!=b->depth ? a->depth<b->depth : a->order<b->order;
  });
  for(auto* motion : pending) {
    if(motion->was_open) {
      if(auto* window=ImGui::FindWindowByName(motion->window_name.c_str()); window && window->Active && !window->Hidden) bring_popup_to_front(window);
    } else replay_popup(*motion,motion->closing_alpha*(1-motion->closing*motion->closing*(3-2*motion->closing)));
  }
}
void animate_popup(ImGuiWindow* window,float alpha,float offset) {
  auto* draw=window->DrawList;
  for(auto& vertex : draw->VtxBuffer) {
    const auto opacity=static_cast<ImU32>(((vertex.col>>IM_COL32_A_SHIFT)&255)*alpha);
    vertex.col=(vertex.col&~IM_COL32_A_MASK)|(opacity<<IM_COL32_A_SHIFT);
    vertex.pos.y+=offset;
  }
  for(auto& command : draw->CmdBuffer) { command.ClipRect.y+=offset; command.ClipRect.w+=offset; }
  for(auto* child : window->DC.ChildWindows)
    if(child->Active && !child->Hidden && !(child->Flags & ImGuiWindowFlags_Popup)) animate_popup(child,alpha,offset);
}
}
float text_y(float top,float height) {
  auto* baked=ImGui::GetFontBaked();
  const auto* glyph=baked->FindGlyph('H');
  return top+height*.5f-(glyph->Y0+glyph->Y1)*.5f*ImGui::GetFontSize()/baked->Size+
    theme::text_center_offset*ImGui::GetStyle().FontScaleMain;
}
void icon_text(ImDrawList* draw,ImVec2 center,ImU32 color,const char* text) {
  unsigned code{}; ImTextCharFromUtf8(&code,text,nullptr);
  auto* baked=ImGui::GetFontBaked();
  const auto* glyph=baked->FindGlyph(static_cast<ImWchar>(code));
  const float ratio=ImGui::GetFontSize()/baked->Size;
  draw->AddText({center.x-(glyph->X0+glyph->X1)*.5f*ratio,
                 center.y-(glyph->Y0+glyph->Y1)*.5f*ratio},color,text);
}
// Tracked small-caps label used by sidebar and card captions.
float tracked_text(ImDrawList* draw,ImVec2 at,ImU32 color,const char* text,float tracking) {
  float x=at.x;
  for(const char* cursor=text;*cursor;) {
    const char* start=cursor; unsigned code{}; cursor+=ImTextCharFromUtf8(&code,cursor,nullptr);
    draw->AddText({x,at.y},color,start,cursor);
    x+=ImGui::CalcTextSize(start,cursor).x+tracking;
  }
  return x-at.x-(text[0] ? tracking : 0);
}
void row_text(const char* label,float height) {
  if(height<=0) height=theme::input_height*ImGui::GetStyle().FontScaleMain;
  const auto at=ImGui::GetCursorScreenPos();
  ImGui::Dummy({ImGui::CalcTextSize(label).x,height});
  ImGui::GetWindowDrawList()->AddText({at.x,text_y(at.y,height)},ImGui::GetColorU32(ImGuiCol_Text),label);
}
bool button(const char* label,ImVec2 size,int flags) {
  if(size.y==0) size.y=theme::input_height*ImGui::GetStyle().FontScaleMain;
  const auto text=ImGui::GetStyleColorVec4(ImGuiCol_Text);
  const auto fill=ImGui::GetStyleColorVec4(ImGuiCol_Button);
  const bool control=fill.w>0 && fill.x==theme::raised.x && fill.y==theme::raised.y && fill.z==theme::raised.z;
  if(control) { ImGui::PushStyleColor(ImGuiCol_ButtonHovered,theme::control_hover); ImGui::PushStyleColor(ImGuiCol_ButtonActive,theme::control_hover); }
  ImGui::PushStyleColor(ImGuiCol_Text,{text.x,text.y,text.z,0});
  const bool clicked=ImGui::ButtonEx(label,size,flags);
  ImGui::PopStyleColor();
  if(control) ImGui::PopStyleColor(2);
  const auto minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();
  const auto* end=ImGui::FindRenderedTextEnd(label);
  const float width=ImGui::CalcTextSize(label,end).x;
  const float align=ImGui::GetStyle().ButtonTextAlign.x;
  // Left-aligned labels sit flush with the row labels above them; centred ones keep the frame padding.
  const float padding=align<=.05f ? 0.f : ImGui::GetStyle().FramePadding.x;
  auto* draw=ImGui::GetWindowDrawList(); draw->PushClipRect(minimum,maximum,true);
  if(control)
    draw->AddRect(minimum,maximum,ImGui::GetColorU32(theme::border),ImGui::GetStyle().FrameRounding,ImDrawFlags_RoundCornersAll,theme::surface_border_width*ImGui::GetStyle().FontScaleMain);
  draw->AddText({minimum.x+padding+(maximum.x-minimum.x-2*padding-width)*align,
                 text_y(minimum.y,maximum.y-minimum.y)},ImGui::GetColorU32(text),label,end);
  draw->PopClipRect();
  return clicked;
}
bool input_text(const char* label,const char* hint,char* buffer,std::size_t capacity,ImGuiInputTextFlags flags,float height,float padding_x) {
  const float s=ImGui::GetStyle().FontScaleMain;
  if(height<=0) height=theme::input_height*s;
  if(padding_x<0) padding_x=theme::input_padding*s;
  // A zero-width item inherits ImGui's default item width, which can exceed the container and
  // push the field past the surface it sits on, so clamp it to the space that is really left.
  const float width=std::max(1.f,std::min(ImGui::CalcItemWidth(),ImGui::GetContentRegionAvail().x));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{padding_x,text_y(0,height)});
  flags&=~(ImGuiInputTextFlags_AllowTabInput|ImGuiInputTextFlags_CallbackCompletion);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,theme::surface_border_width*s);
  ImGui::PushStyleColor(ImGuiCol_TextDisabled,theme::input_hint());
  ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop,true);
  const bool changed=ImGui::InputTextEx(label,hint,buffer,static_cast<int>(capacity),{width,height},flags);
  ImGui::PopItemFlag();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
  return changed;
}
bool input_scalar(const char* label,ImGuiDataType type,void* value,const char* format,float height,float padding_x) {
  char buffer[96]; ImGui::DataTypeFormatString(buffer,sizeof(buffer),type,value,format);
  auto& context=*ImGui::GetCurrentContext();
  context.NextItemData.ItemFlagsSet|=ImGuiItemFlags_NoMarkEdited;
  input_text(label,nullptr,buffer,sizeof(buffer),ImGuiInputTextFlags_AutoSelectAll|ImGuiInputTextFlags_CharsScientific,height,padding_x);
  const bool edited=context.LastItemData.ItemFlags&ImGuiItemFlags_LiveEditOnInputScalar
    ? (context.LastItemData.StatusFlags&ImGuiItemStatusFlags_EditedInternal)!=0 : context.DeactivatedItemData.ID==context.LastItemData.ID;
  const bool changed=edited && ImGui::DataTypeApplyFromText(buffer,type,value,format);
  context.LastItemData.ItemFlags&=~ImGuiItemFlags_NoMarkEdited;
  if(changed) ImGui::MarkItemEdited(context.LastItemData.ID);
  return changed;
}
bool input_int(const char* label,int* value) { return input_scalar(label,ImGuiDataType_S32,value,"%d"); }
void reset() { popup_motions.clear(); popup_motion_stack.clear(); row_labels.clear(); row_flow.clear(); hotkeys::reset(); capturing_key=0;capture_reserved=false; capture_frame=-1; capture_seen=-1; animate_widgets=true; }
void configure(bool animations) {
  animate_widgets=animations;
  auto* context=ImGui::GetCurrentContext();
  bool registered=false;
  for(const auto& hook : context->Hooks) if(hook.Callback==finish_popup_fades) registered=true;
  if(!registered) {
    ImGuiContextHook hook; hook.Type=ImGuiContextHookType_EndFramePost; hook.Callback=finish_popup_fades;
    ImGui::AddContextHook(context,&hook);
  }
  if(capture_seen<ImGui::GetFrameCount()-1) capturing_key=0;
  for(auto it=popup_motions.begin();it!=popup_motions.end();) {
    if(it->second.frame<ImGui::GetFrameCount()-1 && !it->second.was_open && (it->second.snapshot.empty() || it->second.closing>=1)) it=popup_motions.erase(it);
    else ++it;
  }
}
bool key_selector(const char* id,ImGuiKey& key,bool disabled,bool listen,float height,float width) {
  disabled=disabled || (ImGui::GetCurrentContext()->CurrentItemFlags&ImGuiItemFlags_Disabled)!=0;
  ImGui::PushID(id);
  const auto item=ImGui::GetID("###capture");
  if(disabled && capturing_key==item) capturing_key=0;
  ImGui::BeginDisabled(disabled);
  if(listen && !disabled) { capture_reserved=false;capturing_key=item; capture_frame=ImGui::GetFrameCount(); }
  const bool capturing=capturing_key==item;
  // A narrow pill cannot hold the long listening prompt, so it uses a short one.
  const bool compact=width>0;
  const char* label=capturing ? (compact ? "Press key" : "Press a key...") : key==ImGuiKey_None ? "Unbound" : ImGui::GetKeyName(key);
  const std::string button=std::string(label)+"###capture";
  const float s=ImGui::GetStyle().FontScaleMain;
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{6*s,0});
  ImGui::PushStyleColor(ImGuiCol_Button,theme::raised);
  if(widgets::button(button.c_str(),{compact ? width : ImGui::GetContentRegionAvail().x,height*s},ImGuiButtonFlags_NoHoveredOnFocus) && !capturing) {
    capture_reserved=false;capturing_key=item;
    capture_frame=ImGui::GetFrameCount();
  }
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  bool changed=false;
  if(capturing && !disabled && ImGui::GetFrameCount()>capture_frame) {
    ImGui::SetNextFrameWantCaptureKeyboard(true);
    for(int candidate=ImGuiKey_NamedKey_BEGIN;candidate<ImGuiKey_NamedKey_END;++candidate) {
      const auto pressed=static_cast<ImGuiKey>(candidate);
      if((pressed>=ImGuiKey_GamepadStart && pressed<ImGuiKey_MouseLeft) || pressed>ImGuiKey_MouseX2 || !ImGui::IsKeyPressed(pressed,false)) continue;
      if(pressed==ImGuiKey_End){capture_reserved=true;continue;}
      capture_reserved=false;
      if(pressed!=ImGuiKey_Escape) {
        const auto next=pressed;
        changed=key!=next; key=next;
      }
      capturing_key=0;
      capture_frame=ImGui::GetFrameCount();
      break;
    }
  }
  if(capturing_key==item){capture_seen=ImGui::GetFrameCount();if(capture_reserved)ImGui::SetTooltip("End is reserved for stopping the overlay. Choose another key.");}
  ImGui::EndDisabled();
  ImGui::PopID();
  return changed;
}
bool keybind_owns_key(ImGuiKey key) {
  if(hotkeys::owns_key(key)) return true;
  if(capturing_key || capture_frame==ImGui::GetFrameCount()) return true;
  return false;
}
bool capturing_hotkey() { return capturing_key!=0 || capture_frame==ImGui::GetFrameCount(); }
bool navigation(const char* id, const char* label, bool selected, float width,
                MenuRuntime& state, Bounds& bounds) {
  const float s=state.scale;
  const auto p=ImGui::GetCursorScreenPos();
  const ImVec2 size{width,theme::nav_height*s};
  auto* draw=ImGui::GetWindowDrawList();
  ImGui::PushID(id);
  ImGui::PushStyleColor(ImGuiCol_Button,{0,0,0,0});
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,theme::control_hover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,theme::control_hover);
  const bool clicked=widgets::button("##destination",size);
  ImGui::PopStyleColor(3);
  bounds={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  ImGui::Dummy({0,theme::nav_gap*s});
  const auto color=theme::text;
  {
    const std::string_view name=label;
    if(name=="Legit" || name=="Rage") {
      const ImVec2 center{p.x+19*s,p.y+size.y*.5f};
      const auto tint=ImGui::GetColorU32(selected ? theme::accent : theme::secondary);
      draw->AddCircle(center,5*s,tint,24);
      draw->AddCircle(center,2.5f*s,tint,24);
      draw->AddCircleFilled(center,.9f*s,tint,12);
      if(name=="Rage") {draw->AddLine({center.x-7*s,center.y},{center.x+7*s,center.y},tint,s);draw->AddLine({center.x,center.y-7*s},{center.x,center.y+7*s},tint,s);}
    } else {
    const char* glyph=name=="Player" ? "\xee\x9d\xbb" :
      name=="World" ? "\xee\xa4\x89" : name=="Inventory" ? "\xee\xa0\x8a" :
      name=="Misc" ? "\xee\x9c\x93" : "\xee\xa2\xa5";
    ImGui::PushFont(state.icons,13);
    icon_text(draw,{p.x+19*s,p.y+size.y*.5f},ImGui::GetColorU32(selected ? theme::accent : theme::secondary),glyph);
    ImGui::PopFont();
    }
  }
  draw->AddText({p.x+40*s,text_y(p.y,size.y)},ImGui::GetColorU32(color),label);
  ImGui::PopID();
  return clicked;
}
void profile(MenuRuntime& state,float width) {
  const float s=state.scale;
  const auto p=ImGui::GetCursorScreenPos();
  const bool clicked=widgets::button("##profile",{width,theme::nav_height*s});
  state.profile={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  if(clicked || state.request_profile) { ImGui::OpenPopup("profile_settings"); state.request_profile=false; }
  auto* draw=ImGui::GetWindowDrawList();
  draw->AddCircleFilled({p.x+19*s,p.y+18*s},11*s,ImGui::GetColorU32(theme::surface),32);
  draw->AddCircle({p.x+19*s,p.y+18*s},11*s,ImGui::GetColorU32(theme::border),32);
  const auto letter=ImGui::CalcTextSize("N");
  draw->AddText({p.x+19*s-letter.x*.5f,text_y(p.y,theme::nav_height*s)},ImGui::GetColorU32(theme::accent_label()),"N");
  ImGui::PushFont(nullptr,theme::caption_size);
  const float line=theme::nav_height*s*.5f;
  draw->AddText({p.x+40*s,text_y(p.y,line)},ImGui::GetColorU32(theme::text),"Local profile");
  draw->AddText({p.x+40*s,text_y(p.y+line,line)},ImGui::GetColorU32(theme::secondary),"Settings");
  ImGui::PopFont();
  ImGui::PushFont(state.icons,theme::profile_icon);
  icon_text(draw,{p.x+width-12*s,p.y+theme::nav_height*s*.5f},ImGui::GetColorU32(theme::secondary),"\xee\x9d\xac");
  ImGui::PopFont();
  if(!begin_popup("profile_settings",state.profile,theme::profile_popup_width,theme::profile_popup_height,nullptr,true,false,false,true,false,false,{10,4})) return;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{theme::setting_gap*s,theme::item_spacing_y*s});
  state.profile_popup={ImGui::GetWindowPos(),{ImGui::GetWindowPos().x+ImGui::GetWindowSize().x,ImGui::GetWindowPos().y+ImGui::GetWindowSize().y}};
  const auto before=encode_settings(state.settings);
  auto& settings=state.settings;
  const auto row=[&](int index,const char* glyph,const auto& contents) {
    const auto at=ImGui::GetCursorScreenPos();
    // These rows are built by hand, so they take part in the divider rhythm explicitly.
    row_rule(at,ImGui::GetContentRegionAvail().x,theme::row_height*s);
    if(glyph) {
      ImGui::PushFont(state.icons,theme::profile_icon);
      icon_text(ImGui::GetWindowDrawList(),{at.x+theme::profile_icon*s*.5f,at.y+theme::row_height*s*.5f},ImGui::GetColorU32(theme::secondary),glyph);
      ImGui::PopFont();
    }
    ImGui::BeginGroup();
    ImGui::Indent(theme::profile_icon_column*s);
    contents();
    ImGui::Unindent(theme::profile_icon_column*s);
    ImGui::EndGroup();
    state.profile_controls[index]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  };
  static constexpr Option scales[]{{"Auto"},{"75%"},{"100%"},{"125%"},{"150%"},{"175%"},{"200%"}};
  row(0,"\xee\xa3\xa7",[&] { combo("profile_dpi","DPI scale",settings.scale,scales,false,nullptr,false,true,hotkeys::binding("Profile/DPI scale")); });
  row(7,"\xee\x9c\x86",[&] {
    const auto origin=ImGui::GetCursorScreenPos();
    const float available=ImGui::GetContentRegionAvail().x;
    if(color_selector("profile_accent","Appearance",settings.accent,state.accent_selector,state.color_palette,false,true)) state.theme_pending=true;
    const auto after=ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({origin.x+available-74*s,origin.y+(theme::row_height-theme::input_height)*s*.5f});
    ImGui::PushID("profile_mode");
    if(button("##theme",{32*s,theme::input_height*s})) {
      state.appearance_from=state.appearance_position;
      settings.color_mode=settings.color_mode==0 ? 1 : 0;
      state.appearance_motion.start(settings.animations);state.theme_pending=true;
    }
    const Bounds bounds{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
    state.appearance_controls[0]=state.appearance_controls[1]=bounds;
    if(ImGui::IsItemHovered()) ImGui::SetTooltip(settings.color_mode ? "Switch to dark mode" : "Switch to light mode");
    state.appearance_position=state.appearance_from+(settings.color_mode-state.appearance_from)*state.appearance_motion.step(settings.animations,theme::page_transition_seconds);
    ImGui::PushFont(state.icons,theme::profile_icon);
    for(int mode=0;mode<2;++mode) {
      auto tint=theme::text;tint.w*=mode ? state.appearance_position : 1-state.appearance_position;
      icon_text(ImGui::GetWindowDrawList(),{(bounds.min.x+bounds.max.x)*.5f,(bounds.min.y+bounds.max.y)*.5f},ImGui::GetColorU32(tint),mode ? "\xee\x9c\x86" : "\xee\x9c\x88");
    }
    ImGui::PopFont();ImGui::PopID();
    ImGui::SetCursorScreenPos(after);
  });
  static constexpr Option safe_modes[]{{"Automatic"},{"On"},{"Off"}};
  row(1,"\xee\xa8\x98",[&] { combo("profile_safe","Safe mode",settings.safe_mode,safe_modes,false,nullptr,false,true,hotkeys::binding("Profile/Safe mode")); });
  const auto supported=[](ImGuiKey key) {
    return (key>=ImGuiKey_A && key<=ImGuiKey_Z) || (key>=ImGuiKey_0 && key<=ImGuiKey_9) ||
      (key>=ImGuiKey_F1 && key<=ImGuiKey_F24) || key==ImGuiKey_Escape || key==ImGuiKey_Delete || key==ImGuiKey_Insert ||
      key==ImGuiKey_Home || key==ImGuiKey_PageUp || key==ImGuiKey_PageDown || key==ImGuiKey_Space || key==ImGuiKey_Tab ||
      key==ImGuiKey_Enter || key==ImGuiKey_Backspace || (key>=ImGuiKey_LeftArrow && key<=ImGuiKey_DownArrow);
  };
  const auto key_entry=[&](bool close) {
    auto candidate=imgui_key(close ? settings.close_key : settings.hotkey);
    const char* id=close ? "profile_close" : "profile_key";
    if(widgets::key_row(id,close ? "Close menu" : "Menu hotkey",candidate)) {
      if(supported(candidate) && virtual_key(candidate)!=(close ? settings.hotkey : settings.close_key)) {
        (close ? settings.close_key : settings.hotkey)=virtual_key(candidate); settings.error.clear();
      } else settings.error="Choose distinct keyboard keys. End is reserved.";
    }
  };
  row(2,"\xee\x9d\xa5",[&] { key_entry(false); });
  row(3,"\xee\x9d\xa5",[&] { key_entry(true); });
  const auto about_at=ImGui::GetCursorScreenPos();
  const float about_width=ImGui::GetContentRegionAvail().x;
  row_rule(about_at,about_width,theme::row_height*s);
  ImGui::SetCursorScreenPos({about_at.x-theme::action_overhang*s,about_at.y});
  const bool open_about=ImGui::InvisibleButton("##profile_about_action",{about_width+2*theme::action_overhang*s,theme::row_height*s});
  const Bounds about{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  draw=ImGui::GetWindowDrawList();
  if(ImGui::IsItemHovered() || ImGui::IsItemActive())draw->AddRectFilled({about.min.x,about.min.y+2*s},{about.max.x,about.max.y-2*s},ImGui::GetColorU32(theme::control_hover),theme::control_radius*s);
  ImGui::PushFont(state.icons,theme::profile_icon);
  icon_text(draw,{about_at.x+theme::profile_icon*s*.5f,about_at.y+theme::row_height*s*.5f},ImGui::GetColorU32(theme::secondary),"\xee\xa5\x86");
  ImGui::PopFont();
  draw->AddText({about_at.x+theme::profile_icon_column*s,text_y(about_at.y,theme::row_height*s)},ImGui::GetColorU32(theme::text),"About");
  if(open_about)ImGui::OpenPopup("profile_about");
  if(state.request_about) { ImGui::OpenPopup("profile_about"); state.request_about=false; }
  ImGui::SetCursorScreenPos(about_at);ImGui::Dummy({about_width,theme::row_height*s});
  state.about_button=about;
  if(begin_popup("profile_about",state.window,theme::about_width,theme::about_height,nullptr,false,false,false,true,true,false,{10,10})) {
    const auto minimum=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
    state.about_popup={minimum,{minimum.x+size.x,minimum.y+size.y}};
    ImGui::PushFont(state.bold,theme::heading_size);
    const auto heading_at=ImGui::GetCursorScreenPos();
    row_text(app_name,theme::about_heading_height*s);
    ImGui::PopFont();
    const auto after_heading=ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({state.about_popup.max.x-10*s-theme::input_height*s,heading_at.y});
    const bool close=widgets::button("##about_close",{theme::input_height*s,theme::input_height*s});
    state.about_close={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
    icon_text(ImGui::GetWindowDrawList(),{(state.about_close.min.x+state.about_close.max.x)*.5f,(state.about_close.min.y+state.about_close.max.y)*.5f},ImGui::GetColorU32(theme::text),"\xc3\x97");
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Close");
    ImGui::SetCursorScreenPos(after_heading);
    const std::string version=std::string("Version: ")+app_version;
    const std::string build=std::string("Build date: ")+__DATE__;
    const std::string author=std::string("Author: ")+app_author;
    row_text(version.c_str(),theme::about_line_height*s);
    row_text(build.c_str(),theme::about_line_height*s);
    row_text(author.c_str(),theme::about_line_height*s);
    ImGui::PushStyleColor(ImGuiCol_Text,theme::secondary);
    row_text("Inventory, visuals and interface controls.",theme::about_line_height*s);
    ImGui::PopStyleColor();
    ImGui::Dummy({0,theme::setting_gap*s});
    toggle("profile_animations","Animations",settings.animations,false,nullptr,false,true,hotkeys::binding("Profile/Animations"));
    if(toggle("autosave","Autosave",settings.autosave,false,nullptr,false,true)) {
      state.persist_pending=false;if(settings.autosave)persist_features(state);
    }
    if(close || ImGui::IsKeyPressed(ImGuiKey_Escape,false) ||
       (!ImGui::IsWindowAppearing() && ImGui::IsMouseReleased(0) && !ImGui::IsMouseHoveringRect(state.about_popup.min,state.about_popup.max,false))) ImGui::CloseCurrentPopup();
    end_popup();
  }
  if(!settings.error.empty()) ImGui::TextWrapped("%s",settings.error.c_str());
  ImGui::PopStyleVar();
  if(before!=encode_settings(state.settings)) { state.theme_pending=true; save_settings(state); }
  end_popup();
}
void section(const char* id, const char* label, MenuRuntime& state) {
  const float s=state.scale;
  ImGui::PushID(id);
  const auto at=ImGui::GetCursorScreenPos();
  ImGui::PushFont(state.body,theme::caption_size);
  const float advance=tracked_text(ImGui::GetWindowDrawList(),{at.x+2*s,at.y},ImGui::GetColorU32(theme::muted),label,1.1f*s);
  const float height=ImGui::GetFontSize();
  ImGui::PopFont();
  ImGui::Dummy({std::max(1.f,advance),height});
  ImGui::Dummy({0,4*s});
  ImGui::PopID();
}
void config_row(MenuRuntime& state,float width) {
  const float s=state.scale, h=32*s, save_width=32*s, gap=8*s;
  auto* draw=ImGui::GetWindowDrawList();
  auto p=ImGui::GetCursorScreenPos();
  ImGui::BeginDisabled(state.apply_pending || state.config_busy);
  const bool choose=widgets::button("##profile_selector",{width-save_width-gap,h});
  if(choose && state.configs_ready && !state.config_busy) ImGui::OpenPopup("sidebar_profiles");
  ImGui::EndDisabled();
  state.selector={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  draw->AddRectFilled(p,state.selector.max,ImGui::GetColorU32(theme::raised),theme::control_radius*s);
  draw->PushClipRect({p.x+10*s,p.y},{state.selector.max.x-24*s,state.selector.max.y},true);
  draw->AddText({p.x+theme::input_padding*s,text_y(p.y,h)},ImGui::GetColorU32(theme::text),state.active_config.c_str());
  draw->PopClipRect();
  const float cx=state.selector.max.x-14*s, cy=p.y+h*.5f;
  chevron({cx,cy});
  const float profiles_width=std::max((state.selector.max.x-state.selector.min.x)/s,
    ImGui::CalcTextSize("Search profiles...").x/s+2*theme::input_padding)+12;
  if(begin_popup("sidebar_profiles",state.selector,profiles_width,12+30+theme::setting_gap+std::min(theme::popup_rows,static_cast<float>(state.configs.size()))*theme::option_height,nullptr,false,false,false,false,false,false,{6,6})) {
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    widgets::input_text("##profile_search","Search profiles...",state.sidebar_search,sizeof(state.sidebar_search),0,30*s);
    ImGui::Dummy({0,theme::setting_gap*s});
    for(int i=0;i<static_cast<int>(state.configs.size());++i) {
      if(!text_matches(state.configs[i].name.c_str(),state.sidebar_search))continue;
      ImGui::PushID(i);
      if(popup_item(state.configs[i].name.c_str(),state.active_config==state.configs[i].name)) {
        request_load(state,state.configs[i].name);
        ImGui::CloseCurrentPopup();
      }
      ImGui::PopID();
    }
    end_popup();
  }
  ImGui::SameLine(0,gap);
  p=ImGui::GetCursorScreenPos();
  ImGui::BeginDisabled(state.apply_pending || state.config_busy);
  if(widgets::button("##save_configuration",{save_width,h})) { save_config(state); }
  ImGui::EndDisabled();
  state.save={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  draw->AddRectFilled(p,state.save.max,ImGui::GetColorU32(theme::raised),theme::control_radius*s);
  ImGui::PushFont(state.icons,14);
  icon_text(draw,{p.x+save_width*.5f,p.y+h*.5f},ImGui::GetColorU32(theme::secondary),"\xee\x9d\x8e");
  ImGui::PopFont();
}
namespace {
float scale() { return ImGui::GetStyle().FontScaleMain; }
struct Row {
  ImVec2 origin;
  float width,height,control_y;
  bool disabled;
  std::string display_label;
  float label_end{}, label_center{};
  Row(const char* id,const char* label,float controls,bool disabled,bool compact=false,ImGuiID binding=0,bool action=false):disabled(disabled) {
    const float s=scale();
    for(const auto& parent : row_labels) display_label+=parent+" / ";
    display_label+=label; row_labels.emplace_back(label);
    ImGui::PushID(id);
    const auto found=hotkeys::options().find(binding ? binding : ImGui::GetID("hotkeys"));
    const bool decorated=found!=hotkeys::options().end() && std::any_of(found->second.entries.begin(),found->second.entries.end(),[](const hotkeys::Hotkey& entry) { return entry.key!=ImGuiKey_None; });
    const float decoration=decorated ? (theme::hotkey_dot+theme::hotkey_dot_gap)*s : 0;
    ImGui::PushStyleColor(ImGuiCol_NavCursor,{0,0,0,0});
    ImGui::BeginGroup();
    origin=ImGui::GetCursorScreenPos();
    width=ImGui::GetContentRegionAvail().x;
    const auto text_size=ImGui::CalcTextSize(label);
    const bool stacked=!compact && text_size.x+decoration+controls+theme::setting_gap*s>width;
    const float row_height=theme::row_height;
    height=row_height*s+(stacked ? text_size.y+4*s : 0);
    control_y=origin.y+(stacked ? text_size.y+4*s : 0)+(row_height-theme::input_height)*s*.5f;
    auto* draw=ImGui::GetWindowDrawList();
    row_rule(origin,width,height);
    if(action && !disabled && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect({origin.x-theme::action_overhang*s,origin.y},{origin.x+width+theme::action_overhang*s,origin.y+height})) {
      draw->AddRectFilled({origin.x-theme::action_overhang*s,origin.y+2*s},{origin.x+width+theme::action_overhang*s,origin.y+height-2*s},ImGui::GetColorU32(theme::control_hover),theme::control_radius*s);
    }
    const float label_width=std::max(1.f,(stacked ? width : width-controls-theme::setting_gap*s)-decoration);
    label_end=origin.x+std::min(text_size.x,label_width);
    label_center=origin.y+(stacked ? text_size.y : height)*.5f;
    const ImVec2 label_pos{origin.x,text_y(origin.y,stacked ? text_size.y : height)};
    ImGui::PushStyleColor(ImGuiCol_Text,disabled ? theme::secondary : theme::text);
    ImGui::RenderTextEllipsis(draw,label_pos,{label_pos.x+label_width,label_pos.y+text_size.y},
      label_pos.x+label_width,label,nullptr,&text_size);
    ImGui::PopStyleColor();
    if(text_size.x>label_width && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(label_pos,{label_pos.x+label_width,label_pos.y+text_size.y},false))
      ImGui::SetTooltip("%s",label);
    ImGui::SetCursorScreenPos({origin.x+std::max(0.f,width-controls),control_y});
    ImGui::BeginDisabled(disabled);
  }
  ~Row() {
    row_labels.pop_back();
    ImGui::EndDisabled();
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({width,height});
    ImGui::EndGroup();
    ImGui::PopStyleColor();
    ImGui::PopID();
  }
};
void row_hotkeys(Row& row,hotkeys::Kind kind,const char* id,bool enabled,ImGuiID binding=0) {
  if(!enabled || popup_closing()) return;
  const auto key=binding ? binding : ImGui::GetID("hotkeys");
  auto& option=hotkeys::option(key); option.kind=kind; option.label=row.display_label;
  char identifier[16]{}; std::snprintf(identifier,sizeof(identifier),"%08x",key);
  if(!binding) option.path=std::string(ImGui::GetCurrentWindow()->Name)+" / "+id+" / "+identifier;
  const Bounds bounds{row.origin,{row.origin.x+row.width,row.origin.y+row.height}};
  if(std::any_of(option.entries.begin(),option.entries.end(),[](const hotkeys::Hotkey& entry) { return entry.key!=ImGuiKey_None; })) {
    const float s=scale(),radius=theme::hotkey_dot*s*.5f;
    const float x=row.label_end+theme::hotkey_dot_gap*s+radius;
    ImGui::GetWindowDrawList()->AddCircleFilled({x,row.label_center},radius,ImGui::GetColorU32(theme::accent));
  }
  hotkeys::popup(key,option,bounds,bounds,row.disabled,kind==hotkeys::Kind::Boolean);
}
Bounds last_bounds() { return {ImGui::GetItemRectMin(),ImGui::GetItemRectMax()}; }
void settings_action(const PopupContents* settings) {
  if(!settings) return;
  const float s=scale();
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,theme::surface_tint(theme::surface,theme::text,.06f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,theme::surface_tint(theme::surface,theme::text,.10f));
  const bool opened=widgets::button("##settings",{theme::more_width*s,theme::input_height*s},ImGuiButtonFlags_NoHoveredOnFocus);
  ImGui::PopStyleColor(2);
  if(opened || (settings->request_open && !ImGui::IsPopupOpen("settings"))) ImGui::OpenPopup("settings");
  const auto anchor=last_bounds();
  const ImVec2 center{(anchor.min.x+anchor.max.x)*.5f,(anchor.min.y+anchor.max.y)*.5f};
  const auto dot=ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride) ? theme::text : theme::muted;
  for(int i=-1;i<=1;++i) ImGui::GetWindowDrawList()->AddCircleFilled(
    {center.x+i*4*s,center.y},1.2f*s,ImGui::GetColorU32(dot),12);
  if(begin_popup("settings",anchor,theme::popup_width,settings->height,nullptr,true)) {
    if(settings->draw) settings->draw(settings->data);
    end_popup();
  }
  ImGui::SameLine(0,theme::setting_gap*s);
}
void field_text(const char* text,Bounds bounds,bool disabled) {
  const float s=scale();
  auto* draw=ImGui::GetWindowDrawList();
  const ImVec2 p{bounds.min.x+theme::input_padding*s,text_y(bounds.min.y,bounds.max.y-bounds.min.y)};
  ImGui::PushStyleColor(ImGuiCol_Text,disabled ? theme::secondary : theme::text);
  ImGui::RenderTextEllipsis(draw,p,{bounds.max.x-26*s,bounds.max.y},bounds.max.x-26*s,text,nullptr,nullptr);
  ImGui::PopStyleColor();
  const float x=bounds.max.x-13*s,y=(bounds.min.y+bounds.max.y)*.5f;
  auto* storage=ImGui::GetStateStorage();
  const auto rotation_id=ImGui::GetID("chevron_motion");
  const float target=ImGui::IsPopupOpen("options") ? 1.f : 0.f;
  float rotation=storage->GetFloat(rotation_id);
  rotation=animate_widgets ? rotation+(target-rotation)*(1-std::exp(-3*ImGui::GetIO().DeltaTime/theme::popup_open_seconds)) : target;
  storage->SetFloat(rotation_id,rotation); chevron({x,y},rotation);
}
bool option_item(int index,const Option& option,bool selected,bool keep_open) {
  ImGui::PushID(index);
  ImGui::BeginDisabled(option.disabled);
  const bool changed=popup_item(option.label,selected,keep_open);
  ImGui::EndDisabled();
  ImGui::PopID();
  return changed;
}

}
RowResult panel_row(const char* id,const char* label,bool disabled) {
  const float s=scale();
  Row row(id,label,(theme::chevron_size+theme::setting_gap)*s,disabled,true,0,true);
  ImGui::SetCursorScreenPos({row.origin.x-theme::action_overhang*s,row.origin.y});
  const bool clicked=ImGui::InvisibleButton("##panel",{row.width+2*theme::action_overhang*s,row.height});
  chevron({row.origin.x+row.width-(theme::chevron_size*.5f+3)*s,row.origin.y+row.height*.5f},-.5f);
  return {clicked,{row.origin,{row.origin.x+row.width,row.origin.y+row.height}}};
}
bool key_row(const char* id,const char* label,ImGuiKey& key,bool disabled) {
  const float s=scale();
  // A key name is short, so the control hugs it instead of filling a full-width field.
  const char* name=key==ImGuiKey_None ? "Unbound" : ImGui::GetKeyName(key);
  const float width=std::clamp(ImGui::CalcTextSize(name).x+2*theme::input_padding*s,
                               theme::keybind_min_width*s,theme::keybind_max_width*s);
  Row row(id,label,width,disabled,true);
  ImGui::PushStyleColor(ImGuiCol_Button,theme::raised);
  const bool changed=key_selector(id,key,disabled,false,theme::keybind_height,width);
  ImGui::PopStyleColor();
  return changed;
}
void chevron(ImVec2 center,float rotation) {
  const float s=scale(),half=theme::chevron_size*s*.5f;
  const float angle=rotation*3.141592653589793f,c=std::cos(angle),sn=std::sin(angle);
  const auto point=[&](float x,float y) { return ImVec2{center.x+x*c-y*sn,center.y+x*sn+y*c}; };
  auto* draw=ImGui::GetWindowDrawList();
  draw->AddLine(point(-half,-half*.5f),point(0,half*.5f),ImGui::GetColorU32(theme::muted),theme::chevron_stroke*s);
  draw->AddLine(point(0,half*.5f),point(half,-half*.5f),ImGui::GetColorU32(theme::muted),theme::chevron_stroke*s);
}
bool popup_item(const char* label,bool selected,bool keep_open,const char* description) {
  const float s=scale(),height=(description ? theme::inventory_action_row : theme::option_height)*s;
  const auto at=ImGui::GetCursorScreenPos();
  ImGui::PushStyleColor(ImGuiCol_Header,{0,0,0,0});
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered,{0,0,0,0});
  ImGui::PushStyleColor(ImGuiCol_HeaderActive,{0,0,0,0});
  const bool clicked=ImGui::Selectable("##option",selected,static_cast<ImGuiSelectableFlags>(ImGuiSelectableFlags_NoPadWithHalfSpacing)|(keep_open ? ImGuiSelectableFlags_NoAutoClosePopups : ImGuiSelectableFlags_None),{ImGui::GetContentRegionAvail().x,height});
  ImGui::PopStyleColor(3);
  const auto end=ImGui::GetItemRectMax();
  auto* draw=ImGui::GetWindowDrawList();
  if(ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride))
    draw->AddRectFilled(at,end,ImGui::GetColorU32(theme::surface_tint(theme::surface,theme::text,.05f)),theme::control_radius*s);
  const float right=end.x-(theme::popup_item_padding+(selected ? theme::popup_check_space : 0))*s;
  ImGui::PushStyleColor(ImGuiCol_Text,selected ? theme::mix(theme::accent_label(),theme::text,.35f) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
  const ImVec2 text_at{at.x+theme::popup_item_padding*s,text_y(at.y,description ? height*.5f : height)};
  ImGui::RenderTextEllipsis(draw,text_at,{right,end.y},right,label,nullptr,nullptr);
  ImGui::PopStyleColor();
  if(description) {
    ImGui::PushFont(nullptr,theme::caption_size);
    ImGui::PushStyleColor(ImGuiCol_Text,theme::muted);
    const ImVec2 description_at{at.x+theme::popup_item_padding*s,text_y(at.y+height*.5f,height*.5f)};
    ImGui::RenderTextEllipsis(draw,description_at,{end.x-theme::popup_item_padding*s,end.y},end.x-theme::popup_item_padding*s,description,nullptr,nullptr);
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  if(selected) {
    const ImVec2 center{end.x-(theme::popup_item_padding+theme::chevron_size*.5f)*s,at.y+height*.5f};
    const auto color=ImGui::GetColorU32(theme::mix(theme::accent_label(),theme::text,.35f));
    draw->AddLine({center.x-4*s,center.y},{center.x-s,center.y+3*s},color,1.5f*s);
    draw->AddLine({center.x-s,center.y+3*s},{center.x+5*s,center.y-4*s},color,1.5f*s);
    if(ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
  }
  return clicked;
}
float dropdown_width(Bounds field) {
  // Lists never shrink below a readable width, even beside a narrow field.
  return std::max((field.max.x-field.min.x)/scale(),theme::select_min_width);
}
bool begin_dropdown(const char* id,const char* preview,int count,float width) {
  if(width<=0) width=ImGui::CalcItemWidth();
  ImGui::PushStyleColor(ImGuiCol_Button,theme::raised);
  const bool clicked=button(id,{width,theme::input_height*scale()},ImGuiButtonFlags_NoHoveredOnFocus);
  ImGui::PopStyleColor();
  const auto item=ImGui::GetCurrentContext()->LastItemData;
  const Bounds anchor{item.Rect.Min,item.Rect.Max};
  ImGui::PushID(id);
  if(clicked) ImGui::OpenPopup("options");
  field_text(preview,anchor,false);
  const auto* viewport=ImGui::GetMainViewport();
  const float maximum=(viewport->WorkSize.y/scale()-2*(theme::popup_gap+theme::popup_padding))/theme::option_height;
  const float rows=std::max(1.f,std::min({static_cast<float>(count),theme::popup_rows,std::floor(maximum)}));
  if(!begin_popup("options",anchor,dropdown_width(anchor),2*theme::popup_padding+rows*theme::option_height,nullptr,false,true,false,false)) {
    ImGui::PopID(); ImGui::GetCurrentContext()->LastItemData=item; return false;
  }
  dropdown_items.push_back(item);
  return true;
}
void end_dropdown() {
  end_popup(); ImGui::PopID();
  ImGui::GetCurrentContext()->LastItemData=dropdown_items.back(); dropdown_items.pop_back();
}
void tooltip(const char* title,const char* detail) {
  const float s=scale(),padding=theme::popup_padding*s;
  const bool has_detail=detail && *detail;
  const auto first=ImGui::CalcTextSize(title),second=has_detail ? ImGui::CalcTextSize(detail) : ImVec2{};
  ImGui::SetNextWindowSize({std::max(first.x,second.x)+2*padding,first.y+(has_detail ? second.y+theme::item_spacing_y*s : 0)+2*padding});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{padding,padding});
  if(ImGui::BeginTooltipEx(ImGuiTooltipFlags_None,ImGuiWindowFlags_NoBackground)) {
    auto* window=ImGui::GetCurrentWindow();
    const ImVec2 end{window->Pos.x+window->Size.x,window->Pos.y+window->Size.y};
    backdrop::surface(window->DrawList,window->Pos,end,ImGui::GetStyle().PopupRounding,theme::popup_surface);
    window->DrawList->AddRect(window->Pos,end,ImGui::GetColorU32(theme::border),ImGui::GetStyle().PopupRounding,0,theme::surface_border_width*s);
    ImGui::TextUnformatted(title);
    if(has_detail) ImGui::TextColored(theme::secondary,"%s",detail);
    ImGui::EndTooltip();
  }
  ImGui::PopStyleVar();
}
bool begin_popup(const char* id,Bounds anchor,float width,float height,const char* title,bool beside,bool viewport_only,bool compact,bool fit_content,bool modal,bool preserve_focus,ImVec2 padding) {
  const auto key=ImGui::GetID(id);
  auto& motion=popup_motions[key];
  motion.close_seconds=compact ? theme::hotkey_fade : theme::popup_close_seconds;
  const bool native_open=ImGui::IsPopupOpen(id);
  const bool advance=motion.frame!=ImGui::GetFrameCount();
  motion.frame=ImGui::GetFrameCount();
  if(!native_open) {
    if(motion.was_open) { motion.closing=0; motion.closing_alpha=ease(motion.progress); }
    motion.was_open=false;
    if(!animate_widgets) motion.closing=1;
    else if(advance) motion.closing=std::min(1.f,motion.closing+ImGui::GetIO().DeltaTime/(compact ? theme::hotkey_fade : theme::popup_close_seconds));
    if(motion.closing>=1 || motion.window_name.empty()) return false;
    replay_popup(motion,motion.closing_alpha*(1-motion.closing*motion.closing*(3-2*motion.closing)));
    return false;
  }
  else if(!motion.was_open) {
    const float alpha=motion.window_name.empty() || motion.closing>=1 ? 0 : motion.closing_alpha*(1-motion.closing*motion.closing*(3-2*motion.closing));
    motion.progress=1-std::cbrt(std::max(0.f,1-alpha));
  }
  motion.was_open=native_open;
  motion.fading=!native_open;
  if(!animate_widgets) motion.progress=1;
  const float s=scale(),gap=theme::popup_gap*s;
  const auto* viewport=ImGui::GetMainViewport();
  ImVec2 min=viewport->WorkPos,max{min.x+viewport->WorkSize.x,min.y+viewport->WorkSize.y};
  width=std::min(width*s,std::max(1.f,max.x-min.x-2*gap));
  if(fit_content && motion.content_height>0) height=motion.content_height;
  height=std::min(height*s,std::max(1.f,max.y-min.y-2*gap));
  ImVec2 at{anchor.min.x,anchor.max.y+gap};
  if(beside && anchor.max.x+gap+width<=max.x-gap) at={anchor.max.x+gap,anchor.min.y};
  else if(beside && anchor.min.x-gap-width>=min.x+gap) at={anchor.min.x-gap-width,anchor.min.y};
  else if(beside && compact) at={anchor.max.x+gap<max.x-anchor.min.x ? anchor.max.x+gap : anchor.min.x-gap-width,anchor.min.y};
  else if(at.y+height>max.y-gap) at.y=anchor.min.y-gap-height;
  at.x=std::clamp(at.x,min.x+gap,std::max(min.x+gap,max.x-width-gap));
  at.y=std::clamp(at.y,min.y+gap,std::max(min.y+gap,max.y-height-gap));
  if(modal) {
    at={(anchor.min.x+anchor.max.x-width)*.5f,(anchor.min.y+anchor.max.y-height)*.5f};
    at.x=std::clamp(at.x,min.x+gap,std::max(min.x+gap,max.x-width-gap));
    at.y=std::clamp(at.y,min.y+gap,std::max(min.y+gap,max.y-height-gap));
  }
  motion.position=at; motion.size={width,height};
  ImGui::SetNextWindowPos(at,ImGuiCond_Always);
  ImGui::SetNextWindowSize({width,height},ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{(padding.x<0 ? theme::popup_padding_x : padding.x)*s,(padding.y<0 ? theme::popup_padding : padding.y)*s});
  const auto flags=ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoNav|(preserve_focus ? ImGuiWindowFlags_NoFocusOnAppearing : 0);
  ImGui::PushStyleColor(ImGuiCol_WindowBg,ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,ImGui::GetStyle().PopupRounding);
  // Geometry is owned here; ImGui's auto-resize popup wrapper suppresses scrolling
  // when a bounded list contains more rows than fit in this explicit height.
  const bool open=modal ? ImGui::BeginPopupModal(id,nullptr,flags|ImGuiWindowFlags_NoTitleBar) :
    ImGui::BeginPopupEx(key,flags|ImGuiWindowFlags_NoTitleBar);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  if(!open) return false;
  auto* popup=ImGui::GetCurrentWindow();
  // Search suggestions stay above the menu without taking keyboard focus.
  if(preserve_focus)bring_popup_to_front(popup);
  auto* draw=popup->DrawList;
  // ImGui has already drawn its scrollbar in Begin. Put our material underneath it.
  draw->ChannelsSplit(2);
  draw->ChannelsSetCurrent(1);
  draw->PushClipRect(popup->Pos,{popup->Pos.x+popup->Size.x,popup->Pos.y+popup->Size.y},false);
  backdrop::surface(draw,{popup->Pos,{popup->Pos.x+popup->Size.x,popup->Pos.y+popup->Size.y}},ImGui::GetStyle().PopupRounding,ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
  draw->AddRect(popup->Pos,{popup->Pos.x+popup->Size.x,popup->Pos.y+popup->Size.y},ImGui::GetColorU32(theme::border),ImGui::GetStyle().PopupRounding,ImDrawFlags_RoundCornersAll,ImGui::GetStyle().PopupBorderSize);
  draw->PopClipRect();
  draw->ChannelsSetCurrent(0);
  draw->CmdBuffer.swap(draw->_Splitter._Channels[1]._CmdBuffer);
  draw->IdxBuffer.swap(draw->_Splitter._Channels[1]._IdxBuffer);
  draw->ChannelsMerge();
  if(modal && !animate_widgets) ImGui::GetCurrentContext()->DimBgRatio=1;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{theme::setting_gap*s,0});
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,theme::surface_tint(theme::surface,theme::text,.05f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,theme::surface_tint(theme::surface,theme::text,.05f));
  motion.window_name=ImGui::GetCurrentWindow()->Name;
  motion.depth=ImGui::GetCurrentContext()->BeginPopupStack.Size;
  motion.order=ImGui::GetCurrentWindow()->BeginOrderWithinContext;
  ImGui::PushOverrideID(key);
  if(native_open && animate_widgets && advance && !ImGui::GetCurrentWindow()->Hidden)
    motion.progress=std::min(1.f,motion.progress+ImGui::GetIO().DeltaTime/(compact ? theme::hotkey_fade : theme::popup_open_seconds));
  popup_motion_stack.push_back(key);
  if(title && *title) {
    row_text(title,theme::option_height*s);
    ImGui::Separator();
    ImGui::Dummy({0,(theme::popup_title_height-theme::option_height-theme::hotkey_divider)*s});
  }
  return true;
}
void end_popup() {
  auto& motion=popup_motions.at(popup_motion_stack.back());
  auto* window=ImGui::GetCurrentWindow();
  motion.content_height=(window->DC.CursorMaxPos.y-window->DC.CursorStartPos.y+2*window->WindowPadding.y)/scale();
  popup_motion_stack.pop_back();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();
  ImGui::PopID(); ImGui::EndPopup();
  if(!window->Hidden) {
    if(!motion.fading) { motion.snapshot.clear(); motion.snapshot_valid=true; snapshot_popup(window,motion); }
    if(animate_widgets) {
      const float alpha=motion.fading ? motion.closing_alpha*(1-motion.closing*motion.closing*(3-2*motion.closing)) : ease(motion.progress);
      animate_popup(window,alpha,0);
      if(motion.fading) ImGui::BringWindowToDisplayFront(window);
    }
  }
}
bool popup_closing() { return !popup_motion_stack.empty() && popup_motions.at(popup_motion_stack.back()).fading; }
void caption(ImVec2 at,const char* text,float scale) {
  char buffer[96]{};
  const size_t length=std::min(std::char_traits<char>::length(text),sizeof(buffer)-1);
  for(size_t i=0;i<length;++i) buffer[i]=static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
  tracked_text(ImGui::GetWindowDrawList(),at,ImGui::GetColorU32(theme::muted),buffer,1.1f*scale);
}
bool begin_group(const char* id,const char* title) {
  const float s=scale();
  if(title && *title) {
    // Section captions sit outside the card, uppercased and letter-spaced. The block has a fixed
    // height so every caption leaves the same gap above and below it.
    const auto at=ImGui::GetCursorScreenPos();
    const float height=theme::caption_height*s;
    caption({at.x,at.y+std::max(0.f,(height-ImGui::GetFontSize())*.5f)},title,s);
    ImGui::Dummy({0,height+theme::caption_gap*s});
  }
  ImGui::PushStyleColor(ImGuiCol_ChildBg,{0,0,0,0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{theme::popup_item_padding*s,theme::section_padding_y*s});
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,theme::section_radius*s);
  const auto flags=(ImGui::GetCurrentWindow()->Flags&ImGuiWindowFlags_NoInputs)==ImGuiWindowFlags_NoInputs ? ImGuiWindowFlags_NoInputs : ImGuiWindowFlags_None;
  const auto child_id=ImGui::GetID(id);
  const bool visible=ImGui::BeginChild(id,{0,0},ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysUseWindowPadding|ImGuiChildFlags_NavFlattened,flags);
  ImGui::PushOverrideID(child_id);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  if(visible) {
    const auto* window=ImGui::GetCurrentWindow();
    const ImVec2 max{window->Pos.x+window->Size.x,window->Pos.y+window->Size.y};
    window->DrawList->AddRectFilled(window->Pos,max,ImGui::GetColorU32(theme::section_surface),theme::section_radius*s);
    auto edge=theme::border; edge.w*=.4f;
    window->DrawList->AddRect(window->Pos,max,ImGui::GetColorU32(edge),theme::section_radius*s);
  }
  return visible;
}
void end_group() { ImGui::PopID(); ImGui::EndChild(); }
namespace {
unsigned packed_color(ImVec4 color) {
  const auto channel=[](float value) { return static_cast<unsigned>(std::clamp(value,0.f,1.f)*255+.5f); };
  return (channel(color.x)<<16)|(channel(color.y)<<8)|channel(color.z);
}
ImVec4 unpacked_color(unsigned color) { return {((color>>16)&255)/255.f,((color>>8)&255)/255.f,(color&255)/255.f,1}; }
void color_hex(char* text,ImVec4 color) { std::snprintf(text,7,"%06X",packed_color(color)); }
}
bool save_color_palette(ColorPalette& palette) {
  if(!palette.writable || !palette.save || !palette.save(palette.owner,palette)) {
    palette.error="Could not save color. Your saved slots were kept.";return false;
  }
  palette.error.clear();return true;
}
namespace {
// Gradient quads cannot round their corners, so the wedges are painted back in.
void round_corners(ImDrawList* draw,ImVec2 min,ImVec2 max,float radius,ImVec4 background) {
  if(radius<=0) return;
  const auto color=ImGui::GetColorU32(background);
  const auto wedge=[&](ImVec2 corner,ImVec2 center,float start,float end) {
    ImVec2 previous{center.x+std::cos(start)*radius,center.y+std::sin(start)*radius};
    for(int i=1;i<=12;++i) {
      const float angle=start+(end-start)*i/12;
      const ImVec2 next{center.x+std::cos(angle)*radius,center.y+std::sin(angle)*radius};
      draw->AddTriangleFilled(corner,previous,next,color);previous=next;
    }
  };
  wedge(min,{min.x+radius,min.y+radius},IM_PI,IM_PI*1.5f);
  wedge({max.x,min.y},{max.x-radius,min.y+radius},IM_PI*1.5f,IM_PI*2);
  wedge(max,{max.x-radius,max.y-radius},0,IM_PI*.5f);
  wedge({min.x,max.y},{min.x+radius,max.y-radius},IM_PI*.5f,IM_PI);
}
// Color a rounded mesh directly, retaining the antialias alpha at its perimeter.
void rounded_gradient(ImDrawList* draw,ImVec2 min,ImVec2 max,float radius,ImVec4 tl,ImVec4 tr,ImVec4 br,ImVec4 bl) {
  const int first=draw->VtxBuffer.Size;
  draw->AddRectFilled(min,max,IM_COL32_WHITE,radius);
  for(int i=first;i<draw->VtxBuffer.Size;++i) {
    auto& vertex=draw->VtxBuffer[i];
    const float x=std::clamp((vertex.pos.x-min.x)/(max.x-min.x),0.f,1.f);
    const float y=std::clamp((vertex.pos.y-min.y)/(max.y-min.y),0.f,1.f);
    auto tint=theme::mix(theme::mix(tl,tr,x),theme::mix(bl,br,x),y);
    tint.w*=((vertex.col>>IM_COL32_A_SHIFT)&255)/255.f;
    vertex.col=ImGui::ColorConvertFloat4ToU32(tint);
  }
}
// Checkerboard rail carrying the current colour's opacity, with a value capsule.
bool alpha_rail(ImVec4& color,float width) {
  const float s=scale(),height=10*s,cell=5*s;
  const float rail_width=width;
  auto* draw=ImGui::GetWindowDrawList();
  const auto p=ImGui::GetCursorScreenPos();
  draw->PushClipRect(p,{p.x+rail_width,p.y+height},true);
  for(int row=0;row<2;++row) for(int column=0;column*cell<rail_width;++column)
    draw->AddRectFilled({p.x+column*cell,p.y+row*cell},{p.x+(column+1)*cell,p.y+(row+1)*cell},
      ((row+column)&1) ? IM_COL32(96,96,103,255) : IM_COL32(150,150,158,255));
  const auto opaque=ImGui::GetColorU32(ImVec4{color.x,color.y,color.z,1});
  const auto transparent=ImGui::GetColorU32(ImVec4{color.x,color.y,color.z,0});
  draw->AddRectFilledMultiColor(p,{p.x+rail_width,p.y+height},transparent,opaque,opaque,transparent);
  draw->PopClipRect();
  ImGui::InvisibleButton("##alpha",{rail_width,height+6*s});
  bool changed=false;
  if(ImGui::IsItemActive()&&ImGui::IsMouseDown(0)) {
    color.w=std::clamp((ImGui::GetIO().MousePos.x-p.x)/rail_width,0.f,1.f);
    changed=true;
  }
  draw->AddRectFilled({p.x+color.w*rail_width-2*s,p.y-2*s},{p.x+color.w*rail_width+2*s,p.y+height+2*s},IM_COL32_WHITE,2*s);
  return changed;
}
bool spectrum(ImVec4& color,float width) {
  const float s=scale();float hue{},sat{},brightness{};
  ImGui::ColorConvertRGBtoHSV(color.x,color.y,color.z,hue,sat,brightness);
  auto* storage=ImGui::GetStateStorage();const auto hue_id=ImGui::GetID("spectrum_hue");
  if(sat==0 || brightness==0)hue=storage->GetFloat(hue_id,hue);
  else storage->SetFloat(hue_id,hue);
  auto* draw=ImGui::GetWindowDrawList();const auto p=ImGui::GetCursorScreenPos();
  const float height=width*.78f;ImVec4 pure{1,1,1,1};ImGui::ColorConvertHSVtoRGB(hue,1,1,pure.x,pure.y,pure.z);
  const ImVec2 end{p.x+width,p.y+height};
  rounded_gradient(draw,p,end,theme::section_radius*s,{1,1,1,1},pure,pure,{1,1,1,1});
  rounded_gradient(draw,p,end,theme::section_radius*s,{0,0,0,0},{0,0,0,0},{0,0,0,1},{0,0,0,1});
  ImGui::InvisibleButton("##spectrum",{width,height});bool changed=false;
  if(ImGui::IsItemActive()&&ImGui::IsMouseDown(0)) {const auto mouse=ImGui::GetIO().MousePos;sat=std::clamp((mouse.x-p.x)/width,0.f,1.f);brightness=1-std::clamp((mouse.y-p.y)/height,0.f,1.f);changed=true;}
  const ImVec2 cursor{p.x+sat*width,p.y+(1-brightness)*height};
  draw->AddCircle(cursor,7*s,IM_COL32_BLACK,32,4*s);
  draw->AddCircle(cursor,7*s,IM_COL32_WHITE,32,2*s);
  ImGui::Dummy({0,5*s});
  const auto rail=ImGui::GetCursorScreenPos();const float rail_height=10*s;
  for(int segment=0;segment<6;++segment) {ImVec4 a{0,0,0,1},b{0,0,0,1};ImGui::ColorConvertHSVtoRGB(segment/6.f,1,1,a.x,a.y,a.z);ImGui::ColorConvertHSVtoRGB((segment+1)/6.f,1,1,b.x,b.y,b.z);draw->AddRectFilledMultiColor({rail.x+width*segment/6,rail.y},{rail.x+width*(segment+1)/6,rail.y+rail_height},ImGui::GetColorU32(a),ImGui::GetColorU32(b),ImGui::GetColorU32(b),ImGui::GetColorU32(a));}
  round_corners(draw,{rail.x,rail.y},{rail.x+width,rail.y+rail_height},rail_height*.5f,ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
  ImGui::InvisibleButton("##hue",{width,rail_height+4*s});
  if(ImGui::IsItemActive()&&ImGui::IsMouseDown(0)){hue=std::clamp((ImGui::GetIO().MousePos.x-rail.x)/width,0.f,.9999f);storage->SetFloat(hue_id,hue);changed=true;}
  draw->AddRectFilled({rail.x+hue*width-2*s,rail.y-2*s},{rail.x+hue*width+2*s,rail.y+rail_height+2*s},IM_COL32_WHITE,2*s);
  if(changed)ImGui::ColorConvertHSVtoRGB(hue,sat,brightness,color.x,color.y,color.z);
  ImGui::Dummy({0,3*s});
  changed|=alpha_rail(color,width);
  return changed;
}
}
bool color_selector(const char* id,const char* label,ImVec4& color,ColorSelectorState& editor,ColorPalette& palette,bool disabled,bool compact) {
  const float s=scale();
  const float trigger_width=32*s;
  Row row(id,label,trigger_width,disabled,compact);
  char value[7]; color_hex(value,color);
  ImGui::PushStyleColor(ImGuiCol_Button,{0,0,0,0});
  const bool clicked=widgets::button("##color_value",{trigger_width,theme::input_height*s});
  ImGui::PopStyleColor();
  editor.trigger={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  auto* draw=ImGui::GetWindowDrawList();
  const auto at=editor.trigger.min;
  const float inset=5*s;
  draw->AddRectFilled({at.x+inset,at.y+inset},{editor.trigger.max.x-inset,editor.trigger.max.y-inset},ImGui::GetColorU32(color),3*s);
  draw->AddRect({at.x+inset,at.y+inset},{editor.trigger.max.x-inset,editor.trigger.max.y-inset},ImGui::GetColorU32(theme::border),3*s);
  if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s  #%s",label,value);
  if(!disabled && (clicked || editor.request_open)) {
    editor.original=color; color_hex(editor.hex,color); editor.error.clear();
    editor.request_open=false; ImGui::OpenPopup("color_picker");
  }
  const auto* parent=ImGui::GetCurrentWindow();
  const Bounds anchor=(parent->Flags&ImGuiWindowFlags_Popup) ? Bounds{parent->Pos,{parent->Pos.x+parent->Size.x,parent->Pos.y+parent->Size.y}} : editor.trigger;
  if(!begin_popup("color_picker",anchor,theme::color_popup_width,theme::color_popup_height,nullptr,true,false,false,true,false,false,{10,10})) return false;
  ImGui::PushStyleColor(ImGuiCol_NavCursor,{0,0,0,0});
  editor.popup={ImGui::GetWindowPos(),{ImGui::GetWindowPos().x+ImGui::GetWindowSize().x,ImGui::GetWindowPos().y+ImGui::GetWindowSize().y}};
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{theme::setting_gap*s,6*s});
  bool changed=false;
  ImGui::SetNextItemWidth(std::min(theme::color_picker_width*s,ImGui::GetContentRegionAvail().x));
  if(spectrum(color,std::min(theme::color_picker_width*s,ImGui::GetContentRegionAvail().x))) { changed=true; editor.error.clear(); }
  ImGui::BeginGroup();
  ImGui::TextColored(theme::secondary,"Current");
  ImGui::ColorButton("##current",color,ImGuiColorEditFlags_NoAlpha,{theme::color_preview*s,theme::color_preview*s});
  ImGui::EndGroup(); ImGui::SameLine(0,theme::inventory_gap*s); ImGui::BeginGroup();
  ImGui::TextColored(theme::secondary,"Original");
  if(ImGui::ColorButton("##original",editor.original,ImGuiColorEditFlags_NoAlpha,{theme::color_preview*s,theme::color_preview*s})) { color=editor.original; changed=true; editor.error.clear(); }
  editor.original_bounds={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  ImGui::EndGroup();
  row_text("Hex"); ImGui::SameLine();
  ImGui::SetNextItemWidth(theme::color_hex_width*s);
  const bool entered=widgets::input_text("##color_hex",nullptr,editor.hex,sizeof(editor.hex),ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_AutoSelectAll);
  editor.hex_bounds={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  const bool editing=ImGui::IsItemActive();
  if(entered) {
    unsigned hex{}; const auto length=std::char_traits<char>::length(editor.hex);
    const auto parsed=std::from_chars(editor.hex,editor.hex+length,hex,16);
    if(length==6 && parsed.ec==std::errc{}) { const float alpha=color.w;color=unpacked_color(hex);color.w=alpha;changed=true; editor.error.clear(); }
    else editor.error="Enter six hex digits.";
  }
  ImGui::TextColored(theme::secondary,"Saved colors");
  editor.slots.resize(palette.slots.size());
  size_t visible=0;
  for(size_t i=0;i<palette.slots.size();++i) {
    if(!palette.slots[i])continue;
    if(visible++%7)ImGui::SameLine(0,theme::color_slot_gap*s);
    ImGui::PushID(static_cast<int>(i)+4);
    if(ImGui::ColorButton("##saved",*palette.slots[i],ImGuiColorEditFlags_NoAlpha,{theme::color_slot*s,theme::color_slot*s})) {color=*palette.slots[i];changed=true;}
    editor.slots[i]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
    if(ImGui::IsItemClicked(ImGuiMouseButton_Right))ImGui::OpenPopup("delete_color");
    if(begin_popup("delete_color",editor.slots[i],200,110,nullptr,true)) {
      ImGui::TextUnformatted("Delete saved color?");
      if(button("Cancel"))ImGui::CloseCurrentPopup();
      ImGui::SameLine();
      if(button("Delete")) {
        const auto previous=palette.slots;palette.slots[i].reset();
        if(!save_color_palette(palette))palette.slots=previous;
        ImGui::CloseCurrentPopup();
      }
      end_popup();
    }
    ImGui::PopID();
  }
  // One append action; saved colors are persistent menu preferences.
  if(visible%7)ImGui::SameLine(0,theme::color_slot_gap*s);
  ImGui::BeginDisabled(!palette.writable || !palette.save || visible>=256);
  const bool append=button("###save_color",{theme::color_slot*s,theme::color_slot*s});
  const auto add_min=ImGui::GetItemRectMin(),add_max=ImGui::GetItemRectMax();
  icon_text(ImGui::GetWindowDrawList(),{(add_min.x+add_max.x)*.5f,(add_min.y+add_max.y)*.5f},ImGui::GetColorU32(theme::text),"+");
  if(append) {
    const auto previous=palette.slots;
    palette.slots.erase(std::remove(palette.slots.begin(),palette.slots.end(),std::nullopt),palette.slots.end());
    palette.slots.push_back(color);
    if(!save_color_palette(palette))palette.slots=previous;
  }
  editor.save={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  if(ImGui::IsItemHovered())ImGui::SetTooltip("Save selected color");
  ImGui::EndDisabled();
  if(!editor.error.empty()) ImGui::TextWrapped("%s",editor.error.c_str());
  if(!palette.error.empty()) ImGui::TextWrapped("%s",palette.error.c_str());
  if(!editing) color_hex(editor.hex,color);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  end_popup();
  return changed;
}
bool toggle(const char* id,const char* label,bool& value,bool disabled,const PopupContents* settings,bool hotkey_enabled,bool compact,ImGuiID binding) {
  const float s=scale();
  const float controls=(theme::switch_width+(settings ? theme::more_width+theme::setting_gap : 0))*s;
  Row row(id,label,controls,disabled,compact,binding);
  const auto hotkey_id=binding ? binding : ImGui::GetID("hotkeys");
  const bool bound=hotkey_enabled && hotkeys::evaluate(hotkey_id,value,disabled);

  settings_action(settings);
  const auto p=ImGui::GetCursorScreenPos();
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0,0,0,0});
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,{0,0,0,0});
  ImGui::PushStyleColor(ImGuiCol_NavCursor,{0,0,0,0});
  const bool clicked=widgets::button("##switch",{theme::switch_width*s,theme::input_height*s});
  ImGui::PopStyleColor(3);
  if(clicked) value=!value;
  auto* storage=ImGui::GetStateStorage();
  const auto motion_key=ImGui::GetID("switch_motion");
  const float target=value ? 1.f : 0.f;
  float position=storage->GetFloat(motion_key,target);
  if(!animate_widgets || disabled) position=target;
  else position+=(target-position)*(1-std::exp(-3*ImGui::GetIO().DeltaTime/theme::switch_seconds));
  if(std::abs(position-target)<.001f) position=target;
  storage->SetFloat(motion_key,position);
  auto* draw=ImGui::GetWindowDrawList();
  const float inset=(theme::input_height-theme::switch_height)*s*.5f;
  const ImVec2 start{p.x,p.y+inset},end{p.x+theme::switch_width*s,p.y+inset+theme::switch_height*s};
  const auto off=ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride) && !disabled ? theme::mix(theme::track,theme::text,.06f) : theme::track;
  const ImVec4 color{off.x+(theme::accent.x-off.x)*position,off.y+(theme::accent.y-off.y)*position,
    off.z+(theme::accent.z-off.z)*position,off.w+(theme::accent.w-off.w)*position};
  draw->AddRectFilled(start,end,ImGui::GetColorU32(color),theme::switch_height*s*.5f);
  const float knob=theme::switch_height-4;
  const float knob_x=p.x+(2+(theme::switch_width-theme::switch_height)*position)*s;
  const ImVec2 thumb{knob_x+knob*s*.5f,start.y+theme::switch_height*s*.5f};
  draw->AddCircleFilled({thumb.x,thumb.y+1*s},knob*s*.5f,IM_COL32(0,0,0,48),24);
  draw->AddCircleFilled(thumb,knob*s*.5f,ImGui::GetColorU32(ImVec4{1,1,1,position*.06f+.94f}),24);
  row_hotkeys(row,hotkeys::Kind::Boolean,id,hotkey_enabled,binding);
  return clicked || bound;
}
bool toggle_more(const char* id,const char* label,bool& value,const PopupContents& settings,bool disabled,bool hotkey_enabled) {
  return toggle(id,label,value,disabled,&settings,hotkey_enabled);
}
namespace {
template<class T> bool slider_value(const char* id,const char* label,T& value,T min,T max,T step,const char* format,bool disabled,bool hotkey_enabled,bool compact,bool stacked=false,SliderEdit* edit=nullptr,ImGuiID binding_id=0,float display_scale=1) {
  const float s=scale();
  const float controls=std::min(theme::setting_control*s,ImGui::GetContentRegionAvail().x);
  const bool valid=min<max && std::isfinite(static_cast<double>(min)) && std::isfinite(static_cast<double>(max));
  // The capsule is sized for the widest number this row can show, so it never clips a value
  // like "1000 ms" and never steals width the track can use.
  const auto formatted_width=[&](T sample) {
    char text[96]{};
    std::snprintf(text,sizeof(text),format ? format : (std::is_same_v<T,int> ? "%d" : "%.1f"),static_cast<T>(sample*display_scale));
    return ImGui::CalcTextSize(text).x;
  };
  const float value_box=std::clamp(std::max(formatted_width(min),formatted_width(max))+2*theme::value_padding*s,
                                   theme::value_width*s,theme::value_width_max*s);
  Row row(id,label,stacked ? value_box : controls,disabled || !valid,compact,binding_id);
  if(stacked) { row.height=theme::slider_stacked_height*s; ImGui::SetCursorScreenPos({row.origin.x,row.origin.y+theme::row_height*s}); }
  const auto hotkey_id=binding_id ? binding_id : ImGui::GetID("hotkeys");
  auto& binding=hotkeys::option(hotkey_id);
  binding.kind=hotkeys::Kind::Number; binding.minimum=min; binding.maximum=max; binding.step=step;
  double bound_value=value;
  const bool bound=hotkey_enabled && hotkeys::evaluate(hotkey_id,bound_value,disabled || !valid);
  if(bound) value=static_cast<T>(bound_value);
  const auto p=ImGui::GetCursorScreenPos();
  const float track_width=stacked ? row.width : std::max(1.f,std::min(controls,row.width)-value_box-theme::setting_gap*s);
  T edited=std::isfinite(static_cast<double>(value)) && valid ? std::clamp(value,min,max) : min;
  const T previous=edited;
  ImGui::SetNextItemWidth(track_width);
  ImGui::PushStyleColor(ImGuiCol_FrameBg,{0,0,0,0});
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,{0,0,0,0});
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive,{0,0,0,0});
  ImGui::PushStyleColor(ImGuiCol_SliderGrab,{0,0,0,0});
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,{0,0,0,0});
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{theme::input_padding*s,((stacked ? theme::slider_stacked_height-theme::row_height : theme::input_height)*s-ImGui::GetFontSize())*.5f});
  ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,theme::slider_radius*2*s);
  bool changed=false;
  if(valid) {
    if constexpr(std::is_same_v<T,int>) changed=ImGui::SliderInt("##track",&edited,min,max,"",ImGuiSliderFlags_AlwaysClamp|ImGuiSliderFlags_NoInput);
    else changed=ImGui::SliderFloat("##track",&edited,min,max,"",ImGuiSliderFlags_AlwaysClamp|ImGuiSliderFlags_NoInput);
  } else widgets::button("##track",{track_width,theme::input_height*s});
  if(edit) { edit->active=ImGui::IsItemActive(); edit->finished=ImGui::IsItemDeactivatedAfterEdit(); edit->track=last_bounds(); }
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(5);
  if(changed && step>0) {
    if(ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) edited=static_cast<T>(std::max(static_cast<double>(min),static_cast<double>(previous)-step));
    else if(ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) edited=static_cast<T>(std::min(static_cast<double>(max),static_cast<double>(previous)+step));
    else edited=static_cast<T>(std::clamp(static_cast<double>(min)+std::round((static_cast<double>(edited)-min)/step)*step,static_cast<double>(min),static_cast<double>(max)));
  }
  if(changed) { changed=value!=edited; value=edited; }
  auto* draw=ImGui::GetWindowDrawList();
  const auto bounds=last_bounds();
  const float y=(bounds.min.y+bounds.max.y)*.5f;
  const float left=p.x+theme::slider_track_inset*s,right=p.x+track_width-theme::slider_track_inset*s;
  const float radius=std::min(theme::slider_thumb*s,std::max(0.f,(right-left)*.5f));
  const float fraction=valid ? static_cast<float>((static_cast<double>(edited)-min)/(static_cast<double>(max)-min)) : 0;
  const float x=left+radius+std::max(0.f,right-left-2*radius)*fraction;
  const bool focused=ImGui::IsItemActive() || ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride);
  draw->AddLine({left,y},{right,y},ImGui::GetColorU32(theme::rail),theme::slider_track*s);
  draw->AddLine({left,y},{x,y},ImGui::GetColorU32(focused ? theme::mix(theme::accent,theme::text,.12f) : theme::accent),theme::slider_track*s);
  draw->AddCircleFilled({x,y+1*s},radius,IM_COL32(0,0,0,56),32);
  draw->AddCircleFilled({x,y},radius,IM_COL32_WHITE,32);
  if(stacked) ImGui::SetCursorScreenPos({row.origin.x+row.width-value_box,row.origin.y+(theme::row_height-theme::value_height)*s*.5f});
  else ImGui::SameLine(0,theme::setting_gap*s);
  const auto box=ImGui::GetCursorScreenPos();
  if(!stacked) ImGui::SetCursorScreenPos({box.x,box.y+(bounds.max.y-bounds.min.y-theme::value_height*s)*.5f});
  ImGui::SetNextItemWidth(value_box);
  char number_text[96]{};
  std::snprintf(number_text,sizeof(number_text),format ? format : (std::is_same_v<T,int> ? "%d" : "%.1f"),static_cast<T>(value*display_scale));
  const ImVec2 field_min=ImGui::GetCursorScreenPos(),field_max{field_min.x+value_box,field_min.y+theme::value_height*s};
  const auto field_id=ImGui::GetID("##number");
  const auto* context=ImGui::GetCurrentContext();
  const bool editing=context->ActiveId==field_id || context->NavActivateId==field_id ||
    (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(field_min,field_max) && ImGui::IsMouseClicked(0));
  const float padding_x=editing ? theme::value_padding*s : std::max(theme::value_padding*s,(value_box-ImGui::CalcTextSize(number_text).x)*.5f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{padding_x,std::max(0.f,(theme::value_height*s-ImGui::GetFontSize())*.5f)});
  ImGui::PushStyleColor(ImGuiCol_FrameBg,editing ? theme::raised : theme::surface);
  T manual=static_cast<T>(value*display_scale);
  const bool input=input_scalar("##number",std::is_same_v<T,int> ? ImGuiDataType_S32 : ImGuiDataType_Float,&manual,format ? format : (std::is_same_v<T,int> ? "%d" : "%.1f"),theme::value_height*s,padding_x);
  if(edit) { edit->finished|=input; edit->value=last_bounds(); }
  ImGui::PopStyleColor(); ImGui::PopStyleVar();
  if(input && valid && std::isfinite(static_cast<double>(manual))) {
    double result=std::clamp(static_cast<double>(manual)/display_scale,static_cast<double>(min),static_cast<double>(max));
    if(step>0) result=std::clamp(static_cast<double>(min)+std::round((result-min)/step)*step,static_cast<double>(min),static_cast<double>(max));
    const auto next=static_cast<T>(result);
    changed|=value!=next; value=next;
  }
  row_hotkeys(row,hotkeys::Kind::Number,id,hotkey_enabled,binding_id);
  return changed || bound;
}
}
bool slider(const char* id,const char* label,float& value,float min,float max,float step,const char* format,bool disabled,bool hotkey_enabled,bool compact,bool stacked,SliderEdit* edit,ImGuiID binding) {
  return slider_value(id,label,value,min,max,step,format,disabled,hotkey_enabled,compact,stacked,edit,binding);
}
bool percent(const char* id,const char* label,float& value,bool compact,ImGuiID binding) {
  return slider_value(id,label,value,0.f,1.f,0.f,"%.0f%%",false,true,compact,false,nullptr,binding,100.f);
}
bool slider(const char* id,const char* label,int& value,int min,int max,int step,const char* format,bool disabled,bool hotkey_enabled,bool compact,ImGuiID binding) {
  return slider_value(id,label,value,min,max,step,format,disabled,hotkey_enabled,compact,false,nullptr,binding);
}
bool combo(const char* id,const char* label,int& selected,std::span<const Option> options,bool disabled,const PopupContents* settings,bool hotkey_enabled,bool compact,ImGuiID binding_id) {
  const float s=scale();
  const float extra=(settings ? theme::more_width+theme::setting_gap : 0)*s;
  const float control_width=std::max(28*s,std::min(theme::setting_control*s,ImGui::GetContentRegionAvail().x-extra));
  Row row(id,label,control_width+extra,disabled || options.empty(),compact,binding_id);
  const auto hotkey_id=binding_id ? binding_id : ImGui::GetID("hotkeys");
  auto& binding=hotkeys::option(hotkey_id); binding.kind=hotkeys::Kind::Selection; binding.minimum=0; binding.maximum=std::max(0,static_cast<int>(options.size())-1); binding.choices.clear(); binding.choice_disabled.clear();
  for(const auto& item : options) { binding.choices.emplace_back(item.label); binding.choice_disabled.push_back(item.disabled); }
  double bound_value=selected;
  const bool bound=hotkey_enabled && hotkeys::evaluate(hotkey_id,bound_value,disabled || options.empty());
  if(bound) selected=static_cast<int>(bound_value);
  settings_action(settings);
  ImGui::PushStyleColor(ImGuiCol_Button,theme::raised);
  const bool clicked=widgets::button("##value",{control_width,theme::input_height*s},ImGuiButtonFlags_NoHoveredOnFocus);
  ImGui::PopStyleColor();
  const auto anchor=last_bounds();
  const char* current=selected>=0 && selected<static_cast<int>(options.size()) ? options[selected].label : "Select";
  field_text(current,anchor,disabled);
  if(clicked) ImGui::OpenPopup("options");
  bool changed=false;
  const float height=theme::popup_padding*2+std::min(static_cast<float>(options.size()),theme::popup_rows)*theme::option_height;
  if(begin_popup("options",anchor,dropdown_width(anchor),height,nullptr,false,true,false,false)) {
    for(size_t i=0;i<options.size();++i) {
      if(option_item(static_cast<int>(i),options[i],selected==static_cast<int>(i),false)) {
        changed=selected!=static_cast<int>(i); selected=static_cast<int>(i);
      }
    }
    end_popup();
  }
  row_hotkeys(row,hotkeys::Kind::Selection,id,hotkey_enabled,binding_id);
  return changed || bound;
}
bool combo_more(const char* id,const char* label,int& selected,std::span<const Option> options,const PopupContents& settings,bool disabled,bool hotkey_enabled) {
  return combo(id,label,selected,options,disabled,&settings,hotkey_enabled);
}
bool multi_combo(const char* id,const char* label,std::span<bool> selected,std::span<const Option> options,const char* title,bool disabled,SelectionSummary summary,bool hotkey_enabled,ImGuiID binding_id) {
  const float s=scale();
  const bool valid=selected.size()==options.size() && !options.empty();
  const float control_width=std::min(theme::setting_control*s,ImGui::GetContentRegionAvail().x);
  Row row(id,label,control_width,disabled || !valid,false,binding_id);
  const auto hotkey_id=binding_id ? binding_id : ImGui::GetID("hotkeys");
  auto& binding=hotkeys::option(hotkey_id); binding.kind=hotkeys::Kind::Multi; binding.choices.clear(); binding.choice_disabled.clear();
  for(const auto& item : options) { binding.choices.emplace_back(item.label); binding.choice_disabled.push_back(item.disabled); }
  const bool bound=hotkey_enabled && hotkeys::evaluate(hotkey_id,selected,disabled || !valid);
  bool changed=false;
  ImGui::PushStyleColor(ImGuiCol_Button,theme::raised);
  const bool clicked=widgets::button("##value",{control_width,theme::input_height*s},ImGuiButtonFlags_NoHoveredOnFocus);
  ImGui::PopStyleColor();
  const auto anchor=last_bounds();
  const auto count=std::count(selected.begin(),selected.end(),true);
  const std::string text=summary && valid ? summary(selected,options) : count ? std::to_string(count)+" selected" : "Select";
  field_text(text.c_str(),anchor,disabled || !valid);
  if(clicked) ImGui::OpenPopup("options");
  const float height=theme::popup_padding*2+std::min(static_cast<float>(options.size()),theme::popup_rows)*theme::option_height;
  if(begin_popup("options",anchor,dropdown_width(anchor),height,nullptr,false,true,false,false)) {
    if(valid) for(size_t i=0;i<options.size();++i) {
      if(option_item(static_cast<int>(i),options[i],selected[i],true)) { selected[i]=!selected[i]; changed=true; }
    }
    end_popup();
  }
  row_hotkeys(row,hotkeys::Kind::Multi,id,hotkey_enabled,binding_id);
  return changed || bound;
}
}
