#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace iris::combat {
inline float required_damage(float selected,int health,bool relative=false) noexcept {
    return relative?static_cast<float>(std::max(1,health))+selected:selected;
}
struct RageGate {
    std::uint32_t pawn{},weapon{},target{};
    std::uint64_t acquired{},scope_attempt{},updated{};
    bool scope_pending{};
    void reset() noexcept {*this={};}
    void clear_target() noexcept {target=0;acquired=0;}
    bool confirm(std::uint32_t p,std::uint32_t w,std::uint32_t t,std::uint64_t now,float delay) noexcept {
        if(!t){clear_target();return false;}
        if(pawn!=p||weapon!=w||now<updated||now-updated>250){
            *this={p,w,t,now,0,now,false};
        }
        else if(target!=t){target=t;acquired=now;}
        updated=now;
        return std::isfinite(delay)&&delay>=0&&now-acquired>=static_cast<std::uint64_t>(delay);
    }
    bool scope(bool scoped,std::uint64_t now) noexcept {
        if(scoped){scope_pending=false;return false;}
        if(scope_pending&&now>=scope_attempt&&now-scope_attempt<1000)return false;
        scope_pending=true;scope_attempt=now;return true;
    }
};
}
