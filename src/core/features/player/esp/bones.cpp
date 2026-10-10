#include "core/features/player/esp/drawing.hpp"
#include <algorithm>
#include <cstdio>

namespace iris::visuals::esp {
void bones(ImDrawList* d,const Player& p,const Settings& c,const Matrix& matrix,ImVec2 size,ImU32 color) {
        const auto line=[&](Vector a,Vector b){const auto from=project(matrix,a,size.x,size.y),to=project(matrix,b,size.x,size.y);if(from&&to)d->AddLine(point(*from),point(*to),color,c.thickness);};
        if(c.skeleton){
            // Connect native hitbox bones by hitgroup, avoiding fixed indices
            // that break on agents and custom models.
            std::vector<Vector> torso;for(const auto& shape:p.shapes)if(shape.group>=1&&shape.group<=3)torso.push_back(shape.bone);
            std::sort(torso.begin(),torso.end(),[](Vector a,Vector b){return a.z>b.z;});
            for(std::size_t i=1;i<torso.size();++i)line(torso[i-1],torso[i]);
            if(!torso.empty())for(int group=4;group<=7;++group){std::vector<Vector> limb;for(const auto& shape:p.shapes)if(shape.group==group)limb.push_back(shape.bone);auto previous=group<6?torso.front():torso.back();while(!limb.empty()){const auto nearest=std::min_element(limb.begin(),limb.end(),[&](Vector a,Vector b){return nospread::length(a-previous)<nospread::length(b-previous);});line(previous,*nearest);previous=*nearest;limb.erase(nearest);}}
        }
}

}
