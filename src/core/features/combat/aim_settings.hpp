#pragma once
#include "core/features/combat/settings.hpp"
#include <cmath>
#include <stdexcept>
#include <span>
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
namespace iris::combat {
struct LegitSettings {
    bool enabled{},on_fire{true},target_lock{true},visualize_fov{},recoil_control{},standalone_recoil{true},trigger{},scoped_only{},pause_flashed{true},confirmation_delay{};
    std::array<bool,6> hitboxes{true,true,false,false,false,false};
    std::array<bool,6> trigger_hitboxes{true,true,true,true,true,true};
    std::array<bool,6> multipoint{};
    bool only_body{},prefer_head{},prefer_body{},trigger_flash{true},trigger_air{};
    float trigger_interval{},trigger_reaction{},trigger_minimum_damage{20};
    int trigger_visibility{};
    // Zero preserves the pre-key behavior; a bound virtual key is a hold gate.
    int aim_key{};
    int radius_type{},smoothing_type{1},target_selection{2};
    float aim_time{},smooth_pitch{100},smooth_yaw{100};
    float fov{5},smoothing{.18f},max_speed{180},reaction_time{100},recoil_strength{1},shot_delay{},sniper_delay{120},recoil_horizontal{1},recoil_vertical{1};
    float effective_fov(float camera_fov) const noexcept {
        if(radius_type!=1||!std::isfinite(camera_fov)||camera_fov<=1||camera_fov>=179)return fov;
        constexpr float radians=.017453292519943295f;
        return 2.f*std::atan(std::tan(fov*radians*.5f)*std::tan(camera_fov*radians*.5f))/radians;
    }
    bool operator==(const LegitSettings&) const=default;
};
enum class LegitCategory : std::size_t { Pistols,HeavyPistols,Rifles,Smgs,Shotguns,Snipers,MachineGuns,Count };
inline constexpr const char* legit_category_names[]{"Pistols","Heavy pistols","Rifles","SMGs","Shotguns","Snipers","Machine guns"};
struct LegitWeapon {int definition;const char* name;LegitCategory category;};
// Definition identities are already present in the shipped schema catalog.
// This classification adds no native address, layout or weapon behavior.
inline constexpr LegitWeapon legit_weapons[]{
    {1,"Desert Eagle",LegitCategory::HeavyPistols},{2,"Dual Berettas",LegitCategory::Pistols},
    {3,"Five-SeveN",LegitCategory::Pistols},{4,"Glock-18",LegitCategory::Pistols},
    {7,"AK-47",LegitCategory::Rifles},{8,"AUG",LegitCategory::Rifles},{9,"AWP",LegitCategory::Snipers},
    {10,"FAMAS",LegitCategory::Rifles},{11,"G3SG1",LegitCategory::Snipers},{13,"Galil AR",LegitCategory::Rifles},
    {14,"M249",LegitCategory::MachineGuns},{16,"M4A4",LegitCategory::Rifles},{17,"MAC-10",LegitCategory::Smgs},
    {19,"P90",LegitCategory::Smgs},{23,"MP5-SD",LegitCategory::Smgs},{24,"UMP-45",LegitCategory::Smgs},
    {25,"XM1014",LegitCategory::Shotguns},{26,"PP-Bizon",LegitCategory::Smgs},{27,"MAG-7",LegitCategory::Shotguns},
    {28,"Negev",LegitCategory::MachineGuns},{29,"Sawed-Off",LegitCategory::Shotguns},{30,"Tec-9",LegitCategory::Pistols},
    {32,"P2000",LegitCategory::Pistols},{33,"MP7",LegitCategory::Smgs},{34,"MP9",LegitCategory::Smgs},
    {35,"Nova",LegitCategory::Shotguns},{36,"P250",LegitCategory::Pistols},{38,"SCAR-20",LegitCategory::Snipers},
    {39,"SG 553",LegitCategory::Rifles},{40,"SSG 08",LegitCategory::Snipers},{60,"M4A1-S",LegitCategory::Rifles},
    {61,"USP-S",LegitCategory::Pistols},{63,"CZ75-Auto",LegitCategory::Pistols},{64,"R8 Revolver",LegitCategory::HeavyPistols}};
inline const LegitWeapon* legit_weapon(int definition) noexcept {
    for(const auto& weapon:legit_weapons)if(weapon.definition==definition)return &weapon;
    return nullptr;
}
struct LegitOverride {
    bool enabled{};
    LegitSettings settings;
    bool operator==(const LegitOverride&) const=default;
};
struct LegitWeaponOverride {
    int definition{};
    LegitOverride value;
    bool operator==(const LegitWeaponOverride&) const=default;
};
struct LegitProfileSet {
    std::uint32_t id{1};
    std::string name{"Default"};
    LegitSettings global;
    std::array<LegitOverride,static_cast<std::size_t>(LegitCategory::Count)> categories;
    std::vector<LegitWeaponOverride> weapons;
    const LegitSettings& resolve(int definition) const noexcept {
        const LegitSettings* result=&global;
        if(const auto* weapon=legit_weapon(definition)) {
            const auto& category=categories[static_cast<std::size_t>(weapon->category)];
            if(category.enabled)result=&category.settings;
            for(const auto& item:weapons)if(item.definition==definition&&item.value.enabled){result=&item.value.settings;break;}
        }
        return *result;
    }
    bool operator==(const LegitProfileSet&) const=default;
};
struct Settings {
    // Retained for old profiles. Empty profile_sets keeps their existing behavior.
    LegitSettings legit;
    bool master_enabled{};
    std::uint32_t selected_profile{1};
    std::uint32_t next_profile_id{2};
    std::vector<LegitProfileSet> profile_sets;
    const LegitProfileSet* selected_set() const noexcept {
        for(const auto& set:profile_sets)if(set.id==selected_profile)return &set;
        for(const auto& set:profile_sets)if(set.id==1)return &set;
        return nullptr;
    }
    LegitProfileSet* selected_set() noexcept {return const_cast<LegitProfileSet*>(std::as_const(*this).selected_set());}
    LegitSettings resolve(int definition) const noexcept {
        const auto* set=selected_set();auto out=set?set->resolve(definition):legit;
        if(!profile_sets.empty()&&!master_enabled)out.enabled=out.trigger=out.recoil_control=out.visualize_fov=false;
        return out;
    }
    bool active() const noexcept {
        const auto active=[](const LegitSettings& s){return s.enabled||s.trigger||s.recoil_control;};
        if(profile_sets.empty())return active(legit);
        if(!master_enabled)return false;
        const auto* set=selected_set();if(!set)return false;
        if(active(set->global))return true;
        for(const auto& category:set->categories)if(category.enabled&&active(category.settings))return true;
        for(const auto& weapon:set->weapons)if(weapon.value.enabled&&active(weapon.value.settings))return true;
        return false;
    }
    void initialize_profiles() {
        if(!profile_sets.empty())return;
        LegitProfileSet set;set.global=legit;for(auto& category:set.categories)category.settings=legit;
        profile_sets.push_back(std::move(set));selected_profile=1;
        master_enabled=legit.enabled||legit.trigger||legit.recoil_control||legit.visualize_fov;
    }
    bool add_profile() {
        initialize_profiles();if(profile_sets.size()>=32)return false;
        if(next_profile_id<2||next_profile_id==UINT32_MAX)return false;
        LegitProfileSet set;set.id=next_profile_id++;
        for(unsigned n=1;;++n){set.name="Profile "+std::to_string(n);if(std::none_of(profile_sets.begin(),profile_sets.end(),[&](const auto& existing){return existing.name==set.name;}))break;}
        selected_profile=set.id;profile_sets.push_back(std::move(set));return true;
    }
    bool delete_profile(std::uint32_t id) {
        if(id==1)return false;
        const auto found=std::find_if(profile_sets.begin(),profile_sets.end(),[&](const auto& set){return set.id==id;});
        if(found==profile_sets.end())return false;
        profile_sets.erase(found);if(selected_profile==id)selected_profile=1;return true;
    }
    bool operator==(const Settings&) const=default;
};
template<class S> struct Toggle {const char* key;const char* label;bool S::*member;};
template<class S> struct Number {const char* key;const char* label;float S::*member;float low,high;const char* format;};
template<class S> struct Choice {const char* key;const char* label;int S::*member;std::span<const char* const> names;};
inline constexpr const char* legit_visibility_names[]{"Visible only","Allow penetration"};
inline constexpr const char* legit_radius_names[]{"Static angle","Dynamic screen radius (90 deg reference)"};
inline constexpr const char* legit_smoothing_names[]{"Linear","Exponential"};
inline constexpr const char* legit_selection_names[]{"Hit chance","Highest damage","Nearest to crosshair"};
inline constexpr Choice<LegitSettings> legit_choices[]{
    {"trigger_visibility","Trigger visibility",&LegitSettings::trigger_visibility,legit_visibility_names},
    {"radius_type","Radius type",&LegitSettings::radius_type,legit_radius_names},
    {"smoothing_type","Smooth type",&LegitSettings::smoothing_type,legit_smoothing_names},
    {"target_selection","Target selection",&LegitSettings::target_selection,legit_selection_names}};
inline constexpr Toggle<LegitSettings> legit_toggles[]{
    {"enabled","Aim assistance",&LegitSettings::enabled},{"on_fire","Aim while holding fire",&LegitSettings::on_fire},
    {"target_lock","Keep current target",&LegitSettings::target_lock},
    {"visualize_fov","Visualize FOV",&LegitSettings::visualize_fov},{"recoil_control","Recoil correction",&LegitSettings::recoil_control},
    {"standalone_recoil","Without aim target",&LegitSettings::standalone_recoil},
    {"trigger","Fire when crosshair hits an enemy",&LegitSettings::trigger},
    {"scoped_only","Snipers: require scope",&LegitSettings::scoped_only},
    {"pause_flashed","Pause assistance while flashed",&LegitSettings::pause_flashed},
    {"confirmation_delay","Shot confirmation",&LegitSettings::confirmation_delay},
    {"only_body","Only body aim",&LegitSettings::only_body},
    {"prefer_head","Prefer head",&LegitSettings::prefer_head},
    {"prefer_body","Prefer body",&LegitSettings::prefer_body},
    {"trigger_flash","Trigger: flash check",&LegitSettings::trigger_flash},
    {"trigger_air","Trigger: air check",&LegitSettings::trigger_air}};
inline constexpr Number<LegitSettings> legit_numbers[]{
    {"fov","FOV",&LegitSettings::fov,1,30,"%.0f deg"},
    {"smoothing","Aim smoothing",&LegitSettings::smoothing,.05f,1,"%.2f s"},
    {"max_speed","Turn speed (deg/s)",&LegitSettings::max_speed,30,720,"%.0f"},
    {"reaction_time","Target acquisition delay",&LegitSettings::reaction_time,0,500,"%.0f ms"},
    {"recoil_strength","Overall amount",&LegitSettings::recoil_strength,0,1,"%.2f"},
    {"shot_delay","Shot confirmation delay",&LegitSettings::shot_delay,0,500,"%.0f ms"},
    {"sniper_delay","Sniper confirmation delay",&LegitSettings::sniper_delay,0,500,"%.0f ms"},
    {"recoil_horizontal","Side-to-side correction",&LegitSettings::recoil_horizontal,0,1,"%.2f"},
    {"recoil_vertical","Up-and-down correction",&LegitSettings::recoil_vertical,0,1,"%.2f"},
    {"trigger_interval","Delay between shots",&LegitSettings::trigger_interval,0,1000,"%.0f ms"},
    {"trigger_reaction","Trigger reaction time",&LegitSettings::trigger_reaction,0,1000,"%.0f ms"},
    {"trigger_minimum_damage","Trigger minimum damage",&LegitSettings::trigger_minimum_damage,0,130,"%.0f HP"},
    {"aim_time","Aim duration (0 = unlimited)",&LegitSettings::aim_time,0,1000,"%.0f ms"},
    {"smooth_pitch","Smooth pitch (0 = disabled)",&LegitSettings::smooth_pitch,0,100,"%.0f%%"},
    {"smooth_yaw","Smooth yaw (0 = disabled)",&LegitSettings::smooth_yaw,0,100,"%.0f%%"}};
inline void validate(const Settings& s) {
    const auto check=[](const LegitSettings& value){if(value.aim_key<0||value.aim_key>255)throw std::invalid_argument("Legit aim key is outside its supported range.");for(const auto& n:legit_numbers)if(!std::isfinite(value.*n.member)||value.*n.member<n.low||value.*n.member>n.high)throw std::invalid_argument("Legit aim setting is outside its supported range.");for(const auto& c:legit_choices)if(value.*c.member<0||value.*c.member>=static_cast<int>(c.names.size()))throw std::invalid_argument("Legit choice is outside its supported range.");};
    check(s.legit);
    if(s.profile_sets.size()>32)throw std::invalid_argument("Too many Legit profile sets.");
    bool has_default=s.profile_sets.empty(),has_selected=s.profile_sets.empty();
    std::vector<std::uint32_t> ids;std::vector<std::string> names;
    for(const auto& set:s.profile_sets){
        if(!set.id||set.id>=s.next_profile_id||set.name.empty()||set.name.size()>63||std::all_of(set.name.begin(),set.name.end(),[](unsigned char c){return c==' '||c=='\t';})||std::any_of(set.name.begin(),set.name.end(),[](unsigned char c){return c<32||c==127;})||std::find(ids.begin(),ids.end(),set.id)!=ids.end()||std::find(names.begin(),names.end(),set.name)!=names.end())throw std::invalid_argument("Legit profile identities or names are invalid.");
        ids.push_back(set.id);names.push_back(set.name);has_default|=set.id==1;has_selected|=set.id==s.selected_profile;
        if(set.id==1&&set.name!="Default")throw std::invalid_argument("The Default Legit profile cannot be renamed.");
        check(set.global);for(const auto& category:set.categories)check(category.settings);
        if(set.weapons.size()>std::size(legit_weapons))throw std::invalid_argument("Too many Legit weapon overrides.");
        std::vector<int> definitions;
        for(const auto& weapon:set.weapons){if(!legit_weapon(weapon.definition)||std::find(definitions.begin(),definitions.end(),weapon.definition)!=definitions.end())throw std::invalid_argument("Legit weapon override is invalid.");definitions.push_back(weapon.definition);check(weapon.value.settings);}
    }
    if(!has_default||!has_selected||s.next_profile_id<2)throw std::invalid_argument("Legit profile fallback or selection is missing.");
}
}
