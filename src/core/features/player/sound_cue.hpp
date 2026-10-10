#pragma once
#include <cmath>
#include <cstdint>

namespace iris::visuals {
struct SoundCue {
    std::uintptr_t entity{};
    float last{};
    std::uint64_t emitted{};
    bool initialized{};
    float sample(std::uintptr_t address,float time,std::uint64_t now) {
        if(!std::isfinite(time))return -1;
        if(!initialized||entity!=address){entity=address;last=time;emitted=0;initialized=true;return -1;}
        if(time!=last){emitted=time>last&&time>0?now:0;last=time;}
        return emitted&&now>=emitted&&now-emitted<1500?static_cast<float>(now-emitted)/1500.f:-1;
    }
};
}
