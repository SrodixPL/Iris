#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
namespace iris::movement {
using Axes=std::array<float,2>;
struct NativeInput {
    std::array<std::atomic<std::uint64_t>,2> axes{};
    void sample(unsigned axis,float value,std::uint32_t now) noexcept {
        if(axis<axes.size()&&std::isfinite(value))axes[axis].store((std::uint64_t{now}<<32)|std::bit_cast<std::uint32_t>(std::clamp(value,-1.f,1.f)));
    }
    std::optional<Axes> read(std::uint32_t now) const noexcept {
        Axes result;
        for(unsigned i=0;i<axes.size();++i){const auto value=axes[i].load();
            if(!value||now-static_cast<std::uint32_t>(value>>32)>100)return {};
            result[i]=std::bit_cast<float>(static_cast<std::uint32_t>(value));
        }
        return result;
    }
};
inline Axes rotate(Axes v,float delta) noexcept {
    const float angle=delta*(3.14159265358979323846f/180.f),c=std::cos(angle),s=std::sin(angle);
    return {c*v[0]+s*v[1],-s*v[0]+c*v[1]};
}
inline Axes limit(Axes v) noexcept {
    const float gain=std::max({1.f,std::abs(v[0]),std::abs(v[1])});
    return {v[0]/gain,v[1]/gain};
}
struct Event {float when{},yaw{},forward{},left{};};
struct Timeline {Axes base{};std::array<Axes,64> deltas{};};
inline std::optional<Timeline> correct(Axes initial,float delta,std::span<const Event> events) noexcept {
    if(events.size()>64 || !std::isfinite(delta)||!std::isfinite(initial[0])||!std::isfinite(initial[1]))return {};
    Timeline result;auto source=initial;auto previous=limit(rotate(source,delta));result.base=previous;float last{};
    for(std::size_t i=0;i<events.size();++i){const auto& e=events[i];
        if(!std::isfinite(e.when)||e.when<last||e.when>1||!std::isfinite(e.yaw)||!std::isfinite(e.forward)||!std::isfinite(e.left))return {};
        last=e.when;source[0]+=e.forward;source[1]+=e.left;
        // A constant command-yaw offset follows the original timed yaw changes.
        const auto next=limit(rotate(source,delta));result.deltas[i]={next[0]-previous[0],next[1]-previous[1]};previous=next;
    }
    return result;
}
inline std::uint32_t random_word(std::uint32_t value) noexcept {
    value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;return value^(value>>16);
}
}
