#pragma once
#include "core/features/player/visual_settings.hpp"
#include "core/features/combat/targeting.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <span>

namespace iris::visuals {
using Vector=nospread::Vector;
using Matrix=std::array<std::array<float,4>,4>;
inline std::optional<std::array<Vector,2>> local_bounds(Vector a,Vector b,unsigned type,float radius) {
    if(!nospread::finite(a)||!nospread::finite(b)||type>2||!std::isfinite(radius)||radius<0||radius>100)return {};
    if(type==1)b=a;
    if(type==0)radius=0;
    return std::array{Vector{std::min(a.x,b.x)-radius,std::min(a.y,b.y)-radius,std::min(a.z,b.z)-radius},Vector{std::max(a.x,b.x)+radius,std::max(a.y,b.y)+radius,std::max(a.z,b.z)+radius}};
}
struct Shape {int group{};Vector bone{},center{};std::array<Vector,8> corners{};};
struct Player {
    Vector origin{},minimum{},maximum{};
    bool enemy{},visible{},planting{},helmet{};
    int health{},armor{},clip{-1},max_clip{},money{-1},definition{};
    std::string name,weapon;
    std::array<bool,9> flags{};
    float distance{};
    float sound_age{-1};
    std::vector<Shape> shapes;
};
struct Snapshot {Matrix matrix{};Settings settings;std::uint64_t sampled{};std::vector<Player> players;};
inline std::atomic<std::shared_ptr<const Snapshot>> snapshot;
struct WallTarget {Matrix matrix{};std::array<Vector,8> corners{};Vector point{};std::uint64_t sampled{};std::uint32_t handle{};float damage{};int region{};};
inline std::atomic<std::shared_ptr<const WallTarget>> wall_target;
inline std::optional<Vector> project(const Matrix& m,Vector p,float width,float height) {
    if(!nospread::finite(p)||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)return {};
    const auto row=[&](int i){return m[i][0]*p.x+m[i][1]*p.y+m[i][2]*p.z+m[i][3];};
    const float w=row(3);if(!std::isfinite(w)||w<=.01f)return {};
    Vector result{(row(0)/w+1)*width*.5f,(1-row(1)/w)*height*.5f,0};
    return nospread::finite(result)?std::optional<Vector>{result}:std::optional<Vector>{};
}
struct Icon {std::uint64_t texture{};float u0{},v0{},u1{1},v1{1},aspect{1};};
struct ScreenBounds {Vector minimum{},maximum{};};
// Clip the camera-facing collision rectangle before perspective division. A
// world-axis box includes depth which inflates bounds when the camera pitches.
inline std::optional<ScreenBounds> player_bounds(const Matrix& m,const Player& p,float width,float height) {
    if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||!nospread::finite(p.origin)||!nospread::finite(p.minimum)||!nospread::finite(p.maximum))return {};
    const float length=std::hypot(m[0][0],m[0][1]);
    if(!std::isfinite(length)||length<.0001f)return {};
    const Vector right{m[0][0]/length,m[0][1]/length,0};
    const float radius=std::max(p.maximum.x-p.minimum.x,p.maximum.y-p.minimum.y)*.5f;
    const Vector center=p.origin+Vector{(p.minimum.x+p.maximum.x)*.5f,(p.minimum.y+p.maximum.y)*.5f,0};
    struct Clip {float x,y,w;};
    std::vector<Clip> polygon;
    for(const auto& v:std::array{center-right*radius+Vector{0,0,p.minimum.z},center+right*radius+Vector{0,0,p.minimum.z},center+right*radius+Vector{0,0,p.maximum.z},center-right*radius+Vector{0,0,p.maximum.z}}){
        const auto row=[&](int i){return m[i][0]*v.x+m[i][1]*v.y+m[i][2]*v.z+m[i][3];};
        const Clip clip{row(0),row(1),row(3)};if(!std::isfinite(clip.x)||!std::isfinite(clip.y)||!std::isfinite(clip.w))return {};polygon.push_back(clip);
    }
    for(int plane=0;plane<5;++plane){
        const auto distance=[&](const Clip& v){switch(plane){case 0:return v.w-.01f;case 1:return v.w+v.x;case 2:return v.w-v.x;case 3:return v.w+v.y;default:return v.w-v.y;}};
        std::vector<Clip> output;if(polygon.empty())return {};
        auto a=polygon.back();float da=distance(a);
        for(const auto b:polygon){const float db=distance(b);if((da>=0)!=(db>=0)){const float t=da/(da-db);output.push_back({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.w+(b.w-a.w)*t});}if(db>=0)output.push_back(b);a=b;da=db;}
        polygon=std::move(output);
    }
    ScreenBounds bounds{{width,height,0},{0,0,0}};
    for(const auto v:polygon){const float x=std::clamp((v.x/v.w+1)*width*.5f,0.f,width),y=std::clamp((1-v.y/v.w)*height*.5f,0.f,height);bounds.minimum.x=std::min(bounds.minimum.x,x);bounds.minimum.y=std::min(bounds.minimum.y,y);bounds.maximum.x=std::max(bounds.maximum.x,x);bounds.maximum.y=std::max(bounds.maximum.y,y);}
    if(bounds.maximum.x<=bounds.minimum.x||bounds.maximum.y<=bounds.minimum.y)return {};
    return bounds;
}
bool draw(Icon (*lookup)(void*,int)=nullptr,void* owner=nullptr);
bool draw_snapshot(const Snapshot& frame,std::uint64_t now,Icon (*lookup)(void*,int)=nullptr,void* owner=nullptr,std::span<const std::optional<ScreenBounds>> prepared={});
bool draw_wall_target(const WallTarget& frame,std::uint64_t now,const std::array<float,4>& color);
}
