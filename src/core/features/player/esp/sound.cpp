#include "core/features/player/esp/drawing.hpp"
#include <algorithm>
#include <cstdio>

namespace iris::visuals::esp {
void sound(ImDrawList* d,const Player& p,const Settings& c,const Snapshot& frame,std::uint64_t now,ImVec2 size,const std::array<float,4>& rgba) {
        if(c.sounds&&p.sound_age>=0){
            const float age=p.sound_age+static_cast<float>(now-frame.sampled)/1500.f;
            if(age<1){const auto faded=ImGui::ColorConvertFloat4ToU32({rgba[0],rgba[1],rgba[2],rgba[3]*(1-age)});std::optional<Vector> previous;for(int i=0;i<=48;++i){const float angle=i*6.2831853f/48;const auto q=project(frame.matrix,p.origin+Vector{std::cos(angle)*(12+age*60),std::sin(angle)*(12+age*60),2},size.x,size.y);if(previous&&q)d->AddLine(point(*previous),point(*q),faded,c.thickness);previous=q;}}
        }
}

}
