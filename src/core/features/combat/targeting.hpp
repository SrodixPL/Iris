#pragma once
#include "core/features/combat/spread_solver.hpp"
#include <algorithm>
namespace iris::nospread {
struct BoneTransform {Vector position;float scale{};float x{},y{},z{},w{};};
static_assert(sizeof(BoneTransform)==32);
inline std::optional<Vector> transform_point(BoneTransform b,Vector local) {
    const float norm=b.x*b.x+b.y*b.y+b.z*b.z+b.w*b.w;
    if(!finite(b.position)||!finite(local)||!std::isfinite(b.scale)||b.scale<=0||b.scale>100||!std::isfinite(norm)||std::abs(norm-1)>.01f)return {};
    local=local*b.scale;
    const Vector q{b.x,b.y,b.z};
    const auto cross=[](Vector a,Vector c){return Vector{a.y*c.z-a.z*c.y,a.z*c.x-a.x*c.z,a.x*c.y-a.y*c.x};};
    const auto twice=cross(q,local)*2;
    const auto point=b.position+local+twice*b.w+cross(q,twice);
    return finite(point)?std::optional<Vector>{point}:std::optional<Vector>{};
}
inline float angular_distance(Vector a,Vector b) {
    a=normalized(a);b=normalized(b);
    const Vector cross{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
    return std::atan2(length(cross),a.x*b.x+a.y*b.y+a.z*b.z)*degrees;
}
inline bool within_fov(Vector view,Vector eye,Vector point,float fov) {
    return finite(view)&&finite(eye)&&finite(point)&&length(point-eye)>.001f && std::isfinite(fov)&&fov>=1&&fov<=180 && angular_distance(bullet(view,{}),point-eye)<=fov*.5f+1e-5f;
}
inline bool shot_requested(bool auto_fire,bool manual) {return auto_fire||manual;}
inline std::optional<Solution> without_spread(Vector desired,Vector punch) {
    Vector sent{desired.x-punch.x,std::remainder(desired.y-punch.y,360.f),0};
    if(!finite(sent)||std::abs(sent.x)>89)return {};
    return Solution{sent,bullet(desired,{}),0,0};
}
inline float fov_radius(float full_cone,float camera,float height) {
    if(!std::isfinite(full_cone)||!std::isfinite(camera)||!std::isfinite(height)||full_cone<1||full_cone>180||camera<=1||camera>=179||height<=0)return 0;
    return height*(2.f/3.f)*std::tan(std::min(full_cone*.5f,89.9f)/degrees)/std::tan(camera*.5f/degrees);
}
}
