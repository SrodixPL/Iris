#include "core/features/player/esp/drawing.hpp"
#include <algorithm>
namespace iris::visuals::esp {

ImVec2 point(Vector v){return {v.x,v.y};}
void text(ImDrawList* d,ImVec2 at,ImU32 color,const char* value,bool center) {
    if(center)at.x-=ImGui::CalcTextSize(value).x*.5f;
    d->AddText({at.x+1,at.y+1},IM_COL32(0,0,0,230),value);d->AddText(at,color,value);
}
void bar(ImDrawList* d,ImVec2 min,ImVec2 max,float ratio,ImU32 color,bool vertical) {
    d->AddRectFilled({min.x-1,min.y-1},{max.x+1,max.y+1},IM_COL32(0,0,0,220));
    ratio=std::clamp(ratio,0.f,1.f);
    if(vertical)min.y=max.y-(max.y-min.y)*ratio;else max.x=min.x+(max.x-min.x)*ratio;
    d->AddRectFilled(min,max,color);
}
}
