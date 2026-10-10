#pragma once
#include "core/features/player/visual_settings.hpp"
#include "core/features/combat/targeting.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

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
bool draw(Icon (*lookup)(void*,int)=nullptr,void* owner=nullptr);
bool draw_snapshot(const Snapshot& frame,std::uint64_t now,Icon (*lookup)(void*,int)=nullptr,void* owner=nullptr);
bool draw_wall_target(const WallTarget& frame,std::uint64_t now,const std::array<float,4>& color);
}
