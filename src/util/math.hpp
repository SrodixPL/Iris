#pragma once
#include <cmath>

namespace iris::math {
struct Vector { float x{},y{},z{}; };
inline constexpr float degrees=57.29577951308232f;
inline bool finite(Vector v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline Vector operator+(Vector a,Vector b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vector operator-(Vector a,Vector b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vector operator*(Vector a,float b) {return {a.x*b,a.y*b,a.z*b};}
inline float length(Vector a) {return std::hypot(a.x,a.y,a.z);}
inline Vector normalized(Vector a) {const auto n=length(a);return n>0?a*(1/n):Vector{};}
}
