#include "core/menu/feature_switches.hpp"
#include "core/menu/feature_catalog.hpp"
#include "core/menu/legit_profiles.hpp"
#include "core/menu/settings_io.hpp"
#include "ext/json/json.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace iris::ui::feature_switches {
namespace {
using Json=nlohmann::json;
using feature_tree::Kind;
const feature_tree::Node* node(std::string_view path){for(const auto& n:feature_tree::nodes)if(path==n.path)return &n;return nullptr;}
Json parse(const std::string& text){auto j=Json::parse(text,nullptr,false);return j.is_object()?j:Json::object();}
struct Controls {
 Json data;
 explicit Controls(const MenuState& s):data(parse(s.preview_tree)){}
 const Json& item(std::string_view p)const{static const Json empty=Json::object();auto i=data.find(std::string(p));return i!=data.end()&&i->is_object()?*i:empty;}
 bool on(std::string_view p)const{if(!wired(p))return false;const auto& v=item(p);auto i=v.find("enabled");return i!=v.end()&&i->is_boolean()&&i->get<bool>();}
 float number(std::string_view p,float low,float high,float fallback=0)const{const auto* n=node(p);float value=n?n->initial:fallback;const auto& v=item(p);auto i=v.find("number");if(i!=v.end()&&i->is_number()){const double d=i->get<double>();if(std::isfinite(d))value=static_cast<float>(std::clamp(d,double(low),double(high)));}return std::clamp(value,low,high);}
 int choice(std::string_view p,int maximum)const{const auto& v=item(p);auto i=v.find("choice");if(i!=v.end()&&i->is_number_integer()){const auto d=i->get<double>();return static_cast<int>(std::clamp(d,0.,double(maximum)));}return 0;}
 std::array<float,4> color(std::string_view p,std::array<float,4> fallback)const{const auto& v=item(p);auto i=v.find("color");if(i!=v.end()&&i->is_array()&&i->size()==4)for(int n=0;n<4;++n)if((*i)[n].is_number()){const double d=(*i)[n].get<double>();if(std::isfinite(d))fallback[n]=static_cast<float>(std::clamp(d,0.,1.));}return fallback;}
 template<std::size_t N> std::array<bool,N> multi(std::string_view p)const{std::array<bool,N> out{};const auto& v=item(p);auto i=v.find("selected");if(i!=v.end()&&i->is_array())for(std::size_t n=0;n<N&&n<i->size();++n)if((*i)[n].is_boolean())out[n]=(*i)[n].get<bool>();return out;}
};
constexpr std::string_view mapped_paths[]{
 "Misc/Inventory/Equipment / item selection","Misc/Inventory/Skins for selected item","Misc/Inventory/Apply to both teams","Misc/Inventory/Live updating preview",
 "Misc/Misc/Movement/Bunny hop","Misc/Misc/Movement/Strafe assist","Misc/Misc/Movement/Bounce chance","Misc/Misc/Movement/Bounce limit","Misc/Misc/Movement/Assist strength","Misc/Misc/Auto accept",

 "Combat/Legit/General/Master switch","Combat/Legit/Aim assist/Enable","Combat/Legit/Aim assist/Draw FOV","Combat/Legit/Aim assist/Aim/Radius","Combat/Legit/Aim assist/Accuracy/Hit groups: all / selectable groups","Combat/Legit/Triggerbot/Enable","Combat/Legit/RCS","Combat/Legit/RCS/Horizontal (X)","Combat/Legit/RCS/Vertical (Y)",
 "Visuals/Player/Target group: Enemies / Teammates","Visuals/Player/ESP items/Box","Visuals/Player/ESP items/Name","Visuals/Player/ESP items/Health bar","Visuals/Player/ESP items/Flags","Visuals/Player/Off-screen arrows","Visuals/Player/Off-screen arrows/Size","Visuals/Player/Off-screen arrows/Color","Visuals/Player/Sound ESP","Visuals/Player/Enemy model / Chams","Visuals/Player/Enemy model / Chams/Material: shaded / solid / glow / glow outline / iridescent / water flow / glossy",
 "Visuals/World/View/Perspective options/Force third person","Visuals/World/View/Viewmodel options/Override position","Visuals/World/HUD/Preserve killfeed","Visuals/World/HUD/Inaccuracy overlay","Visuals/World/HUD/Crosshairs/Scoped crosshair",
 "Visuals/World/Miscellaneous/Removals/Flashbang effects","Visuals/World/Miscellaneous/Removals/Smoke rendering","Visuals/World/Miscellaneous/Removals/Ragdolls: remove / force","Visuals/World/Miscellaneous/Ambience/Night mode","Visuals/World/Miscellaneous/Ambience/Sunlight override","Visuals/World/Miscellaneous/Ambience/Fog","Visuals/World/Miscellaneous/Ambience/Depth-of-field override","Visuals/World/Miscellaneous/Ambience/Weather"
};
constexpr std::string_view player_color_prefix="Visuals/Player/Element colors/";
struct PlayerToggle {const char* path;bool visuals::Settings::*member;};
constexpr PlayerToggle player_toggles[]{
 {"Visuals/Player/ESP items/Distance",&visuals::Settings::distance},{"Visuals/Player/ESP items/Ammo bar",&visuals::Settings::ammo},
 {"Visuals/Player/ESP items/Skeleton",&visuals::Settings::skeleton},{"Visuals/Player/ESP items/Hitboxes",&visuals::Settings::hitboxes},
 {"Visuals/Player/ESP items/Health number",&visuals::Settings::health_number},{"Visuals/Player/ESP items/Armor bar",&visuals::Settings::armor},
 {"Visuals/Player/ESP items/Snapline",&visuals::Settings::snapline},{"Visuals/Player/ESP items/Weapon",&visuals::Settings::weapon}};
}
bool wired(std::string_view path){
 const auto* control=node(path);if(!control || control->planned || path.starts_with("Combat/Rage"))return false;
 for(const auto& option:visuals::esp_toggles)if(path==std::string("Visuals/Player/Teammates/")+option.label)return true;
 if(path=="Visuals/Player/Teammates/Flags")return true;
 if(control->kind==Kind::ToggleColor && (!control->color_path || !wired(control->color_path)))return false;
 if(path=="Visuals/Player/Enabled" || path=="Combat/Legit/Aim assist/Conditions" || path=="Combat/Legit/Triggerbot/Conditions")return true;
 constexpr std::string_view hud_options[]{"Visuals/World/HUD/Scope overlay: default / lines / none","Visuals/World/HUD/Scope color","Visuals/World/HUD/Scoped crosshair color","Visuals/World/HUD/Scope gap","Visuals/World/HUD/Scope length","Visuals/World/HUD/Scope thickness"};
 if(std::find(std::begin(hud_options),std::end(hud_options),path)!=std::end(hud_options))return true;
 constexpr std::string_view visual_options[]{"Visuals/Player/Glow","Visuals/Player/ESP items/Visible only","Visuals/Player/ESP items/Box style","Visuals/Player/ESP items/Weapon style","Visuals/Player/ESP items/Flag types","Visuals/Player/ESP items/Distance limit","Visuals/Player/ESP items/Thickness","Visuals/Player/ESP items/Box opacity","Visuals/Player/ESP items/Box distance","Visuals/Player/Enemy model / Chams/Visible pass","Visuals/Player/Enemy model / Chams/Visible only","Visuals/Player/Enemy model / Chams/Opacity","Visuals/Player/Enemy model / Chams/Distance limit"};
 if(std::find(std::begin(visual_options),std::end(visual_options),path)!=std::end(visual_options))return true;
 if(path=="Misc/Misc/Movement/Air strafe/Subtick mode"||path=="Misc/Misc/Movement/Air strafe/Turn speed"||path=="Misc/Misc/Movement/Slow walk"||path=="Misc/Misc/Movement/Standalone quick stop")return true;
 if(path.starts_with("Combat/Legit/")) {
   if(path=="Combat/Legit/General/Master switch"||path=="Combat/Legit/Aim assist/Aim key"||path=="Combat/Legit/Aim assist/Accuracy/Hit groups: all / selectable groups")return true;
   for(const auto& t:combat::legit_toggles)if(path==legit_profiles::toggle_path(t.key))return true;
   for(const auto& n:combat::legit_numbers)if(path==legit_profiles::number_path(n.key))return true;
   for(const auto& c:combat::legit_choices)if(path==legit_profiles::choice_path(c.key))return true;
   if(path=="Combat/Legit/Triggerbot/Hit groups"||path=="Combat/Legit/Aim assist/Aim/Multipoint")return true;
   return false;
 }
 constexpr std::string_view additional[]{
 "Visuals/World/View/FOV","Visuals/World/View/Enable FOV override","Visuals/World/View/Scope options/Override scope FOV","Visuals/World/View/Scope options/Enable FOV override",
 "Visuals/World/View/Viewmodel options/Enable FOV override","Visuals/World/View/Viewmodel options/Override FOV","Visuals/World/View/Viewmodel options/Offset X","Visuals/World/View/Viewmodel options/Offset Y","Visuals/World/View/Viewmodel options/Offset Z","Visuals/World/View/Perspective options/Distance",
 "Visuals/World/World ESP/Weapons","Visuals/World/World ESP/Hostages","Visuals/World/World ESP/Grenades","Visuals/World/World ESP/Bomb/Bomb indicator",
 "Visuals/World/Miscellaneous/Removals/Fog","Visuals/World/Miscellaneous/Removals/Shadows","Visuals/World/Miscellaneous/Removals/Sky","Visuals/World/Miscellaneous/Removals/3D sky"};
 if(std::find(std::begin(additional),std::end(additional),path)!=std::end(additional))return true;
 if(path.starts_with("Visuals/World/World ESP/Element colors/"))for(const auto& color:world::esp::colors)if(path.substr(std::string_view("Visuals/World/World ESP/Element colors/").size())==color.key)return true;
 constexpr std::string_view world_controls[]{"Sky override","Sky color override","Sky brightness","Night strength","Sunlight color","Sunlight intensity","Fog color","Fog density","Fog start","Fog distance","Fog falloff","Weather intensity","DoF near blurry","DoF near crisp","DoF far crisp","DoF far blurry","World tint","World color","Bloom override","Bloom","Wetness override","Wetness","Smoke tint","Smoke color","Color grading","Brightness","Vibrance","Contrast","Gamma","Warmth","Sharpness","Vignette"};
 constexpr std::string_view ambience="Visuals/World/Miscellaneous/Ambience/";
 if(path.starts_with(ambience))for(const auto name:world_controls)if(path.substr(ambience.size())==name)return true;
 if(std::find(std::begin(mapped_paths),std::end(mapped_paths),path)!=std::end(mapped_paths))return true;
 for(const auto& toggle:player_toggles)if(path==toggle.path)return true;
 if(path.starts_with(player_color_prefix))for(const auto& color:visuals::colors)if(path.substr(player_color_prefix.size())==color.key)return true;
 return false;
}
std::array<float,4> default_color(std::string_view path){
 const cosmetics::HudSettings hud;
 if(path=="Visuals/World/HUD/Scope color")return hud.scope_color;
 if(path=="Visuals/World/HUD/Scoped crosshair color")return hud.crosshair_color;
 const visuals::Settings defaults;
 if(path=="Visuals/Player/Off-screen arrows/Color")return defaults.arrow_color;
 if(path.starts_with(player_color_prefix))for(const auto& color:visuals::colors)if(path.substr(player_color_prefix.size())==color.key)return defaults.*color.member;
 const world::esp::Settings world_esp;
 if(path.starts_with("Visuals/World/World ESP/Element colors/"))for(const auto& color:world::esp::colors)if(path.substr(std::string_view("Visuals/World/World ESP/Element colors/").size())==color.key)return world_esp.*color.member;
 const WorldState world;const auto rgba=[](ImVec4 c){return std::array<float,4>{c.x,c.y,c.z,c.w};};
 if(path=="Visuals/World/Miscellaneous/Ambience/Sky color override")return rgba(world.sky_color);
 if(path=="Visuals/World/Miscellaneous/Ambience/Sunlight color")return rgba(world.lighting_color);
 if(path=="Visuals/World/Miscellaneous/Ambience/Fog color")return rgba(world.fog_color);
 if(path=="Visuals/World/Miscellaneous/Ambience/World color")return rgba(world.world_color);
 if(path=="Visuals/World/Miscellaneous/Ambience/Smoke color")return rgba(world.smoke_color);
 return {theme::default_accent.x,theme::default_accent.y,theme::default_accent.z,theme::default_accent.w};
}
bool enabled(const MenuState& s,std::string_view p){return Controls(s).on(p);}
void set_toggle(MenuState& s,std::string_view path,bool enabled){if(!wired(path))return;auto data=parse(s.preview_tree);data[std::string(path)]["enabled"]=enabled;s.preview_tree=data.dump();}
void project(MenuState& s){
 Controls v(s);auto& c=s.cosmetics;
 c.enabled=true;
 // Inventory selections always equip; clearing a slot restores its original item.
 c.thirdperson=v.on("Visuals/World/View/Perspective options/Force third person");
 c.auto_accept=v.on("Misc/Misc/Auto accept");c.preserve_killfeed=v.on("Visuals/World/HUD/Preserve killfeed");
 c.bhop=v.on("Misc/Misc/Movement/Bunny hop");c.strafe_assist=v.on("Misc/Misc/Movement/Strafe assist");
 if(v.item("Misc/Misc/Movement/Air strafe/Subtick mode").contains("choice"))c.movement_options.subtick=v.choice("Misc/Misc/Movement/Air strafe/Subtick mode",1)==1;
 c.movement_options.turn_speed=v.number("Misc/Misc/Movement/Air strafe/Turn speed",0,2880,c.movement_options.turn_speed);
 c.movement_options.slow_walk=v.on("Misc/Misc/Movement/Slow walk");c.movement_options.quick_stop=v.on("Misc/Misc/Movement/Standalone quick stop");
 c.movement_options.chance=v.number("Misc/Misc/Movement/Bounce chance",0,100,c.movement_options.chance);c.movement_options.attempt_limit=std::floor(v.number("Misc/Misc/Movement/Bounce limit",0,100,c.movement_options.attempt_limit));c.strafe_strength=v.number("Misc/Misc/Movement/Assist strength",0,100,c.strafe_strength*100)/100.f;
 c.custom_fov=v.on("Visuals/World/View/Enable FOV override");c.fov=v.number("Visuals/World/View/FOV",60,140);
 c.scope_fov_override=v.on("Visuals/World/View/Scope options/Enable FOV override");c.scope_fov=v.number("Visuals/World/View/Scope options/Override scope FOV",60,140,c.scope_fov);
 c.viewmodel_override=v.on("Visuals/World/View/Viewmodel options/Override position");c.viewmodel_fov_override=v.on("Visuals/World/View/Viewmodel options/Enable FOV override");
 c.viewmodel_fov=v.number("Visuals/World/View/Viewmodel options/Override FOV",40,120);
 c.viewmodel_x=v.number("Visuals/World/View/Viewmodel options/Offset X",-10,10);c.viewmodel_y=v.number("Visuals/World/View/Viewmodel options/Offset Y",-10,10);c.viewmodel_z=v.number("Visuals/World/View/Viewmodel options/Offset Z",-10,10);
 c.thirdperson_distance=v.number("Visuals/World/View/Perspective options/Distance",0,500);
 c.world_esp.weapons=v.on("Visuals/World/World ESP/Weapons");c.world_esp.hostages=v.on("Visuals/World/World ESP/Hostages");c.world_esp.grenades=v.on("Visuals/World/World ESP/Grenades");c.world_esp.bomb=v.on("Visuals/World/World ESP/Bomb/Bomb indicator");
 for(const auto& color:world::esp::colors)c.world_esp.*color.member=v.color(std::string("Visuals/World/World ESP/Element colors/")+color.key,c.world_esp.*color.member);
 c.no_spread=false; // Rage is unavailable, including values loaded from old profiles.
 auto& aim=c.combat_settings;const bool master=v.on("Combat/Legit/General/Master switch");
 if(aim.profile_sets.empty()) {
 auto& legit=aim.legit;
 legit.enabled=v.on("Combat/Legit/Aim assist/Enable");legit.trigger=v.on("Combat/Legit/Triggerbot/Enable");legit.recoil_control=v.on("Combat/Legit/RCS");legit.visualize_fov=v.on("Combat/Legit/Aim assist/Draw FOV");
 legit.fov=v.number("Combat/Legit/Aim assist/Aim/Radius",1,30);legit.hitboxes=v.multi<6>("Combat/Legit/Aim assist/Accuracy/Hit groups: all / selectable groups");legit.recoil_horizontal=v.number("Combat/Legit/RCS/Horizontal (X)",0,100)/100.f;legit.recoil_vertical=v.number("Combat/Legit/RCS/Vertical (Y)",0,100)/100.f;
 aim.initialize_profiles();
 }
 aim.master_enabled=master;
 auto& p=c.player_visuals;p.box=v.on("Visuals/Player/ESP items/Box");p.name=v.on("Visuals/Player/ESP items/Name");p.health=v.on("Visuals/Player/ESP items/Health bar");p.flags=v.on("Visuals/Player/ESP items/Flags")?visuals::Settings{}.flags:std::array<bool,9>{};
 p.arrows=v.on("Visuals/Player/Off-screen arrows");p.arrow_size=v.number("Visuals/Player/Off-screen arrows/Size",6,24,p.arrow_size);p.sounds=v.on("Visuals/Player/Sound ESP");p.chams=v.on("Visuals/Player/Enemy model / Chams");p.teammates=v.choice("Visuals/Player/Target group: Enemies / Teammates",1)==1;
 constexpr const char* material_path="Visuals/Player/Enemy model / Chams/Material: shaded / solid / glow / glow outline / iridescent / water flow / glossy";
 constexpr int materials[]{4,3,1,2};const int material=v.choice(material_path,6);p.chams_material=material<4?materials[material]:4;
 bool normalized=material>=4;if(normalized)v.data[material_path]["choice"]=0;
 p.arrow_color=v.color("Visuals/Player/Off-screen arrows/Color",p.arrow_color);
 for(const auto& color:visuals::colors)if(std::string_view(color.key)!="arrow_color")p.*color.member=v.color(std::string(player_color_prefix)+color.key,p.*color.member);
 for(const auto& toggle:player_toggles)p.*toggle.member=v.on(toggle.path);
 p.glow=v.on("Visuals/Player/Glow");p.visible_only=v.on("Visuals/Player/ESP items/Visible only");p.box_style=v.choice("Visuals/Player/ESP items/Box style",1);p.weapon_mode=v.choice("Visuals/Player/ESP items/Weapon style",2);
 p.max_distance=v.number("Visuals/Player/ESP items/Distance limit",5,500,p.max_distance);p.thickness=v.number("Visuals/Player/ESP items/Thickness",1,3,p.thickness);p.box_opacity=v.number("Visuals/Player/ESP items/Box opacity",0,1);p.box_max_distance=v.number("Visuals/Player/ESP items/Box distance",5,500);
 p.chams_visible=v.item("Visuals/Player/Enemy model / Chams/Visible pass").contains("enabled")?v.on("Visuals/Player/Enemy model / Chams/Visible pass"):true;p.chams_visible_only=v.on("Visuals/Player/Enemy model / Chams/Visible only");p.chams_opacity=v.number("Visuals/Player/Enemy model / Chams/Opacity",0,1,p.chams_opacity);p.chams_max_distance=v.number("Visuals/Player/Enemy model / Chams/Distance limit",5,500,p.chams_max_distance);
 if(v.on("Visuals/Player/ESP items/Flags")&&v.item("Visuals/Player/ESP items/Flag types").contains("selected"))p.flags=v.multi<9>("Visuals/Player/ESP items/Flag types");
 p.enabled=v.item("Visuals/Player/Enabled").contains("enabled")?v.on("Visuals/Player/Enabled"):(p.glow||p.box||p.name||p.health||p.arrows||p.sounds||p.chams||p.distance||p.ammo||p.skeleton||p.hitboxes||p.health_number||p.armor||p.snapline||p.weapon||std::any_of(p.flags.begin(),p.flags.end(),[](bool on){return on;}));
 const auto teammate=visuals::teammate_options(p);p.teammate_esp=teammate;
 for(const auto& option:visuals::esp_toggles){const auto path=std::string("Visuals/Player/Teammates/")+option.label;if(v.data.contains(path)){p.separate_teammate_esp=true;p.teammate_esp.*option.target=v.on(path);}}
 if(v.data.contains("Visuals/Player/Teammates/Flags")){p.separate_teammate_esp=true;p.teammate_esp.flags=v.multi<9>("Visuals/Player/Teammates/Flags");}
 const auto scope=v.item("Visuals/World/HUD/Scope overlay: default / lines / none");const auto scope_multi=v.multi<2>("Visuals/World/HUD/Scope overlay: default / lines / none");const int scope_overlay=v.choice("Visuals/World/HUD/Scope overlay: default / lines / none",2);c.hud.scope_lines=scope.contains("selected")?scope_multi[0]:scope_overlay==1;c.hud.scope_hide=scope.contains("selected")?scope_multi[1]:scope_overlay==2;c.hud.scope_gap=v.number("Visuals/World/HUD/Scope gap",0,240,c.hud.scope_gap);c.hud.scope_length=v.number("Visuals/World/HUD/Scope length",0,4000,c.hud.scope_length);c.hud.scope_thickness=v.number("Visuals/World/HUD/Scope thickness",.5f,8,c.hud.scope_thickness);c.hud.scope_color=v.color("Visuals/World/HUD/Scope color",c.hud.scope_color);c.hud.crosshair_color=v.color("Visuals/World/HUD/Scoped crosshair color",c.hud.crosshair_color);c.hud.inaccuracy_overlay=v.on("Visuals/World/HUD/Inaccuracy overlay");c.hud.crosshair_mode=v.on("Visuals/World/HUD/Crosshairs/Scoped crosshair")?1:0;
 auto& w=s.world;w.night_mode=v.on("Visuals/World/Miscellaneous/Ambience/Night mode");w.lighting_override=v.on("Visuals/World/Miscellaneous/Ambience/Sunlight override");w.fog_override=v.on("Visuals/World/Miscellaneous/Ambience/Fog");w.dof_override=v.on("Visuals/World/Miscellaneous/Ambience/Depth-of-field override");w.weather=0;
 w.remove_flash=v.on("Visuals/World/Miscellaneous/Removals/Flashbang effects");w.remove_smoke=v.on("Visuals/World/Miscellaneous/Removals/Smoke rendering");w.remove_ragdolls=v.on("Visuals/World/Miscellaneous/Removals/Ragdolls: remove / force");
 w.sky_override=v.on("Visuals/World/Miscellaneous/Ambience/Sky override");
 w.night_intensity=v.number("Visuals/World/Miscellaneous/Ambience/Night strength",0,1);
 w.sky_brightness=v.number("Visuals/World/Miscellaneous/Ambience/Sky brightness",0,4,w.sky_brightness);
 w.lighting_intensity=v.number("Visuals/World/Miscellaneous/Ambience/Sunlight intensity",0,4,w.lighting_intensity);
 w.fog_density=v.number("Visuals/World/Miscellaneous/Ambience/Fog density",0,1,w.fog_density);
 w.fog_start=v.number("Visuals/World/Miscellaneous/Ambience/Fog start",0,10000,w.fog_start);
 const float fog_distance=v.number("Visuals/World/Miscellaneous/Ambience/Fog distance",100,20000,w.fog_distance);w.fog_distance=std::max(w.fog_start+1,fog_distance);
 if(w.fog_distance!=fog_distance){v.data["Visuals/World/Miscellaneous/Ambience/Fog distance"]["number"]=w.fog_distance;normalized=true;}
 w.fog_falloff=v.number("Visuals/World/Miscellaneous/Ambience/Fog falloff",.5f,8.5f,w.fog_falloff);
 w.weather_intensity=v.number("Visuals/World/Miscellaneous/Ambience/Weather intensity",0,1,w.weather_intensity);
 w.dof_near_blurry=v.number("Visuals/World/Miscellaneous/Ambience/DoF near blurry",0,50,w.dof_near_blurry);
 const auto ordered=[&](const char* path,float low,float high,float previous){const float requested=v.number(path,low,high);const float actual=std::max(previous,requested);if(actual!=requested){v.data[path]["number"]=actual;normalized=true;}return actual;};
 w.dof_near_crisp=ordered("Visuals/World/Miscellaneous/Ambience/DoF near crisp",0,100,w.dof_near_blurry);
 w.dof_far_crisp=ordered("Visuals/World/Miscellaneous/Ambience/DoF far crisp",100,10000,w.dof_near_crisp);
 w.dof_far_blurry=ordered("Visuals/World/Miscellaneous/Ambience/DoF far blurry",200,20000,w.dof_far_crisp);
 const auto world_color=[&](const char* path,ImVec4& value){const auto color=v.color(path,{value.x,value.y,value.z,value.w});value={color[0],color[1],color[2],color[3]};};
 world_color("Visuals/World/Miscellaneous/Ambience/Sky color override",w.sky_color);
 world_color("Visuals/World/Miscellaneous/Ambience/Sunlight color",w.lighting_color);
 world_color("Visuals/World/Miscellaneous/Ambience/Fog color",w.fog_color);
 w.world_override=v.on("Visuals/World/Miscellaneous/Ambience/World tint");w.bloom_override=v.on("Visuals/World/Miscellaneous/Ambience/Bloom override");w.wetness_override=v.on("Visuals/World/Miscellaneous/Ambience/Wetness override");w.smoke_override=v.on("Visuals/World/Miscellaneous/Ambience/Smoke tint");
 w.bloom=v.number("Visuals/World/Miscellaneous/Ambience/Bloom",0,2);w.wetness=v.number("Visuals/World/Miscellaneous/Ambience/Wetness",0,5);
 world_color("Visuals/World/Miscellaneous/Ambience/World color",w.world_color);world_color("Visuals/World/Miscellaneous/Ambience/Smoke color",w.smoke_color);
 w.remove_fog=v.on("Visuals/World/Miscellaneous/Removals/Fog");w.remove_shadows=v.on("Visuals/World/Miscellaneous/Removals/Shadows");w.remove_sky=v.on("Visuals/World/Miscellaneous/Removals/Sky");w.remove_3d_sky=v.on("Visuals/World/Miscellaneous/Removals/3D sky");
 w.skybox=0;w.skybox_material.clear();w.exposure=0;
 const bool grade=v.on("Visuals/World/Miscellaneous/Ambience/Color grading");w.ambient=1;w.haze=0;
 w.brightness=grade?v.number("Visuals/World/Miscellaneous/Ambience/Brightness",0,4):1;w.vibrance=grade?v.number("Visuals/World/Miscellaneous/Ambience/Vibrance",0,3):1;w.contrast=grade?v.number("Visuals/World/Miscellaneous/Ambience/Contrast",0,3):1;w.gamma=grade?v.number("Visuals/World/Miscellaneous/Ambience/Gamma",.1f,3):1;
 w.warmth=grade?v.number("Visuals/World/Miscellaneous/Ambience/Warmth",-1,1):0;w.sharpness=grade?v.number("Visuals/World/Miscellaneous/Ambience/Sharpness",0,2):0;w.vignette=grade?v.number("Visuals/World/Miscellaneous/Ambience/Vignette",0,1):0;
 w.enabled=grade||w.world_override||w.bloom_override||w.wetness_override||w.smoke_override||w.remove_fog||w.remove_shadows||w.remove_sky||w.remove_3d_sky||w.night_mode||w.sky_override||w.lighting_override||w.fog_override||w.dof_override||w.weather||w.remove_flash||w.remove_smoke||w.remove_ragdolls;
 // One owner for the inventory mirror switch, regardless of where it was edited.
 const auto& both=v.item("Misc/Inventory/Apply to both teams");if(both.contains("enabled"))s.inventory.both_teams=v.on("Misc/Inventory/Apply to both teams");
 if(normalized)s.preview_tree=v.data.dump();
}
cosmetics::Configuration runtime_configuration(const MenuState& s){auto c=s.cosmetics;c.no_spread=false;c.enabled=true;c.gloves=s.inventory.gloves;return c;}
void reconcile(const MenuState& before,const MenuState& requested,const MenuState& actual,MenuState& target,bool publish){
 const auto b=parse(before.preview_tree),r=parse(requested.preview_tree),a=parse(actual.preview_tree);auto t=parse(target.preview_tree);
 const auto merge=[](auto&& self,const Json& b,const Json& r,const Json& a,Json& t,bool publish)->void{
  if(b==r)return;
  if((b.is_object()||b.is_null())&&r.is_object()&&(a.is_object()||a.is_null())&&t.is_object()){
   std::set<std::string> keys;for(const auto& [k,v]:b.items())keys.insert(k);for(const auto& [k,v]:r.items())keys.insert(k);
   const Json missing;
   for(const auto& k:keys){Json next=t.contains(k)?t.at(k):missing;self(self,b.contains(k)?b.at(k):missing,r.contains(k)?r.at(k):missing,a.contains(k)?a.at(k):missing,next,publish);if(next.is_null())t.erase(k);else t[k]=std::move(next);}
  }else if(!publish||t==r)t=publish?a:r;
 };
 merge(merge,b,r,a,t,publish);target.preview_tree=t.empty()?std::string{}:t.dump();
}
}
