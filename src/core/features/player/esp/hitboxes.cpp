#include "core/features/player/esp/drawing.hpp"
#include <algorithm>
#include <cstdio>

namespace iris::visuals::esp {
void hitboxes(ImDrawList* d,const Player& p,const Settings& c,const Matrix& matrix,ImVec2 size,ImU32 color) {
        const auto line=[&](Vector a,Vector b){const auto from=project(matrix,a,size.x,size.y),to=project(matrix,b,size.x,size.y);if(from&&to)d->AddLine(point(*from),point(*to),color,c.thickness);};
        if(c.hitboxes)for(const auto& shape:p.shapes)for(int i=0;i<8;++i)for(int bit=1;bit<=4;bit*=2)if(!(i&bit))line(shape.corners[i],shape.corners[i|bit]);
}

}
