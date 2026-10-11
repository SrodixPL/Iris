#include "core/menu/player_model.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace iris::ui::player_model {
namespace {
struct Vertex {float x,y,z,nx,ny,nz;unsigned color;};
struct Triangle {unsigned a,b,c;};
struct AuthoredShape {int group;visuals::Vector bone;std::array<visuals::Vector,8> corners;};
#include "core/menu/assets/player_mesh.inc"
}
visuals::Matrix draw(ImDrawList* d,ImVec2 min,ImVec2 max,float yaw,float pitch,visuals::Player& player,Cache& cache){
    const float y=yaw*.0174532925f,p=pitch*.0174532925f,cy=std::cos(y),sy=std::sin(y),cp=std::cos(p),sp=std::sin(p);
    const auto rotate=[&](visuals::Vector v){const float x=cy*v.x+sy*v.z,z=-sy*v.x+cy*v.z;return visuals::Vector{x,cp*v.y-sp*z,sp*v.y+cp*z};};
    const float height=max.y-min.y,scale=std::min(height/2.04f,(max.x-min.x)/.85f);
    // Immutable asset projection cache; gameplay settings never live here.
    if(cache.yaw!=yaw||cache.pitch!=pitch){
        cache.yaw=yaw;cache.pitch=pitch;cache.vertices.clear();cache.vertices.reserve(std::size(vertices));
        for(const auto& v:vertices){const auto normal=rotate({v.nx,v.ny,v.nz});const float light=.55f+.45f*std::max(0.f,normal.z*.8f+normal.y*.4f-normal.x*.2f);unsigned color=0xff000000u;for(unsigned channel=0;channel<3;++channel)color|=static_cast<unsigned>(((v.color>>(channel*8))&255)*light)<<(channel*8);cache.vertices.push_back({rotate({v.x,v.y-1.f,v.z}),color});}
        cache.faces.clear();cache.faces.reserve(std::size(triangles));
        for(unsigned i=0;i<std::size(triangles);++i){const auto& t=triangles[i];cache.faces.push_back({i,(cache.vertices[t.a].position.z+cache.vertices[t.b].position.z+cache.vertices[t.c].position.z)/3});}
        std::sort(cache.faces.begin(),cache.faces.end(),[](const Face& a,const Face& b){return a.depth<b.depth;});
    }
    const auto uv=ImGui::GetFontTexUvWhitePixel();
    d->PrimReserve(static_cast<int>(cache.faces.size()*3),static_cast<int>(cache.faces.size()*3));
    for(const auto& face:cache.faces){
        const auto& triangle=triangles[face.triangle];for(const auto id:{triangle.a,triangle.b,triangle.c}){const auto& v=cache.vertices[id];d->PrimVtx({(min.x+max.x)*.5f+v.position.x*scale,(min.y+max.y)*.5f-v.position.y*scale},uv,v.color);}
    }
    player.shapes.clear();player.shapes.reserve(std::size(shapes));
    const auto upright=[](visuals::Vector v){return visuals::Vector{v.x,-v.z,v.y};};
    for(const auto& shape:shapes){visuals::Shape value;value.group=shape.group;value.bone=upright(shape.bone);for(std::size_t i=0;i<value.corners.size();++i)value.corners[i]=upright(shape.corners[i]);player.shapes.push_back(value);}
    // Shared ESP renderers project the same authored model-space geometry.
    const float width=max.x-min.x;visuals::Matrix matrix{};
    matrix[0]={2*scale*cy/width,-2*scale*sy/width,0,0};
    matrix[1]={2*scale*sp*sy/height,2*scale*sp*cy/height,2*scale*cp/height,-2*scale*cp/height};
    matrix[3][3]=1;return matrix;
}
}
