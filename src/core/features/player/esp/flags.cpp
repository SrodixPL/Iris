#include "core/features/player/esp/drawing.hpp"
#include <algorithm>
#include <cstdio>

namespace iris::visuals::esp {
void flags(ImDrawList* d,const Player& p,const Settings& c,ImVec2 min,ImVec2 max,ImU32 color,float font) {
        float tag=min.y;
        for(unsigned i=0;i<c.flags.size();++i)if(c.flags[i]&&p.flags[i]){char value[32];const char* label=flag_names[i];if(i==5&&p.planting)label="Planting";if(i==8)label=p.helmet?"Armor + helmet":"Armor";if(i==6){std::snprintf(value,sizeof(value),"$%d",p.money);label=value;}text(d,{max.x+5,tag},color,label);tag+=font;}
}

}
