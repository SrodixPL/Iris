#pragma once
#include <array>
#include <cmath>
#include <stdexcept>

namespace iris::visuals {
struct Settings {
    bool enabled{}, teammates{}, visible_only{}, box{true}, name{true}, distance{}, health{true}, ammo{}, skeleton{}, arrows{}, glow{}, hitboxes{}, health_number{}, armor{}, snapline{}, weapon{true};
    bool sounds{},chams{},chams_visible{true},chams_visible_only{},box_visible_only{};
    int box_style{}, weapon_mode{},chams_material{};
    float max_distance{150}, thickness{1}, arrow_size{12},box_opacity{1},chams_opacity{1},box_max_distance{150},chams_max_distance{150};
    std::array<bool,9> flags{true,true,true,true,true,true,false,true,false};
    std::array<float,4> enemy_color{1,.35f,.38f,1}, ally_color{.35f,.7f,1,1}, hidden_color{1,.65f,.3f,.85f};
    std::array<float,4> box_enemy_color{1,.35f,.38f,1},box_ally_color{.35f,.7f,1,1},box_hidden_color{1,.65f,.3f,.85f};
    std::array<float,4> chams_color{1,.35f,.38f,1},chams_hidden_color{1,.65f,.3f,.85f};
    bool operator==(const Settings&) const = default;
};
struct Toggle {const char* key;const char* label;bool Settings::*member;};
inline constexpr std::array toggles{
    Toggle{"enabled","Player visuals",&Settings::enabled},Toggle{"teammates","Include teammates",&Settings::teammates},
    Toggle{"visible_only","Visible only",&Settings::visible_only},Toggle{"box","Bounding box",&Settings::box},
    Toggle{"name","Name",&Settings::name},Toggle{"distance","Distance",&Settings::distance},Toggle{"health","Health bar",&Settings::health},
    Toggle{"ammo","Ammo bar",&Settings::ammo},Toggle{"skeleton","Skeleton",&Settings::skeleton},Toggle{"arrows","Offscreen arrows",&Settings::arrows},
    Toggle{"glow","Glow",&Settings::glow},Toggle{"hitboxes","Hitboxes",&Settings::hitboxes},Toggle{"health_number","Health number",&Settings::health_number},
    Toggle{"armor","Armor bar",&Settings::armor},Toggle{"snapline","Snapline",&Settings::snapline},Toggle{"weapon","Show weapon",&Settings::weapon},Toggle{"sounds","Sound pulses",&Settings::sounds},
    Toggle{"chams","Chams",&Settings::chams},Toggle{"chams_visible","Visible pass",&Settings::chams_visible},
    Toggle{"chams_visible_only","Chams visible only",&Settings::chams_visible_only},Toggle{"box_visible_only","Box visible only",&Settings::box_visible_only}};
inline constexpr std::array<const char*,9> flag_names{"Defusing","Defuse kit","Blind","Scoped","Reloading","Bomb","Money","Hostage","Armor / helmet"};
struct Color {const char* key;std::array<float,4> Settings::*member;};
inline constexpr std::array colors{Color{"enemy_color",&Settings::enemy_color},Color{"ally_color",&Settings::ally_color},Color{"hidden_color",&Settings::hidden_color},
    Color{"box_enemy_color",&Settings::box_enemy_color},Color{"box_ally_color",&Settings::box_ally_color},Color{"box_hidden_color",&Settings::box_hidden_color},
    Color{"chams_color",&Settings::chams_color},Color{"chams_hidden_color",&Settings::chams_hidden_color}};
inline void validate(const Settings& s) {
    const auto range=[](float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
    if(s.box_style<0||s.box_style>1||s.weapon_mode<0||s.weapon_mode>2||s.chams_material<0||s.chams_material>4||!range(s.box_opacity,0,1)||!range(s.chams_opacity,0,1)||!range(s.box_max_distance,5,500)||!range(s.chams_max_distance,5,500)||!range(s.max_distance,5,500)||!range(s.thickness,1,3)||!range(s.arrow_size,6,24))throw std::invalid_argument("Player visual setting is outside its supported range.");
    for(const auto* c:{&s.enemy_color,&s.ally_color,&s.hidden_color,&s.box_enemy_color,&s.box_ally_color,&s.box_hidden_color,&s.chams_color,&s.chams_hidden_color})for(float v:*c)if(!range(v,0,1))throw std::invalid_argument("Player visual color is invalid.");
}
}
