#pragma once
#include <array>
namespace iris::nospread {
inline constexpr std::array<const char*,6> hitbox_names{"Head","Chest","Stomach","Pelvis","Arms","Legs"};
struct Settings {
    bool auto_fire{true},compensate{true},visualize_fov{},through_walls{},highlight_target{true};
    bool silent_aim{true},remove_recoil{true},multipoint{},quick_stop{},quick_scope{},delay_shot{},health_damage{};
    std::array<bool,6> hitboxes{false,true,false,false,false,false};
    float fov{30},multipoint_scale{.5f},shot_delay{};
    float minimum_damage{1},hit_chance{50};
    bool operator==(const Settings&) const=default;
};
}
