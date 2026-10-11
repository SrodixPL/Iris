#include "core/menu/esp_preview.hpp"
#include "core/menu/widgets.hpp"
#include "core/menu/player_model.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cfloat>
#include <imgui_internal.h>

namespace iris::ui::esp_preview {
namespace {
constexpr std::array labels{"Box","Name","Health bar","Health number","Ammo bar","Armor bar","Weapon","Distance","Flags","Snapline","Skeleton","Hitboxes","Off-screen arrows","Sound pulses"};
constexpr std::array elements{Element::Name,Element::Health,Element::HealthNumber,Element::Ammo,Element::Armor,Element::Weapon,Element::Distance,Element::Flags};

bool enabled(Element e,const visuals::Settings& c) {
    switch(e) {
    case Element::Box:return c.box;case Element::Name:return c.name;case Element::Health:return c.health;
    case Element::HealthNumber:return c.health_number;case Element::Ammo:return c.ammo;case Element::Armor:return c.armor;
    case Element::Weapon:return c.weapon;case Element::Distance:return c.distance;case Element::Flags:return std::any_of(c.flags.begin(),c.flags.end(),[](bool on){return on;});
    case Element::Snapline:return c.snapline;case Element::Skeleton:return c.skeleton;case Element::Hitboxes:return c.hitboxes;
    case Element::Arrows:return c.arrows;case Element::Sounds:return c.sounds;default:return false;
    }
}

}
const char* label(Element element) {const auto index=static_cast<std::size_t>(element);return index<labels.size()?labels[index]:"Element";}
bool draw(MenuRuntime& menu,const visuals::Settings& input,State& state,visuals::EspLayout* updated_layout,const ModelFrame* model) {
    auto c=input;bool changed{};ImGui::PushID("esp_preview");

    const auto start=ImGui::GetCursorScreenPos();const float s=menu.scale,width=ImGui::GetContentRegionAvail().x,height=std::max(310*s,ImGui::GetContentRegionAvail().y-30*s);
    const ImVec2 end{start.x+width,start.y+height};auto* d=ImGui::GetWindowDrawList();
    d->AddRectFilled(start,end,ImGui::GetColorU32(theme::section_surface),theme::section_radius*s);
    d->PushClipRect(start,end,true);
    const bool live=model&&model->texture;
    const auto at=[&](ImVec2 uv){return ImVec2{start.x+uv.x*width,start.y+uv.y*height};};
    ImVec2 lo=at(live?model->bounds_min:ImVec2{.28f,.18f}),hi=at(live?model->bounds_max:ImVec2{.72f,.82f});
    if(live)d->AddImage(ImTextureRef(model->texture),start,end);
    visuals::Player player=live?model->player:visuals::Player{};
    if(!live){player.name="Player";player.weapon="AK-47";player.distance=30;player.health=72;player.armor=65;player.clip=18;player.max_clip=30;player.money=4200;player.helmet=true;player.flags={false,true,false,true,false,false,true,false,true};}
    player.enemy=state.target!=2;player.visible=state.target!=1;
    if(!live)c.enabled=true;
    const bool overlay=c.enabled&&player.distance<=c.max_distance&&(!c.visible_only||player.visible);
    c.box=c.enabled&&c.box&&player.distance<=c.box_max_distance&&(!c.box_visible_only||player.visible);
    if(!overlay){c.name=c.health=c.health_number=c.ammo=c.armor=c.weapon=c.distance=c.snapline=c.skeleton=c.hitboxes=c.arrows=c.sounds=false;c.flags.fill(false);}
    if(!live)for(int pass=0;pass<3;++pass){
        ImVec2 a=lo,b=hi;const auto measured=visuals::esp::draw_elements(nullptr,player,c,lo,hi);
        for(const auto& rect:measured)if(rect.drawn){a.x=std::min(a.x,rect.min.x);a.y=std::min(a.y,rect.min.y);b.x=std::max(b.x,rect.max.x);b.y=std::max(b.y,rect.max.y);}
        const float left=lo.x-a.x,right=b.x-hi.x,top=lo.y-a.y,bottom=b.y-hi.y;
        const float fit=std::min({1.f,(width-24*s-left-right)/(hi.x-lo.x),(height-40*s-top-bottom)/(hi.y-lo.y)});
        const float w=(hi.x-lo.x)*std::max(.3f,fit),h=(hi.y-lo.y)*std::max(.3f,fit);
        lo={start.x+(width-w-left-right)*.5f+left,start.y+(height-h-top-bottom)*.5f+top};hi={lo.x+w,lo.y+h};
    }
    const auto local_matrix=live?visuals::Matrix{}:player_model::draw(d,lo,hi,state.yaw,state.pitch,player,state.model_cache);
    state.model_bounds={lo,hi};
    visuals::esp::box(d,player,c,lo,hi);
    const auto rects=visuals::esp::draw_elements(d,player,c,lo,hi,live?model->lookup:nullptr,live?model->icon_owner:nullptr);
    const auto hit=[&](Element element,ImVec2 a,ImVec2 b,const char* part,std::size_t layout_id=elements.size()) {
        if(!enabled(element,c))return;
        const auto cursor=ImGui::GetCursorScreenPos();ImGui::SetCursorScreenPos(a);ImGui::PushID(static_cast<int>(element));
        const bool clicked=ImGui::InvisibleButton(part,{std::max(1.f,b.x-a.x),std::max(1.f,b.y-a.y)});
        const bool hovered=ImGui::IsItemHovered();
        if(updated_layout&&layout_id<elements.size()&&ImGui::IsItemActive()&&ImGui::IsMouseDragging(0)&&!widgets::popup_closing()){
            if(state.dragging<0){state.dragging=static_cast<int>(layout_id);const auto mouse=ImGui::GetIO().MouseClickedPos[0];state.drag_offset={mouse.x-(a.x+b.x)*.5f,mouse.y-(a.y+b.y)*.5f};}
            state.selected=element;
        }
        if((clicked&&state.dragging<0)||ImGui::IsItemClicked(ImGuiMouseButton_Right)){state.selected=element;state.options_open=true;state.element_options=true;state.options_anchor=element==Element::Box?Bounds{lo,hi}:Bounds{a,b};}
        if(!widgets::popup_closing()&&hovered){
            if(element==Element::Box)d->AddRect(lo,hi,ImGui::GetColorU32(theme::accent),0,0,2*s);
            else d->AddRect(a,b,ImGui::GetColorU32(theme::accent),theme::control_radius*s);
            ImGui::SetMouseCursor(layout_id<elements.size()?ImGuiMouseCursor_ResizeAll:ImGuiMouseCursor_Hand);
            ImGui::SetTooltip(layout_id<elements.size()?"Drag to reposition; click to edit %s":"Edit %s",label(element));
        }
        ImGui::PopID();ImGui::SetCursorScreenPos(cursor);
    };
    for(std::size_t i=0;i<rects.size();++i)if(rects[i].drawn){const auto& r=rects[i];hit(elements[i],{r.min.x-2*s,r.min.y-2*s},{r.max.x+2*s,r.max.y+2*s},"element",i);}
    hit(Element::Box,{lo.x-3*s,lo.y-3*s},{hi.x+3*s,lo.y+3*s},"top");
    hit(Element::Box,{lo.x-3*s,hi.y-3*s},{hi.x+3*s,hi.y+3*s},"bottom");
    hit(Element::Box,{lo.x-3*s,lo.y+3*s},{lo.x+3*s,hi.y-3*s},"left");
    hit(Element::Box,{hi.x-3*s,lo.y+3*s},{hi.x+3*s,hi.y-3*s},"right");
    {
        const auto& base=player.enemy?(player.visible?c.enemy_color:c.hidden_color):c.ally_color;
        const auto tint=[&](const auto& value){return visuals::esp::rgba(visuals::tint(base,value));};
        visuals::esp::bones(d,player,c,live?model->matrix:local_matrix,live?ImVec2{width,height}:ImVec2{hi.x-lo.x,hi.y-lo.y},tint(c.skeleton_color),live?start:lo);
        visuals::esp::hitboxes(d,player,c,live?model->matrix:local_matrix,live?ImVec2{width,height}:ImVec2{hi.x-lo.x,hi.y-lo.y},tint(c.hitbox_color),live?start:lo);
        if(c.snapline)d->AddLine({start.x+width*.5f,end.y-1},{(lo.x+hi.x)*.5f,hi.y},tint(c.snapline_color),c.thickness);
        const auto cursor=ImGui::GetCursorScreenPos();ImGui::SetCursorScreenPos({lo.x+4*s,lo.y+4*s});ImGui::InvisibleButton("rotate_model",{std::max(1.f,hi.x-lo.x-8*s),std::max(1.f,hi.y-lo.y-8*s)});
        state.rotating=state.dragging<0&&!widgets::popup_closing()&&ImGui::IsItemActive()&&ImGui::IsMouseDragging(0);
        if(state.rotating){const auto delta=ImGui::GetIO().MouseDelta;state.yaw=std::remainder(state.yaw+delta.x*.6f,360.f);state.pitch=std::clamp(state.pitch+delta.y*.4f,-30.f,30.f);}
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Drag player to rotate");ImGui::SetCursorScreenPos(cursor);
    }
    if(state.dragging>=0){
        if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)||!updated_layout||(ImGui::GetCurrentContext()->CurrentItemFlags&ImGuiItemFlags_Disabled)){state.dragging=-1;}
        else {
            const auto id=static_cast<std::size_t>(state.dragging);const auto mouse=ImGui::GetMousePos();const ImVec2 center{mouse.x-state.drag_offset.x,mouse.y-state.drag_offset.y};
            const bool bar=elements[id]==Element::Health||elements[id]==Element::Ammo||elements[id]==Element::Armor;
            float best=FLT_MAX;visuals::EspSide side{};visuals::EspAnchor anchor{};ImVec2 target{};
            for(unsigned candidate=0;candidate<4;++candidate){if(elements[id]==Element::Flags&&candidate<2)continue;
                for(unsigned align=1;align<=3;++align){if(bar&&align!=2)continue;
                    const float factor=(align-1)*.5f;const ImVec2 point=candidate<2?ImVec2{lo.x+(hi.x-lo.x)*factor,candidate==0?lo.y:hi.y}:ImVec2{candidate==2?lo.x:hi.x,lo.y+(hi.y-lo.y)*factor};
                    const float distance=(center.x-point.x)*(center.x-point.x)+(center.y-point.y)*(center.y-point.y);
                    if(distance<best){best=distance;side=static_cast<visuals::EspSide>(candidate);anchor=static_cast<visuals::EspAnchor>(align);target=point;}
                }
            }
            d->AddCircleFilled(target,4*s,ImGui::GetColorU32(theme::accent));
            const char* text=label(elements[id]);const auto size=ImGui::CalcTextSize(text);const ImVec2 ghost{center.x-size.x*.5f,center.y-size.y*.5f};
            d->AddRectFilled({ghost.x-6*s,ghost.y-4*s},{ghost.x+size.x+6*s,ghost.y+size.y+4*s},ImGui::GetColorU32(theme::raised),theme::control_radius*s);d->AddText(ghost,ImGui::GetColorU32(theme::accent),text);
            if(ImGui::IsMouseReleased(0)){
                unsigned order{};for(std::size_t other=0;other<input.esp_layout.size();++other)if(other!=id&&input.esp_layout[other].side==side)++order;
                auto next=input.esp_layout;visuals::move_esp_element(next,static_cast<visuals::EspElement>(id),side,order);next[id].anchor=bar?visuals::EspAnchor::Automatic:anchor;
                if(next!=input.esp_layout){*updated_layout=next;changed=true;}state.dragging=-1;
            }else if(!ImGui::IsMouseDown(0))state.dragging=-1;
        }
    }
    d->PopClipRect();ImGui::SetCursorScreenPos(start);ImGui::Dummy({width,height});ImGui::PopID();return changed;
}
}
