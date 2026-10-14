#pragma once
#include "core/features/combat/targeting.hpp"
#include "core/features/combat/aim_settings.hpp"
#include "core/features/movement/planner.hpp"
namespace iris::combat {
using nospread::Vector;
inline float random_unit(std::uint32_t seed) noexcept {seed^=seed>>16;seed*=0x7feb352du;seed^=seed>>15;seed*=0x846ca68bu;seed^=seed>>16;return static_cast<float>((seed+.5)/4294967296.0);}
inline float sampled_delay(float lower,float upper,std::uint32_t seed) noexcept {
    return lower+(std::max(lower+1.f,upper)-lower)*random_unit(seed);
}
inline float gaussian(std::uint32_t seed) noexcept {
    const float u=std::max(random_unit(seed),1e-7f),v=random_unit(seed^0xa511e9b3u);
    return std::sqrt(-2.f*std::log(u))*std::cos(6.283185307f*v);
}
inline Vector assist_step(Vector delta,Vector user,float strength,float dt,float radius) noexcept {
    if(!nospread::finite(delta)||!nospread::finite(user)||!std::isfinite(dt)||dt<=0)return {};
    const float distance=std::hypot(delta.x,delta.y),input=std::hypot(user.x,user.y);
    if(input<=1e-6f||!std::isfinite(strength)||!std::isfinite(radius)||radius<=0||distance>radius)return {};
    strength=std::clamp(strength,0.f,1.f);
    const float proximity=std::clamp(1.f-distance/std::max(.1f,radius),0.f,1.f);
    const float alignment=distance>1e-6f?std::clamp((delta.x*user.x+delta.y*user.y)/(distance*input),-1.f,1.f):0.f;
    // Scale only the user's existing motion. Zero input cannot turn the view;
    // the gain stays positive, so opposition remains movable at full strength.
    const float gain=std::max(-.85f,2.f*strength*(.8f*std::max(0.f,alignment)*(1.f-proximity)-
        proximity*proximity*(alignment>0?.6f:.45f)));
    return user*gain;
}
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
    bool sampled{};
    Vector update(Vector view,Vector punch,bool firing,float strength) {
        if(!nospread::finite(punch)||!nospread::finite(view)){previous={};sampled=false;return view;}
        const auto delta=sampled?punch-previous:punch;
        // Track the natural settling between bursts without moving the view.
        // Resuming fire then compensates only new recoil, not the remaining punch.
        previous=punch;sampled=true;
        if(!firing)return view;
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
