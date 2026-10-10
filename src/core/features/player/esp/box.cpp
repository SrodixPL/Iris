#include "core/features/player/esp/drawing.hpp"
#include <algorithm>
#include <cstdio>

namespace iris::visuals::esp {
void box(ImDrawList* d,const Player& p,const Settings& c,ImVec2 min,ImVec2 max) {
        if(c.box&&p.distance<=c.box_max_distance&&(!c.box_visible_only||p.visible)){
            const auto& box_rgba=p.enemy?(p.visible?c.box_enemy_color:c.box_hidden_color):c.box_ally_color;
            const auto alpha=box_rgba[3]*c.box_opacity;
            const auto box_color=ImGui::ColorConvertFloat4ToU32({box_rgba[0],box_rgba[1],box_rgba[2],alpha});
            const auto outline=ImGui::ColorConvertFloat4ToU32({0,0,0,alpha*.9f});
            if(!c.box_style){d->AddRect(min,max,outline,0,0,c.thickness+2);d->AddRect(min,max,box_color,0,0,c.thickness);}
            else {const float w=(max.x-min.x)*.25f,h=(max.y-min.y)*.2f;for(int i=0;i<4;++i){const ImVec2 at{i&1?max.x:min.x,i&2?max.y:min.y},endx{at.x+(i&1?-w:w),at.y},endy{at.x,at.y+(i&2?-h:h)};for(const auto to:{endx,endy}){d->AddLine(at,to,outline,c.thickness+2);d->AddLine(at,to,box_color,c.thickness);}}}
        }
}

}
