#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace iris::visuals {
enum class EspElement : unsigned char {Name,Health,HealthNumber,Ammo,Armor,Weapon,Distance,Flags,Count};
enum class EspSide : unsigned char {Top,Bottom,Left,Right};
inline constexpr std::size_t esp_element_count=static_cast<std::size_t>(EspElement::Count);
inline constexpr std::array<const char*,esp_element_count> esp_element_keys{"name","health","health_number","ammo","armor","weapon","distance","flags"};
inline constexpr std::array<const char*,4> esp_side_names{"Top","Bottom","Left","Right"};
enum class EspAnchor : unsigned char {Automatic,Start,Center,End};
struct EspPlacement {EspSide side{};unsigned char order{};EspAnchor anchor{};bool operator==(const EspPlacement&) const = default;};
using EspLayout=std::array<EspPlacement,esp_element_count>;
inline constexpr EspLayout default_esp_layout{{{EspSide::Top,0},{EspSide::Left,0},{EspSide::Left,1},{EspSide::Bottom,0},{EspSide::Bottom,1},{EspSide::Bottom,2},{EspSide::Bottom,3},{EspSide::Right,0}}};
inline void normalize_esp_layout(EspLayout& layout) {
    for(auto& p:layout){if(static_cast<unsigned>(p.side)>3)p.side=EspSide::Bottom;if(static_cast<unsigned>(p.anchor)>3)p.anchor=EspAnchor::Automatic;}
    for(unsigned side=0;side<4;++side){std::array<std::size_t,esp_element_count> ids{};std::size_t count{};
        for(std::size_t i=0;i<layout.size();++i)if(static_cast<unsigned>(layout[i].side)==side)ids[count++]=i;
        std::sort(ids.begin(),ids.begin()+count,[&](auto a,auto b){return layout[a].order==layout[b].order?a<b:layout[a].order<layout[b].order;});
        for(std::size_t i=0;i<count;++i)layout[ids[i]].order=static_cast<unsigned char>(i);
    }
}
inline void move_esp_element(EspLayout& layout,EspElement element,EspSide side,unsigned position) {
    const auto id=static_cast<std::size_t>(element);if(id>=layout.size()||static_cast<unsigned>(side)>3)return;
    normalize_esp_layout(layout);
    const auto old=layout[id];for(std::size_t i=0;i<layout.size();++i)if(i!=id&&layout[i].side==old.side&&layout[i].order>old.order)--layout[i].order;
    unsigned count{};for(std::size_t i=0;i<layout.size();++i)if(i!=id&&layout[i].side==side)++count;
    position=std::min(position,count);for(std::size_t i=0;i<layout.size();++i)if(i!=id&&layout[i].side==side&&layout[i].order>=position)++layout[i].order;
    layout[id]={side,static_cast<unsigned char>(position),old.anchor};
}
struct TargetEsp {
    bool enabled{},box{true},name{true},distance{},health{true},ammo{},skeleton{},hitboxes{},health_number{},armor{},snapline{},weapon{true},visible_only{};
    std::array<bool,9> flags{true,true,true,true,true,true,false,true,false};
    EspLayout layout{default_esp_layout};
    bool operator==(const TargetEsp&) const=default;
};
struct Settings {
    bool enabled{}, teammates{}, visible_only{}, box{true}, name{true}, distance{}, health{true}, ammo{}, skeleton{}, arrows{}, glow{}, hitboxes{}, health_number{}, armor{}, snapline{}, weapon{true};
    bool sounds{},chams{},chams_visible{true},chams_visible_only{},box_visible_only{};
    int box_style{}, weapon_mode{},chams_material{};
    float max_distance{150}, thickness{1}, arrow_size{12},box_opacity{1},chams_opacity{1},box_max_distance{150},chams_max_distance{150};
    std::array<bool,9> flags{true,true,true,true,true,true,false,true,false};
    std::array<float,4> enemy_color{1,.35f,.38f,1}, ally_color{.35f,.7f,1,1}, hidden_color{1,.65f,.3f,.85f};
    std::array<float,4> box_enemy_color{1,.35f,.38f,1},box_ally_color{.35f,.7f,1,1},box_hidden_color{1,.65f,.3f,.85f};
    std::array<float,4> chams_color{1,.35f,.38f,1},chams_hidden_color{1,.65f,.3f,.85f};
    std::array<float,4> name_color{1,1,1,1},health_number_color{1,1,1,1},weapon_color{1,1,1,1},distance_color{1,1,1,1};
    std::array<float,4> health_low_color{1,0,.2f,1},health_full_color{0,1,.2f,1},armor_color{125/255.f,175/255.f,1,1};
    // White tints preserve the existing team/visibility palette in older profiles.
    std::array<float,4> ammo_color{1,1,1,1},flags_color{1,1,1,1},snapline_color{1,1,1,1},skeleton_color{1,1,1,1},hitbox_color{1,1,1,1},sound_color{1,1,1,1};
    std::array<float,4> arrow_color{1,.35f,.38f,1};
    EspLayout esp_layout{default_esp_layout};
    bool separate_teammate_esp{};
    TargetEsp teammate_esp;
    bool operator==(const Settings&) const = default;
};
struct EspToggle {const char* key;const char* label;bool Settings::*shared;bool TargetEsp::*target;};
inline constexpr std::array esp_toggles{
    EspToggle{"enabled","Enabled",&Settings::enabled,&TargetEsp::enabled},
    EspToggle{"box","Bounding box",&Settings::box,&TargetEsp::box},EspToggle{"name","Name",&Settings::name,&TargetEsp::name},
    EspToggle{"health","Health bar",&Settings::health,&TargetEsp::health},EspToggle{"health_number","Health number",&Settings::health_number,&TargetEsp::health_number},
    EspToggle{"ammo","Ammo bar",&Settings::ammo,&TargetEsp::ammo},EspToggle{"armor","Armor bar",&Settings::armor,&TargetEsp::armor},
    EspToggle{"weapon","Weapon",&Settings::weapon,&TargetEsp::weapon},EspToggle{"distance","Distance",&Settings::distance,&TargetEsp::distance},
    EspToggle{"snapline","Snapline",&Settings::snapline,&TargetEsp::snapline},EspToggle{"skeleton","Skeleton",&Settings::skeleton,&TargetEsp::skeleton},
    EspToggle{"hitboxes","Hitboxes",&Settings::hitboxes,&TargetEsp::hitboxes},EspToggle{"visible_only","Visible only",&Settings::visible_only,&TargetEsp::visible_only}};
inline TargetEsp teammate_options(const Settings& s){
    if(s.separate_teammate_esp)return s.teammate_esp;
    TargetEsp out;for(const auto& field:esp_toggles)out.*field.target=s.*field.shared;
    out.enabled=s.enabled&&s.teammates;out.flags=s.flags;out.layout=s.esp_layout;return out;
}
inline bool any_esp_enabled(const Settings& s){return s.enabled||teammate_options(s).enabled;}
inline Settings for_target(const Settings& s,bool enemy){
    auto out=s;if(!enemy){const auto target=teammate_options(s);for(const auto& field:esp_toggles)out.*field.shared=target.*field.target;out.flags=target.flags;out.esp_layout=target.layout;}
    return out;
}
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
    Color{"chams_color",&Settings::chams_color},Color{"chams_hidden_color",&Settings::chams_hidden_color},
    Color{"name_color",&Settings::name_color},Color{"health_low_color",&Settings::health_low_color},Color{"health_full_color",&Settings::health_full_color},
    Color{"health_number_color",&Settings::health_number_color},Color{"ammo_color",&Settings::ammo_color},Color{"armor_color",&Settings::armor_color},
    Color{"weapon_color",&Settings::weapon_color},Color{"distance_color",&Settings::distance_color},Color{"flags_color",&Settings::flags_color},
    Color{"snapline_color",&Settings::snapline_color},Color{"skeleton_color",&Settings::skeleton_color},Color{"hitbox_color",&Settings::hitbox_color},
    Color{"arrow_color",&Settings::arrow_color},Color{"sound_color",&Settings::sound_color}};
inline std::array<float,4> tint(const std::array<float,4>& base,const std::array<float,4>& element) {
    return {base[0]*element[0],base[1]*element[1],base[2]*element[2],base[3]*element[3]};
}
inline std::array<float,4> health_color(const Settings& s,float fraction) {
    fraction=std::clamp(fraction,0.f,1.f);std::array<float,4> result{};
    for(std::size_t i=0;i<result.size();++i)result[i]=s.health_low_color[i]+(s.health_full_color[i]-s.health_low_color[i])*fraction;
    return result;
}
inline void validate(const Settings& s) {
    const auto range=[](float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
    if(s.box_style<0||s.box_style>1||s.weapon_mode<0||s.weapon_mode>2||s.chams_material<0||s.chams_material>4||!range(s.box_opacity,0,1)||!range(s.chams_opacity,0,1)||!range(s.box_max_distance,5,500)||!range(s.chams_max_distance,5,500)||!range(s.max_distance,5,500)||!range(s.thickness,1,3)||!range(s.arrow_size,6,24))throw std::invalid_argument("Player visual setting is outside its supported range.");
    for(const auto& c:colors)for(float v:s.*c.member)if(!range(v,0,1))throw std::invalid_argument("Player visual color is invalid.");
    for(const auto& p:s.esp_layout)if(static_cast<unsigned>(p.side)>3||p.order>=esp_element_count||static_cast<unsigned>(p.anchor)>3)throw std::invalid_argument("ESP element placement is invalid.");
    for(const auto& p:s.teammate_esp.layout)if(static_cast<unsigned>(p.side)>3||p.order>=esp_element_count||static_cast<unsigned>(p.anchor)>3)throw std::invalid_argument("Teammate ESP placement is invalid.");
    auto teammate=s.teammate_esp.layout;normalize_esp_layout(teammate);if(teammate!=s.teammate_esp.layout)throw std::invalid_argument("Teammate ESP order is invalid.");
    auto canonical=s.esp_layout;normalize_esp_layout(canonical);if(canonical!=s.esp_layout)throw std::invalid_argument("ESP element order is invalid.");
}
}
