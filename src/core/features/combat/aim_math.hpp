#pragma once
#include "core/features/combat/targeting.hpp"
#include "core/features/combat/aim_settings.hpp"
#include "core/features/movement/planner.hpp"
namespace iris::combat {
using nospread::Vector;
inline Vector clamp_angles(Vector v) {return {std::clamp(v.x,-89.f,89.f),std::remainder(v.y,360.f),0};}
inline Vector smooth(Vector current,Vector target,float dt,float time,float speed) {
    if(!nospread::finite(current)||!nospread::finite(target)||!std::isfinite(dt)||dt<=0||!std::isfinite(time)||time<=0||!std::isfinite(speed)||speed<=0)return current;
    dt=std::min(dt,.05f);
    Vector delta{target.x-current.x,std::remainder(target.y-current.y,360.f),0};
    auto step=delta*(-std::expm1(-dt/time));
    const auto magnitude=std::hypot(step.x,step.y),limit=speed*dt;
    if(magnitude>limit)step=step*(limit/magnitude);
    return clamp_angles(current+step);
}
struct ShotConfirmation {
    std::uint32_t target{};double since{};
    void reset() noexcept {target=0;since=0;}
    bool ready(std::uint32_t candidate,double now,float delay) noexcept {
        if(!candidate || !std::isfinite(now) || !std::isfinite(delay) || delay<0){reset();return false;}
        if(candidate!=target || now<since){target=candidate;since=now;}
        return now-since>=delay;
    }
};
struct RecoilState {
    Vector previous{};
    bool active{};
    Vector update(Vector view,Vector punch,bool firing,float strength) {
        if(!firing||!nospread::finite(punch)||!nospread::finite(view)){previous={};active=false;return view;}
        const auto delta=active?punch-previous:punch;
        previous=punch;active=true;
        return clamp_angles(view-Vector{delta.x,delta.y,0}*strength);
    }
};
inline float direct_damage(float base,float head,float range_modifier,float distance,int region,int armor,bool helmet,float armor_ratio) {
    if(!std::isfinite(base)||!std::isfinite(head)||!std::isfinite(range_modifier)||!std::isfinite(distance)||!std::isfinite(armor_ratio)||base<=0||head<=0||range_modifier<=0||range_modifier>1||distance<0||armor_ratio<0)return 0;
    const auto multiplier=region==0?head:region==2||region==3?1.25f:region==5?.75f:1.f;
    float damage=base*multiplier*std::pow(range_modifier,distance/500.f);
    if(armor>0 && region!=5 && (region!=0||helmet)) {
        const float health=damage*std::clamp(armor_ratio*.5f,0.f,1.f);
        damage=(damage-health)*.5f>armor?damage-armor*2.f:health;
    }
    return damage;
}
template<class Sample> bool chance_passes(float percent,Sample sample,int count=128) {
    if(!std::isfinite(percent)||percent<0||percent>100||count<=0)return false;
    const int required=static_cast<int>(std::ceil(percent*.01f*count));
    int hits=0;
    for(int i=0;i<count && hits<required;++i){if(sample(i))++hits;if(hits+count-i-1<required)return false;}
    return hits>=required;
}

}
