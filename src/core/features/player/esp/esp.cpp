#include "core/features/player/esp/drawing.hpp"
#include "core/features/player/esp/elements.hpp"
#include "core/features/player/esp/pipeline.hpp"
#include "core/features/combat/settings.hpp"
#include <Windows.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace iris::visuals {
using namespace esp;
bool draw(Icon (*lookup)(void*,int),void* owner) {
    const auto current=snapshot.load();
    if(!current)return false;
    DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);if(pid!=GetCurrentProcessId())return false;
    const auto ready=esp::prepared();const auto size=ImGui::GetIO().DisplaySize;
    if(!ready||!ready->players||ready->width!=size.x||ready->height!=size.y||ready->players->settings!=current->settings)return false;
    return draw_snapshot(*ready->players,GetTickCount64(),lookup,owner,ready->bounds);
}
bool draw_wall_target(const WallTarget& frame,std::uint64_t now,const std::array<float,4>& color) {
    if(now<frame.sampled || now-frame.sampled>150 || frame.damage<=0 || frame.region<0 || frame.region>=6)return false;
    const auto size=ImGui::GetIO().DisplaySize;auto* d=ImGui::GetBackgroundDrawList();
    std::array<ImVec2,8> points{};
    for(int i=0;i<8;++i){const auto p=project(frame.matrix,frame.corners[i],size.x,size.y);if(!p)return false;points[i]=point(*p);}
    const ImVec4 tint{color[0],color[1],color[2],std::max(.65f,color[3])};
    const auto line=ImGui::ColorConvertFloat4ToU32(tint),fill=ImGui::ColorConvertFloat4ToU32({tint.x,tint.y,tint.z,.16f});
    constexpr int faces[][4]{{0,1,3,2},{4,5,7,6},{0,1,5,4},{2,3,7,6},{0,2,6,4},{1,3,7,5}};
    for(const auto& face:faces)d->AddQuadFilled(points[face[0]],points[face[1]],points[face[2]],points[face[3]],fill);
    for(int i=0;i<8;++i)for(int bit=1;bit<=4;bit*=2)if(!(i&bit))d->AddLine(points[i],points[i|bit],line,1.5f);
    const auto center=project(frame.matrix,frame.point,size.x,size.y);
    if(center){char label[64]{};std::snprintf(label,sizeof(label),"%s Â· %.0f HP",nospread::hitbox_names[frame.region],frame.damage);text(d,{center->x,center->y+8},line,label,true);}
    return true;
}
bool draw_snapshot(const Snapshot& frame,std::uint64_t now,Icon (*lookup)(void*,int),void* owner,std::span<const std::optional<ScreenBounds>> prepared) {
    const auto* current=&frame;
    if(!any_esp_enabled(current->settings)||now<current->sampled||now-current->sampled>250)return false;
    const auto size=ImGui::GetIO().DisplaySize;const auto& settings=current->settings;
    auto* d=ImGui::GetBackgroundDrawList();const auto initial=d->VtxBuffer.Size;const float font=ImGui::GetFontSize();
    bool drawn{};
    for(std::size_t index=0;index<current->players.size();++index) {
        const auto& p=current->players[index];
        const auto c=for_target(settings,p.enemy);if(!c.enabled)continue;
        const bool overlay=p.distance<=c.max_distance&&(!c.visible_only||p.visible);
        const auto& rgba=p.enemy?(p.visible?c.enemy_color:c.hidden_color):c.ally_color;
        const auto element=[&](const std::array<float,4>& value){return esp::rgba(tint(rgba,value));};
        if(overlay&&c.sounds&&p.sound_age>=0){sound(d,p,c,*current,now,size,tint(rgba,c.sound_color));drawn=true;}
        const auto bounds=prepared.size()==current->players.size()?prepared[index]:player_bounds(current->matrix,p,size.x,size.y);
        if(!bounds) {
            if(!overlay||!c.arrows||!p.enemy)continue;
            const auto& m=current->matrix;const auto v=p.origin;
            float x=m[0][0]*v.x+m[0][1]*v.y+m[0][2]*v.z+m[0][3],y=-(m[1][0]*v.x+m[1][1]*v.y+m[1][2]*v.z+m[1][3]);
            const float n=std::hypot(x,y);if(!std::isfinite(n))continue;if(n<.001f){x=0;y=1;}else{x/=n;y/=n;}
            const float margin=c.arrow_size*2+font;
            const float reach=std::min((size.x*.5f-margin)/std::max(std::abs(x),.001f),(size.y*.5f-margin)/std::max(std::abs(y),.001f));if(reach<=0)continue;
            const ImVec2 tip{size.x*.5f+x*reach,size.y*.5f+y*reach};
            const ImVec2 a{tip.x-x*c.arrow_size-y*c.arrow_size*.6f,tip.y-y*c.arrow_size+x*c.arrow_size*.6f},b{tip.x-x*c.arrow_size+y*c.arrow_size*.6f,tip.y-y*c.arrow_size-x*c.arrow_size*.6f};
            d->AddTriangleFilled(tip,a,b,esp::rgba(c.arrow_color));d->AddTriangle(tip,a,b,IM_COL32(0,0,0,220));drawn=true;continue;
        }
        const ImVec2 min=point(bounds->minimum),max=point(bounds->maximum);
        if(max.x-min.x<.5f||max.y-min.y<.5f)continue;
        const float middle=(min.x+max.x)*.5f;
        box(d,p,c,min,max);
        if(!overlay){drawn=true;continue;}
        draw_elements(d,p,c,min,max,lookup,owner);
        if(c.snapline)d->AddLine({size.x*.5f,size.y-1},{middle,max.y},element(c.snapline_color),c.thickness);
        hitboxes(d,p,c,current->matrix,size,element(c.hitbox_color));
        bones(d,p,c,current->matrix,size,element(c.skeleton_color));
        drawn=true;
    }
    return drawn&&d->VtxBuffer.Size>initial;
}
}
