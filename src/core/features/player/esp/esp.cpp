#include "core/features/player/esp/drawing.hpp"
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
    return draw_snapshot(*current,GetTickCount64(),lookup,owner);
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
    if(center){char label[64]{};std::snprintf(label,sizeof(label),"%s · %.0f HP",nospread::hitbox_names[frame.region],frame.damage);text(d,{center->x,center->y+8},line,label,true);}
    return true;
}
bool draw_snapshot(const Snapshot& frame,std::uint64_t now,Icon (*lookup)(void*,int),void* owner) {
    const auto* current=&frame;
    if(!current->settings.enabled||now<current->sampled||now-current->sampled>250)return false;
    const auto size=ImGui::GetIO().DisplaySize;const auto& c=current->settings;
    auto* d=ImGui::GetBackgroundDrawList();const auto initial=d->VtxBuffer.Size;const float font=ImGui::GetFontSize();
    bool drawn{};
    for(const auto& p:current->players) {
        const bool overlay=p.distance<=c.max_distance&&(!c.visible_only||p.visible);
        const auto& rgba=p.enemy?(p.visible?c.enemy_color:c.hidden_color):c.ally_color;
        const auto color=ImGui::ColorConvertFloat4ToU32({rgba[0],rgba[1],rgba[2],rgba[3]});
        if(overlay&&c.sounds&&p.sound_age>=0){sound(d,p,c,*current,now,size,rgba);drawn=true;}
        ImVec2 min{size.x,size.y},max{};bool projected=true;
        for(int i=0;i<8;++i){const auto q=project(current->matrix,p.origin+Vector{i&1?p.maximum.x:p.minimum.x,i&2?p.maximum.y:p.minimum.y,i&4?p.maximum.z:p.minimum.z},size.x,size.y);if(!q){projected=false;break;}min.x=std::min(min.x,q->x);min.y=std::min(min.y,q->y);max.x=std::max(max.x,q->x);max.y=std::max(max.y,q->y);}
        if(!projected||max.x<0||min.x>size.x||max.y<0||min.y>size.y) {
            if(!overlay||!c.arrows||!p.enemy)continue;
            const auto& m=current->matrix;const auto v=p.origin;
            float x=m[0][0]*v.x+m[0][1]*v.y+m[0][2]*v.z+m[0][3],y=-(m[1][0]*v.x+m[1][1]*v.y+m[1][2]*v.z+m[1][3]);
            const float n=std::hypot(x,y);if(!std::isfinite(n)||n<.001f)continue;x/=n;y/=n;
            const float margin=c.arrow_size*2+font;
            const float reach=std::min((size.x*.5f-margin)/std::max(std::abs(x),.001f),(size.y*.5f-margin)/std::max(std::abs(y),.001f));if(reach<=0)continue;
            const ImVec2 tip{size.x*.5f+x*reach,size.y*.5f+y*reach};
            const ImVec2 a{tip.x-x*c.arrow_size-y*c.arrow_size*.6f,tip.y-y*c.arrow_size+x*c.arrow_size*.6f},b{tip.x-x*c.arrow_size+y*c.arrow_size*.6f,tip.y-y*c.arrow_size-x*c.arrow_size*.6f};
            d->AddTriangleFilled(tip,a,b,color);d->AddTriangle(tip,a,b,IM_COL32(0,0,0,220));drawn=true;continue;
        }
        if(max.x-min.x<2||max.y-min.y<2||max.x-min.x>size.x*2||max.y-min.y>size.y*2)continue;
        const float middle=(min.x+max.x)*.5f;
        box(d,p,c,min,max);
        if(!overlay){drawn=true;continue;}
        if(c.name&&!p.name.empty())text(d,{middle,min.y-font-3},IM_COL32_WHITE,p.name.c_str(),true);
        const auto health=std::clamp(p.health/100.f,0.f,1.f);
        if(c.health)bar(d,{min.x-6,min.y},{min.x-3,max.y},health,ImGui::ColorConvertFloat4ToU32({1-health,health,.2f,1}),true);
        if(c.health_number){char value[16];std::snprintf(value,sizeof(value),"%d",p.health);text(d,{min.x-8-ImGui::CalcTextSize(value).x,max.y-(max.y-min.y)*health-font*.5f},IM_COL32_WHITE,value);}
        float below=max.y+3;
        if(c.ammo&&p.clip>=0){if(p.max_clip>0)bar(d,{min.x,below},{max.x,below+3},static_cast<float>(p.clip)/p.max_clip,color,false);char value[24];std::snprintf(value,sizeof(value),"%d",p.clip);text(d,{max.x+4,below-font*.5f},color,value);below+=7;}
        if(c.armor){bar(d,{min.x,below},{max.x,below+3},p.armor/100.f,IM_COL32(125,175,255,255),false);below+=7;}
        if(c.weapon&&c.weapon_mode>0&&p.definition){
            const auto icon=lookup?lookup(owner,p.definition):Icon{};
            const float height=font*1.5f,width=height*std::clamp(icon.aspect,.5f,3.f);
            const ImVec2 at{middle-width*.5f,below},end{middle+width*.5f,below+height};
            if(icon.texture)d->AddImage(ImTextureRef(icon.texture),at,end,{icon.u0,icon.v0},{icon.u1,icon.v1});
            else {d->AddRect(at,end,IM_COL32(150,150,150,180),2);d->AddLine({middle-width*.2f,below+height*.5f},{middle+width*.2f,below+height*.5f},IM_COL32(150,150,150,180),2);}
            below+=height+2;
        }
        if(c.weapon&&c.weapon_mode!=1&&!p.weapon.empty()){text(d,{middle,below},IM_COL32_WHITE,p.weapon.c_str(),true);below+=font;}
        if(c.distance){char value[24];std::snprintf(value,sizeof(value),"%.0f m",p.distance);text(d,{middle,below},IM_COL32_WHITE,value,true);}
        flags(d,p,c,min,max,color,font);
        if(c.snapline)d->AddLine({size.x*.5f,size.y-1},{middle,max.y},color,c.thickness);
        hitboxes(d,p,c,current->matrix,size,color);
        bones(d,p,c,current->matrix,size,color);
        drawn=true;
    }
    return drawn&&d->VtxBuffer.Size>initial;
}
}
