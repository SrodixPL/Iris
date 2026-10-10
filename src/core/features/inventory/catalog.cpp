#include "core/features/inventory/inventory.hpp"
#include "core/features/player/models.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace iris::cosmetics {
namespace {
constexpr Finish finishes[]{
#include "core/sdk/generated/skin_catalog.inc"
};
constexpr Agent agent_catalog[]{
#include "core/sdk/generated/agent_catalog.inc"
};
constexpr Weapon weapon_catalog[]{
#include "core/sdk/generated/weapon_catalog.inc"
};
bool contains(const char* text, const char* query) noexcept {
    const auto size = std::strlen(query);
    for (; *text; ++text) {
        std::size_t i = 0;
        while (i < size && text[i] && std::tolower(static_cast<unsigned char>(text[i])) == std::tolower(static_cast<unsigned char>(query[i]))) ++i;
        if (i == size) return true;
    }
    return size == 0;
}
}
std::span<const Agent> agents() noexcept { return agent_catalog; }
const Agent* find_agent(int definition) noexcept { for (const auto& agent : agent_catalog) if (agent.definition == definition) return &agent; return nullptr; }
std::span<const Finish> catalog() noexcept { return finishes; }
std::span<const Weapon> weapons() noexcept { return weapon_catalog; }
const Weapon* find_weapon(int definition) noexcept {
    for (const auto& weapon : weapon_catalog) if (weapon.definition == definition) return &weapon;
    return nullptr;
}
bool is_knife(int definition) noexcept { const auto* weapon = find_weapon(definition); return weapon && weapon->knife; }
bool is_glove(int definition) noexcept { const auto* weapon = find_weapon(definition); return weapon && weapon->glove; }
const Selection* selection_for(const Configuration& configuration, int team, int definition) noexcept {
    if (team != 2 && team != 3) return nullptr;
    for (const auto& selection : configuration.loadouts[team - 2])
        if (selection.enabled && (selection.definition == definition || (is_knife(definition) && is_knife(selection.definition)))) return &selection;
    return nullptr;
}
const Finish* find_finish(int definition, int paint) noexcept {
    for (const auto& finish : finishes) if (finish.definition == definition && finish.paint == paint) return &finish;
    return nullptr;
}
bool matches(const Finish& finish, const char* query) noexcept {
    return !*query || contains(finish.name, query) || contains(finish.weapon, query);
}
bool valid(const Selection& selection) noexcept {
    const auto* finish = find_finish(selection.definition, selection.paint);
    return find_weapon(selection.definition) && (finish || selection.paint == 0) && std::isfinite(selection.wear) && selection.wear >= (finish ? finish->minimum_wear : 0.f) &&
        selection.wear <= (finish ? finish->maximum_wear : 1.f) && selection.seed >= 0 && selection.seed <= 1000 &&
        selection.stattrak >= -1 && selection.name.back() == '\0';
}
void validate_configuration(const Configuration& configuration) {
    visuals::validate(configuration.player_visuals);
    combat::validate(configuration.combat_settings);
    movement::validate(configuration.movement_options);
    const auto bounded=[](float value,float low,float high){return std::isfinite(value) && value>=low && value<=high;};
    if(!bounded(configuration.strafe_strength,0,1) ||
       !bounded(configuration.fov,60,140) || !bounded(configuration.viewmodel_fov,30,120) ||
       !bounded(configuration.viewmodel_x,-10,10) || !bounded(configuration.viewmodel_y,-10,10) || !bounded(configuration.viewmodel_z,-10,10))
        throw std::invalid_argument("Movement or camera setting is outside its supported range.");
    const auto& rage=configuration.no_spread_settings;
    if(!bounded(rage.fov,1,180))throw std::invalid_argument("Aim FOV must be between 1 and 180 degrees.");
    if(!bounded(rage.minimum_damage,1,130)||!bounded(rage.hit_chance,0,100)||!bounded(rage.multipoint_scale,0.1f,1)||!bounded(rage.shot_delay,0,1000))throw std::invalid_argument("Rage setting is outside its supported range.");
    const auto& hud=configuration.hud;
    if(hud.crosshair_mode<0||hud.crosshair_mode>=static_cast<int>(crosshair_modes.size())||!bounded(hud.scope_gap,0,240)||!bounded(hud.scope_length,0,4000)||!bounded(hud.scope_thickness,0.5f,8))throw std::invalid_argument("HUD setting is outside its supported range.");
    for(float component:hud.scope_color)if(!bounded(component,0,1))throw std::invalid_argument("Scope color is outside its supported range.");
    for(float component:hud.crosshair_color)if(!bounded(component,0,1))throw std::invalid_argument("Crosshair color is outside its supported range.");
    for(const auto& resource:configuration.custom_models)
        if(!resource.empty() && !models::valid_resource_path(resource))throw std::invalid_argument("Custom models must use a relative models/*.vmdl resource path.");
    for (int team = 0; team < 2; ++team) {
        const auto* agent = find_agent(configuration.agents[team]);
        if (configuration.agents[team] && (!agent || !(agent->teams & (1u << team))))
            throw std::invalid_argument("Invalid agent model for this team.");
        const auto& glove = configuration.gloves[team];
        if (glove.enabled && (!valid(glove) || !is_glove(glove.definition) ||
            !(find_weapon(glove.definition)->teams & (1u << team)) || glove.stattrak != -1 || glove.name[0]))
            throw std::invalid_argument("Invalid glove selection for this team.");
        bool knife = false;
        std::vector<int> definitions;
        for (const auto& selection : configuration.loadouts[team]) {
            const auto* weapon = find_weapon(selection.definition);
            if (!valid(selection) || !weapon || weapon->glove ||
                (weapon->knife && !(weapon->teams & (1u << team))))
                throw std::invalid_argument("Invalid cosmetic selection for this team.");
            if (std::find(definitions.begin(), definitions.end(), selection.definition) != definitions.end())
                throw std::invalid_argument("Duplicate loadout weapon.");
            definitions.push_back(selection.definition);
            if (weapon->knife) { if (knife) throw std::invalid_argument("Duplicate loadout knife."); knife = true; }
        }
    }
}
}
