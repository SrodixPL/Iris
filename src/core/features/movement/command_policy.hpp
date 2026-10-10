#pragma once
#include "core/features/movement/requests.hpp"
#include <vector>
namespace iris::movement {
struct SteeringEvent {float when{},yaw{},forward{},left{};std::uint64_t button{};bool pressed{};};
struct AxisEvent {float when{};Axes delta{};};
struct SteeringDecision {Axes initial{},terminal{};std::vector<AxisEvent> events;};
inline Axes directions(std::uint64_t buttons) noexcept {
    return {float(bool(buttons&8))-float(bool(buttons&16)),float(bool(buttons&512))-float(bool(buttons&1024))};
}
inline std::optional<SteeringDecision> plan_steering(const Settings& settings,Steering& phase,float yaw,Axes velocity,Axes input,
    std::uint64_t buttons,bool grounded,bool jumping,bool shift,bool rage_stop,std::span<const SteeringEvent> source,
    std::optional<AirPhysics> physics={}) {
    if(source.size()>64||!std::isfinite(yaw))return {};
    auto config=settings;
    if(shift&&config.options.stop_on_shift)config.view=config.keys=config.mouse=config.smooth=false;
    if(const auto ground=ground_request(rage_stop,config.slow_walk,yaw,velocity[0],velocity[1],input,grounded,jumping))
        return SteeringDecision{*ground,*ground,{}};
    if(!config.options.subtick||grounded||!(config.view||config.keys||config.mouse)){
        const auto axes=steer(config,phase,yaw,velocity[0],velocity[1],input[0],input[1],grounded,jumping,1.f/64.f,true,physics);
        if(!axes)return {};return SteeringDecision{*axes,*axes,{}};
    }
    std::vector<float> times;times.reserve(64);float previous{},total_yaw{};
    for(const auto& event:source){
        if(!std::isfinite(event.when)||event.when<previous||event.when>1||!std::isfinite(event.yaw)||!std::isfinite(event.forward)||!std::isfinite(event.left))return {};
        previous=event.when;total_yaw+=event.yaw;if(event.when>0&&event.when<1)times.push_back(event.when);
    }
    for(int i=1;i<8&&times.size()+source.size()<62;++i)times.push_back(i/8.f);
    std::sort(times.begin(),times.end());times.erase(std::unique(times.begin(),times.end()),times.end());times.push_back(1);
    constexpr std::uint64_t mask=8|16|512|1024;
    auto held=buttons;
    for(auto i=source.rbegin();i!=source.rend();++i)if(i->button&mask)held=i->pressed?held&~i->button:held|i->button;
    if(std::any_of(source.begin(),source.end(),[](const auto& e){return (e.button&(8|16|512|1024))!=0;}))input=directions(held);
    float view=yaw-total_yaw;std::size_t index{};
    const auto consume=[&](float time){
        while(index<source.size()&&source[index].when<=time){const auto& event=source[index++];view+=event.yaw;
            if(event.button&mask){held=event.pressed?held|event.button:held&~event.button;input=directions(held);}
            else if(!event.button){input[0]+=event.forward;input[1]+=event.left;input=limit(input);}
        }
    };
    consume(0);
    const auto first=steer(config,phase,view,velocity[0],velocity[1],input[0],input[1],false,jumping,times.front()/64.f,true,physics);
    if(!first)return {};
    const auto initial=rotate(*first,std::remainder(yaw-view,360.f));
    SteeringDecision result{initial,initial,{}};previous=0;
    for(const float time:times){
        if(physics){const auto world=rotate(result.terminal,-yaw);const auto& p=*physics;
            const float projection=velocity[0]*world[0]+velocity[1]*world[1];
            const float gain=std::max(0.f,std::min(p.cap-projection,p.accelerate*p.maximum*p.friction*(time-previous)/64.f*.5f));
            velocity[0]+=world[0]*gain;velocity[1]+=world[1]*gain;
        }
        consume(time);
        if(time<1){
            const auto next=steer(config,phase,view,velocity[0],velocity[1],input[0],input[1],false,jumping,(time-previous)/64.f,false,physics).value_or(input);
            const auto axes=rotate(next,std::remainder(yaw-view,360.f));
            result.events.push_back({time,{axes[0]-result.terminal[0],axes[1]-result.terminal[1]}});result.terminal=axes;
        }
        previous=time;
    }
    return result;
}
}
