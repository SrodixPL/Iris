#pragma once
#include <array>
#include <cmath>
#include <stdexcept>

namespace iris::world::esp {
struct Settings {
    bool weapons{},hostages{},grenades{},bomb{};
    std::array<float,4> weapons_color{1,1,1,1},hostages_color{1,1,1,1},grenades_color{1,1,1,1},bomb_color{1,1,1,1};
    bool active() const noexcept {return weapons||hostages||grenades||bomb;}
    bool operator==(const Settings&) const = default;
};
struct Toggle {const char* key;const char* label;bool Settings::*member;};
inline constexpr std::array toggles{
    Toggle{"weapons","Dropped weapon labels",&Settings::weapons},Toggle{"hostages","Hostage labels",&Settings::hostages},
    Toggle{"grenades","Grenade projectile labels",&Settings::grenades},Toggle{"bomb","Bomb location label",&Settings::bomb}};
struct Color {const char* key;const char* label;std::array<float,4> Settings::*member;};
inline constexpr std::array colors{
    Color{"weapons_color","Dropped weapons",&Settings::weapons_color},Color{"hostages_color","Hostages",&Settings::hostages_color},
    Color{"grenades_color","Grenades",&Settings::grenades_color},Color{"bomb_color","Bomb location",&Settings::bomb_color}};
inline void validate(const Settings& settings){
    for(const auto& color:colors)for(float channel:settings.*color.member)
        if(!std::isfinite(channel)||channel<0||channel>1)throw std::invalid_argument("World ESP color is outside its supported range.");
}
}
