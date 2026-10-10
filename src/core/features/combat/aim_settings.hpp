#pragma once
#include "core/features/combat/settings.hpp"
#include <cmath>
#include <stdexcept>
#include <span>
namespace iris::combat {
struct LegitSettings {
    bool enabled{},on_fire{true},target_lock{true},visualize_fov{},recoil_control{},standalone_recoil{true},trigger{},scoped_only{},pause_flashed{true};
    std::array<bool,6> hitboxes{true,true,false,false,false,false};
    float fov{5},smoothing{.18f},max_speed{180},reaction_time{100},recoil_strength{1},shot_delay{},sniper_delay{120},recoil_horizontal{1},recoil_vertical{1};
    bool operator==(const LegitSettings&) const=default;
};
struct Settings {
    LegitSettings legit;
    bool operator==(const Settings&) const=default;
};
template<class S> struct Toggle {const char* key;const char* label;bool S::*member;};
template<class S> struct Number {const char* key;const char* label;float S::*member;float low,high;const char* format;};
template<class S> struct Choice {const char* key;const char* label;int S::*member;std::span<const char* const> names;};
inline constexpr Toggle<LegitSettings> legit_toggles[]{
    {"enabled","Aim assistance",&LegitSettings::enabled},{"on_fire","Aim while holding fire",&LegitSettings::on_fire},
    {"target_lock","Keep current target",&LegitSettings::target_lock},
    {"visualize_fov","Visualize FOV",&LegitSettings::visualize_fov},{"recoil_control","Recoil correction",&LegitSettings::recoil_control},
    {"standalone_recoil","Without aim target",&LegitSettings::standalone_recoil},
    {"trigger","Fire when crosshair hits an enemy",&LegitSettings::trigger},
    {"scoped_only","Snipers: require scope",&LegitSettings::scoped_only},
    {"pause_flashed","Pause assistance while flashed",&LegitSettings::pause_flashed}};
inline constexpr Number<LegitSettings> legit_numbers[]{
    {"fov","FOV",&LegitSettings::fov,1,30,"%.0f deg"},
    {"smoothing","Aim smoothing",&LegitSettings::smoothing,.05f,1,"%.2f s"},
    {"max_speed","Turn speed (deg/s)",&LegitSettings::max_speed,30,720,"%.0f"},
    {"reaction_time","Target acquisition delay",&LegitSettings::reaction_time,0,500,"%.0f ms"},
    {"recoil_strength","Overall amount",&LegitSettings::recoil_strength,0,1,"%.2f"},
    {"shot_delay","Shot confirmation delay",&LegitSettings::shot_delay,0,500,"%.0f ms"},
    {"sniper_delay","Sniper confirmation delay",&LegitSettings::sniper_delay,0,500,"%.0f ms"},
    {"recoil_horizontal","Side-to-side correction",&LegitSettings::recoil_horizontal,0,1,"%.2f"},
    {"recoil_vertical","Up-and-down correction",&LegitSettings::recoil_vertical,0,1,"%.2f"}};
inline void validate(const Settings& s) {
    for(const auto& n:legit_numbers)if(!std::isfinite(s.legit.*n.member)||s.legit.*n.member<n.low||s.legit.*n.member>n.high)throw std::invalid_argument("Legit aim setting is outside its supported range.");
}
}
