#pragma once
#include "core/features/movement/steering.hpp"
namespace iris::movement {
inline std::optional<Axes> ground_request(bool stop,bool slow,float yaw,float vx,float vy,Axes input,bool grounded,bool jumping) noexcept {
    if(!grounded||jumping||!std::isfinite(vx)||!std::isfinite(vy))return {};
    const float speed=std::hypot(vx,vy);
    if(stop){
        if(speed<3)return Axes{};
        return wish_axes(std::atan2(vy,vx)*57.295779513f+180,yaw,std::clamp(speed/30.f,0.f,1.f));
    }
    if(slow){
        const float magnitude=std::hypot(input[0],input[1]);
        if(!std::isfinite(magnitude)||magnitude<.001f)return {};
        const float scale=std::min(magnitude,.3f)/magnitude;
        return Axes{input[0]*scale,input[1]*scale};
    }
    return {};
}
}
