#include "core/features/player/esp/drawing.hpp"
#include <algorithm>
#include <cstdio>

namespace iris::visuals::esp {
FlagLabels flag_labels(const Player& p,const Settings& c) {
    FlagLabels result;
    for(unsigned i=0;i<c.flags.size();++i)if(c.flags[i]&&p.flags[i]){
        auto& value=result.values[result.count++];const char* label=flag_names[i];
        if(i==5&&p.planting)label="Planting";if(i==8)label=p.helmet?"Armor + helmet":"Armor";
        if(i==6)std::snprintf(value.data(),value.size(),"$%d",p.money);else std::snprintf(value.data(),value.size(),"%s",label);
    }
    return result;
}
void flags(ImDrawList* d,const Player& p,const Settings& c,ImVec2 min,ImVec2 max,ImU32 color,float font) {
    float tag=min.y;const auto labels=flag_labels(p,c);for(std::size_t i=0;i<labels.count;++i){text(d,{max.x+5,tag},color,labels.values[i].data());tag+=font;}
}

}
