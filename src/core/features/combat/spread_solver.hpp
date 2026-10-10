#pragma once
#include "util/math.hpp"
#include <cmath>
#include <array>
#include <algorithm>
#include <cstdint>
#include <optional>
namespace iris::nospread {
using math::Vector;
using math::degrees;
using math::finite;
using math::length;
using math::normalized;
inline std::optional<Vector> aim(Vector eye,Vector point) {
    const auto d=point-eye;
    if(!finite(d)||length(d)<.001f)return {};
    return Vector{-std::atan2(d.z,std::hypot(d.x,d.y))*degrees,std::atan2(d.y,d.x)*degrees,0};
}
inline Vector bullet(Vector a,Vector spread) {
    const float sp=std::sin(a.x/degrees),cp=std::cos(a.x/degrees),sy=std::sin(a.y/degrees),cy=std::cos(a.y/degrees),sr=std::sin(a.z/degrees),cr=std::cos(a.z/degrees);
    const Vector f{cp*cy,cp*sy,-sp},r{-sr*sp*cy+cr*sy,-sr*sp*sy-cr*cy,-sr*cp},u{cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp};
    return normalized(f+r*spread.x+u*spread.y);
}
struct Solution {Vector sent{},direction{};std::uint32_t seed{};float error{};};
// Seed from the transmitted (recoil-subtracted) angle, then verify the actual ray.
template<class Seed,class Spread> std::optional<Solution> solve(Vector desired,Vector punch,int tick,Seed seed_of,Spread spread_of,float max_spread=1.f) {
    if(!finite(desired)||!finite(punch)||tick<=0||!std::isfinite(max_spread)||max_spread<0||max_spread>1)return {};
    const auto target=bullet(desired,{});
    const auto corrected=[&](Vector s)->std::optional<Vector> {
        if(!finite(s)||std::hypot(s.x,s.y)>1)return {};
        const float norm=std::sqrt(1+s.x*s.x+s.y*s.y);
        const float vertical=std::sin(desired.x/degrees)*norm/std::sqrt(1+s.y*s.y);
        if(std::abs(vertical)>1)return {};
        const float pitch=std::atan(s.y)+std::asin(vertical);
        const float yaw=desired.y+std::atan2(s.x,std::cos(pitch)+s.y*std::sin(pitch))*degrees;
        const Vector sent{pitch*degrees-punch.x,std::remainder(yaw-punch.y,360.f),0};
        return finite(sent)&&std::abs(sent.x)<=89.f?std::optional<Vector>{sent}:std::nullopt;
    };
    const auto try_seed=[&](std::uint32_t seed)->std::optional<Solution> {
        const auto s=spread_of(seed);const auto sent=corrected(s);if(!sent)return {};
        if(seed_of(*sent,tick)!=seed)return {};
        const auto actual=bullet(*sent+Vector{punch.x,punch.y,0},s);
        const auto error=length(actual-target);
        if(!std::isfinite(error)||error>1e-5f)return {};
        return Solution{*sent,actual,seed,error};
    };
    Vector fallback=desired-Vector{punch.x,punch.y,0};fallback.z=0;
    auto seed=seed_of(fallback,tick);
    std::array<std::uint32_t,24> visited{};int visited_count{};
    for(int i=0;i<24;++i) {
        if(std::find(visited.begin(),visited.begin()+visited_count,seed)!=visited.begin()+visited_count)break;
        visited[visited_count++]=seed;
        if(auto result=try_seed(seed))return result;
        const auto s=spread_of(seed);
        if(!finite(s))return {};
        const auto next=corrected(s);if(!next)return {};fallback=*next;
        const auto next_seed=seed_of(*next,tick);
        if(next_seed==seed)break;
        seed=next_seed;
    }
    // Bound retries after a quantized seed cycle instead of stalling a game frame.
    const float reach=std::atan(max_spread)*degrees/std::max(.1f,std::abs(std::cos(desired.x/degrees)));
    int probes{};
    for(int step=0;step<32;++step){
        const float offset=(step%2?1.f:-1.f)*static_cast<float>((step+1)/2)*.5f;
        if(std::abs(offset)>reach+.5f)continue;
        ++probes;if(auto result=try_seed(seed_of({fallback.x+offset,fallback.y,0},tick)))return result;
    }
    for(int radius=1;radius<=8 && probes<64;++radius)for(int x=-radius;x<=radius && probes<64;++x)for(int y=-radius;y<=radius && probes<64;++y){
        if(std::abs(x)!=radius && std::abs(y)!=radius)continue;
        ++probes;const Vector test{fallback.x+x*.5f,fallback.y+y*.5f,0};
        if(std::abs(x*.5f)>reach+.5f || std::abs(y*.5f)>reach+.5f)continue;
        if(auto result=try_seed(seed_of(test,tick)))return result;
    }
    return {};
}
}
