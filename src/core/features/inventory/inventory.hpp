#pragma once
#include "core/features/movement/settings.hpp"
#include "core/features/combat/settings.hpp"
#include "core/features/combat/aim_settings.hpp"
#include "core/features/player/visual_settings.hpp"
#include "core/features/world/esp_settings.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "core/sdk/preview_camera.hpp"

namespace iris::cosmetics {
struct Finish {
    int definition, paint;
    const char* weapon;
    const char* name;
    float minimum_wear, maximum_wear;
    bool legacy;
};
struct Agent { int definition; const char* name; const char* model; unsigned teams; };
std::span<const Agent> agents() noexcept;
const Agent* find_agent(int definition) noexcept;
struct MusicKit { int id; const char* name; const char* image; };
std::span<const MusicKit> music_kits() noexcept;
const MusicKit* find_music_kit(int id) noexcept;
struct Environment {
    std::string skybox_material;
    bool world_override{}, lighting_override{}, bloom_override{}, dof_override{}, wetness_override{}, smoke_override{};
    bool remove_fog{}, remove_shadows{}, remove_sky{}, remove_3d_sky{}, remove_flash{}, remove_smoke{}, remove_ragdolls{};
    float exposure{}, sky_brightness{1}, lighting_intensity{1}, bloom{.5f}, wetness{1}, fog_start{}, fog_falloff{1};
    float dof_near_blurry{}, dof_near_crisp{20}, dof_far_crisp{1500}, dof_far_blurry{3000};
    std::array<float,3> world_color{1,1,1}, lighting_color{1,1,1}, smoke_color{1,1,1};
    bool enabled{}, sky{}, fog{};
    std::array<float,3> sky_color{1,1,1}, fog_color{.6f,.6f,.6f};
    float ambient{1}, fog_density{.35f}, fog_distance{1800}, wind{};
    int weather{},skybox{};float weather_intensity{.5f};
    bool operator==(const Environment&) const = default;
};
// Map-owned controllers are restored when effects are disabled or stopped.
void submit_environment(const Environment& environment);
struct Weapon { int definition; const char* name; const char* model; bool knife; unsigned teams; bool glove{}; };
struct Selection {
    int definition{7};
    int paint{};
    int seed{};
    float wear{.01f};
    int stattrak{-1};
    std::array<char, 161> name{};
    bool enabled{true};
    bool operator==(const Selection&) const = default;
};
inline constexpr std::array<const char*,2> crosshair_modes{"Default","While Scoped"};
struct HudSettings {
    bool scope_lines{},scope_hide{},inaccuracy_overlay{};
    int crosshair_mode{};
    float scope_gap{48},scope_length{70},scope_thickness{1.5f};
    std::array<float,4> scope_color{1,1,1,1},crosshair_color{1,1,1,1};
    bool operator==(const HudSettings&) const=default;
};
struct Configuration {
    bool enabled{true}, thirdperson{}, bhop{}, no_spread{};
    nospread::Settings no_spread_settings;
    combat::Settings combat_settings;
    visuals::Settings player_visuals;
    world::esp::Settings world_esp;
    bool strafe_assist{}, auto_accept{}, preserve_killfeed{};
    float strafe_strength{.65f};
    movement::Options movement_options;
    HudSettings hud;
    bool custom_fov{}, scope_fov_override{}, viewmodel_override{}, viewmodel_fov_override{};
    float scope_fov{90};
    float thirdperson_distance{120};
    float fov{90}, viewmodel_x{1}, viewmodel_y{1}, viewmodel_z{-1}, viewmodel_fov{60};
    std::array<std::string, 2> custom_models;
    std::uint64_t revision{};
    // Index 0 is T (team 2); index 1 is CT (team 3).
    std::array<std::vector<Selection>, 2> loadouts;
    std::array<Selection, 2> gloves;
    std::array<int, 2> agents{};
    int music_kit{};
    Configuration() { for (auto& glove : gloves) { glove.definition = 0; glove.enabled = false; } }
};
struct EquippedSlot { int slot{}, definition{}; bool operator==(const EquippedSlot&) const = default; };
struct EquippedLoadout {
    bool available{};
    std::uint64_t revision{};
    std::string detail;
    std::array<std::vector<EquippedSlot>, 2> teams;
};
EquippedLoadout equipped_loadout();
struct PreviewImage {
    Selection selection;
    std::uint64_t revision{};
    unsigned width{}, height{};
    std::vector<unsigned char> rgba;
    std::string detail;
    bool interactive{};
};
using PreviewPose=sdk::preview::CameraControl;
struct PreviewRequest {Selection selection;PreviewPose pose;bool operator==(const PreviewRequest&) const=default;};
// Transient, debounced rendering of an owned item. Never commits a loadout.
void request_preview(const Selection& selection,const PreviewPose& pose={});
PreviewImage preview_image(std::uint64_t after_revision = 0);
struct Status {
    bool available{};
    std::string detail{"Standalone preview: game runtime is not loaded."};
    std::uint64_t applied_revision{};
    unsigned updated_weapons{};
    int team{};
};
std::span<const Finish> catalog() noexcept;
std::span<const Weapon> weapons() noexcept;
const Weapon* find_weapon(int definition) noexcept;
bool is_knife(int definition) noexcept;
bool is_glove(int definition) noexcept;
const Selection* selection_for(const Configuration&, int team, int definition) noexcept;
const Finish* find_finish(int definition, int paint) noexcept;
bool matches(const Finish& finish, const char* query) noexcept;
bool valid(const Selection& selection) noexcept;
void validate_configuration(const Configuration& configuration);
// Configuration crosses from the render thread to the game update thread.
void start() noexcept;
void submit(const Configuration& configuration);
Status status();
enum class InventoryAcceptance { Pending, Accepted, Rejected };
struct InventoryReceipt {
    std::uint64_t revision{};
    InventoryAcceptance acceptance{InventoryAcceptance::Pending};
    std::string detail;
};
// Worker-only acknowledgement. Native work runs on the game thread, including
// main-menu frames; a settings submission alone does not accept inventory work.
InventoryReceipt await_inventory(std::uint64_t revision,unsigned timeout_ms);
void stop() noexcept;
}
