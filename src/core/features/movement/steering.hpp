#pragma once
#include "core/features/movement/settings.hpp"
#include "core/features/movement/planner.hpp"
#include <array>
#include <cmath>
#include <algorithm>
#include <optional>
namespace iris::movement {
struct SteeringOptions : Options {
    bool only_while_holding_jump{},stop_on_shift{},stop_on_key_release{};
    int mode{2};float direction{};
    bool operator==(const SteeringOptions&) const=default;
};
struct Settings {
    bool view{},keys{},mouse{},smooth{},quick_stop{},air_stop{};
    float strength{.65f},smoothing{.25f};
    SteeringOptions options;
    bool bhop{};
    bool slow_walk{},rage_stop{};
    bool active() const noexcept {return view||keys||mouse||smooth||quick_stop||air_stop||bhop||slow_walk||rage_stop;}
    bool operator==(const Settings&) const=default;
};
struct Steering {float previous_yaw{},filtered{},lateral{},desired_yaw{},key_forward{},key_left{};bool valid{},alternate{},air{},desired_valid{};};
struct AirPhysics {float accelerate{},cap{},maximum{},friction{};};
inline std::array<float,2> wish_axes(float wish_yaw,float view_yaw,float strength=1) noexcept {
    if(!std::isfinite(wish_yaw)||!std::isfinite(view_yaw)||!std::isfinite(strength))return {};
    const float angle=std::remainder(wish_yaw-view_yaw,360.f)*(3.14159265358979323846f/180.f);
    strength=std::clamp(strength,0.f,1.f);
    return {std::cos(angle)*strength,std::sin(angle)*strength};
}
inline std::optional<Axes> steer(const Settings& s,Steering& state,float yaw,float vx,float vy,float forward,float left,bool grounded,bool jumping,float dt=1.f/64.f,bool advance=true,std::optional<AirPhysics> physics={}) noexcept {
    if(!std::isfinite(yaw)||!std::isfinite(vx)||!std::isfinite(vy)||!std::isfinite(forward)||!std::isfinite(left)||!std::isfinite(dt)||dt<0)return {};
    constexpr float rad=3.14159265358979323846f/180.f;
    const bool valid=state.valid,keys=std::hypot(forward,left)>.001f;
    const float turn=valid?std::remainder(yaw-state.previous_yaw,360.f):0;
    state.previous_yaw=yaw;state.valid=true;
    const float speed=std::hypot(vx,vy),velocity_yaw=std::atan2(vy,vx)/rad;
    if(!keys&&((grounded&&s.quick_stop&&!jumping)||(!grounded&&s.air_stop))){
        state.air=state.desired_valid=false;
        return speed>=3?std::optional<Axes>(wish_axes(velocity_yaw+180,yaw,std::min(speed/30.f,1.f))):std::nullopt;
    }
    if(grounded||(!(s.view||s.keys||s.mouse))||(s.options.only_while_holding_jump&&!jumping)||(s.options.stop_on_key_release&&!keys)||(s.keys&&!keys)){
        state.air=state.desired_valid=false;return {};
    }
    if(s.mouse&&std::abs(turn)<.001f){state.air=false;return {};}
    const bool changed=s.keys&&(forward!=state.key_forward||left!=state.key_left);
    state.key_forward=forward;state.key_left=left;
    const float requested=yaw+(s.keys?std::atan2(left,forward)/rad:s.options.mode==1?s.options.direction:0);
    if(!state.desired_valid||speed<10||changed){state.desired_yaw=requested;state.desired_valid=true;}
    else state.desired_yaw+=std::clamp(std::remainder(requested-state.desired_yaw,360.f),-s.options.turn_speed*dt,s.options.turn_speed*dt);
    const float strength=std::clamp(s.strength,0.f,1.f);if(strength==0)return {};
    if(speed<10){state.air=false;return wish_axes(state.desired_yaw,yaw,strength);}
    // Nexoria's velocity-relative steering uses the available air-acceleration projection.
    float ideal=90;
    if(physics){const auto& p=*physics;const float wish_speed=p.maximum*strength;
        const float half=p.accelerate*wish_speed*p.friction*std::clamp(dt,0.f,.05f)*.5f;
        const float floor=std::max(half,std::min(p.cap,wish_speed)-half);
        if(std::isfinite(floor)&&floor>=0)ideal=90-std::clamp(std::atan(floor/speed)/rad,0.f,45.f);
    }
    const float error=std::remainder(state.desired_yaw-velocity_yaw,360.f);
    float side=std::abs(error)>.5f?(error>0?1.f:-1.f):(state.alternate?1.f:-1.f);
    if(s.mouse)side=turn>0?1.f:-1.f;
    if(advance)state.alternate=side>0;
    float amount=strength;
    if(s.smooth){
        const float alpha=1-std::exp(-std::clamp(dt,0.f,.05f)/std::max(.001f,s.smoothing*.25f));
        if(!valid||!state.air)state.lateral=side;else state.lateral+=(side-state.lateral)*alpha;
        side=state.lateral>=0?1.f:-1.f;amount*=std::abs(state.lateral);
    }
    state.air=true;
    return wish_axes(velocity_yaw+side*ideal,yaw,amount);
}
}
