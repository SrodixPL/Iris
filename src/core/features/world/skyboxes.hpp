#pragma once
#include <array>
#include <string_view>
namespace iris::cosmetics {
inline bool valid_skybox_material(std::string_view path) noexcept {
    return path.empty() || (path.size()<=255 && path.starts_with("materials/") && path.ends_with(".vmat") &&
        path.find("..")==std::string_view::npos && path.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-/.")==std::string_view::npos);
}
struct Skybox {const char* label;const char* material;};
inline constexpr std::array<Skybox,12> skyboxes{{
    {"Map default",""},
    {"Dust II","materials/skybox/sky_de_dust2.vmat"},
    {"Mirage","materials/skybox/sky_de_mirage.vmat"},
    {"Nuke","materials/skybox/sky_de_nuke.vmat"},
    {"Train","materials/skybox/sky_de_train03.vmat"},
    {"Office","materials/skybox/cs_office_45_0.vmat"},
    {"Vertigo","materials/skybox/sky_de_vertigo.vmat"},
    {"Anubis","materials/skybox/sky_de_annubis.vmat"},
    {"Overcast","materials/skybox/sky_overcast_01.vmat"},
    {"Rainy night","materials/skybox/sky_rain_night_01.vmat"},
    {"Italy","materials/skybox/cs_italy_s2_skybox_2.vmat"},
    {"Aztec","materials/skybox/sky_hr_aztec_02.vmat"},
}};
}
