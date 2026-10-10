#include "core/menu/feature_switches.hpp"
#include "core/menu/feature_catalog.hpp"
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
 bool on(std::string_view p)const{const auto& v=item(p);auto i=v.find("enabled");return i!=v.end()&&i->is_boolean()&&i->get<bool>();}
 float number(std::string_view p,float low,float high)const{const auto* n=node(p);float value=n?n->initial:low;const auto& v=item(p);auto i=v.find("number");if(i!=v.end()&&i->is_number()){const double d=i->get<double>();if(std::isfinite(d))value=static_cast<float>(std::clamp(d,double(low),double(high)));}return std::clamp(value,low,high);}
 int choice(std::string_view p,int maximum)const{const auto& v=item(p);auto i=v.find("choice");if(i!=v.end()&&i->is_number_integer()){const auto d=i->get<double>();return static_cast<int>(std::clamp(d,0.,double(maximum)));}return 0;}
 std::array<float,4> color(std::string_view p,std::array<float,4> fallback)const{const auto& v=item(p);auto i=v.find("color");if(i!=v.end()&&i->is_array()&&i->size()==4)for(int n=0;n<4;++n)if((*i)[n].is_number()){const double d=(*i)[n].get<double>();if(std::isfinite(d))fallback[n]=static_cast<float>(std::clamp(d,0.,1.));}return fallback;}
 template<std::size_t N> std::array<bool,N> multi(std::string_view p)const{std::array<bool,N> out{};const auto& v=item(p);auto i=v.find("selected");if(i!=v.end()&&i->is_array())for(std::size_t n=0;n<N&&n<i->size();++n)if((*i)[n].is_boolean())out[n]=(*i)[n].get<bool>();return out;}
};
constexpr std::string_view mapped_paths[]{
 "Misc/Inventory/Equipment / item selection","Misc/Inventory/Skins for selected item","Misc/Inventory/Apply to both teams","Misc/Inventory/Live updating preview",
 "Misc/Misc/Movement/Bunny hop","Misc/Misc/Movement/Strafe assist","Misc/Misc/Movement/Bounce chance","Misc/Misc/Movement/Bounce limit","Misc/Misc/Movement/Assist strength","Misc/Misc/Auto accept",
 "Combat/Rage/Aimbot/Enable","Combat/Rage/Aimbot/Silent aim","Combat/Rage/Aimbot/Automatic fire","Combat/Rage/Aimbot/Aim through walls","Combat/Rage/Aimbot/Field of view","Combat/Rage/Aimbot/Hitboxes","Combat/Rage/Aimbot/Multipoints","Combat/Rage/Aimbot/Multipoints/Scale","Combat/Rage/Aimbot/Hitchance","Combat/Rage/Aimbot/Minimum damage","Combat/Rage/Aimbot/Quick stop","Combat/Rage/Aimbot/Quick scope","Combat/Rage/Aimbot/Remove recoil","Combat/Rage/Aimbot/Remove spread",
 "Combat/Legit/General/Master switch","Combat/Legit/Aim assist/Enable","Combat/Legit/Aim assist/Draw FOV","Combat/Legit/Aim assist/Aim/Radius","Combat/Legit/Aim assist/Accuracy/Hit groups: all / selectable groups","Combat/Legit/Triggerbot/Enable","Combat/Legit/RCS","Combat/Legit/RCS/Horizontal (X)","Combat/Legit/RCS/Vertical (Y)",
 "Visuals/Player/Target group: Enemies / Teammates","Visuals/Player/ESP items/Box","Visuals/Player/ESP items/Name","Visuals/Player/ESP items/Health bar","Visuals/Player/ESP items/Flags","Visuals/Player/Off-screen arrows","Visuals/Player/Off-screen arrows/Size","Visuals/Player/Off-screen arrows/Color","Visuals/Player/Sound ESP","Visuals/Player/Enemy model / Chams","Visuals/Player/Enemy model / Chams/Material: shaded / solid / glow / glow outline / iridescent / water flow / glossy",
 "Visuals/World/View/Perspective options/Force third person","Visuals/World/View/Viewmodel options/Override position","Visuals/World/HUD/Preserve killfeed","Visuals/World/HUD/Inaccuracy overlay","Visuals/World/HUD/Crosshairs/Scoped crosshair",
 "Visuals/World/Miscellaneous/Removals/Flashbang effects","Visuals/World/Miscellaneous/Removals/Smoke rendering","Visuals/World/Miscellaneous/Removals/Ragdolls: remove / force","Visuals/World/Miscellaneous/Ambience/Night mode","Visuals/World/Miscellaneous/Ambience/Sunlight override","Visuals/World/Miscellaneous/Ambience/Fog","Visuals/World/Miscellaneous/Ambience/Depth-of-field override","Visuals/World/Miscellaneous/Ambience/Weather"
};
}
bool wired(std::string_view path){return std::find(std::begin(mapped_paths),std::end(mapped_paths),path)!=std::end(mapped_paths);}
bool enabled(const MenuState& s,std::string_view p){return Controls(s).on(p);}
void set_toggle(MenuState& s,std::string_view path,bool enabled){auto data=parse(s.preview_tree);data[std::string(path)]["enabled"]=enabled;s.preview_tree=data.dump();}
void project(MenuState& s){
 Controls v(s);auto& c=s.cosmetics;
 c.enabled=v.on("Misc/Inventory/Equipment / item selection");
 // Skin/item drafts stay committed while the master is off; only runtime input is filtered.
 c.thirdperson=v.on("Visuals/World/View/Perspective options/Force third person");
 c.auto_accept=v.on("Misc/Misc/Auto accept");c.preserve_killfeed=v.on("Visuals/World/HUD/Preserve killfeed");
 c.bhop=v.on("Misc/Misc/Movement/Bunny hop");c.strafe_assist=v.on("Misc/Misc/Movement/Strafe assist");
 c.movement_options.chance=v.number("Misc/Misc/Movement/Bounce chance",0,100);c.movement_options.attempt_limit=std::floor(v.number("Misc/Misc/Movement/Bounce limit",0,100));c.strafe_strength=v.number("Misc/Misc/Movement/Assist strength",0,100)/100.f;
 c.custom_fov=false;c.viewmodel_override=v.on("Visuals/World/View/Viewmodel options/Override position");c.viewmodel_fov_override=false;
 c.no_spread=v.on("Combat/Rage/Aimbot/Enable");auto& rage=c.no_spread_settings;
 rage.auto_fire=v.on("Combat/Rage/Aimbot/Automatic fire");rage.silent_aim=v.on("Combat/Rage/Aimbot/Silent aim");rage.through_walls=v.on("Combat/Rage/Aimbot/Aim through walls");rage.remove_recoil=v.on("Combat/Rage/Aimbot/Remove recoil");rage.compensate=v.on("Combat/Rage/Aimbot/Remove spread");rage.quick_stop=v.on("Combat/Rage/Aimbot/Quick stop");rage.quick_scope=v.on("Combat/Rage/Aimbot/Quick scope");rage.multipoint=v.on("Combat/Rage/Aimbot/Multipoints");
 constexpr float scales[]{.5f,.25f,.5f,.75f};rage.multipoint_scale=scales[v.choice("Combat/Rage/Aimbot/Multipoints/Scale",3)];
 rage.fov=v.number("Combat/Rage/Aimbot/Field of view",1,180);rage.hit_chance=v.number("Combat/Rage/Aimbot/Hitchance",0,100);rage.minimum_damage=v.number("Combat/Rage/Aimbot/Minimum damage",1,130);rage.hitboxes=v.multi<6>("Combat/Rage/Aimbot/Hitboxes");
 rage.visualize_fov=false;rage.highlight_target=false;rage.health_damage=false;
 // Delay has no independent toggle in the current catalog: leave it disconnected.
 rage.delay_shot=false;rage.shot_delay=0;
 auto& legit=c.combat_settings.legit;const bool master=v.on("Combat/Legit/General/Master switch");
 legit.enabled=master&&v.on("Combat/Legit/Aim assist/Enable");legit.trigger=master&&v.on("Combat/Legit/Triggerbot/Enable");legit.recoil_control=master&&v.on("Combat/Legit/RCS");legit.standalone_recoil=false;legit.visualize_fov=master&&v.on("Combat/Legit/Aim assist/Draw FOV");
 legit.fov=v.number("Combat/Legit/Aim assist/Aim/Radius",1,30);legit.hitboxes=v.multi<6>("Combat/Legit/Aim assist/Accuracy/Hit groups: all / selectable groups");legit.recoil_horizontal=v.number("Combat/Legit/RCS/Horizontal (X)",0,100)/100.f;legit.recoil_vertical=v.number("Combat/Legit/RCS/Vertical (Y)",0,100)/100.f;
 legit.shot_delay=0;
 auto& p=c.player_visuals;p.box=v.on("Visuals/Player/ESP items/Box");p.name=v.on("Visuals/Player/ESP items/Name");p.health=v.on("Visuals/Player/ESP items/Health bar");p.flags=v.on("Visuals/Player/ESP items/Flags")?visuals::Settings{}.flags:std::array<bool,9>{};
 p.arrows=v.on("Visuals/Player/Off-screen arrows");p.arrow_size=v.number("Visuals/Player/Off-screen arrows/Size",6,24);p.sounds=v.on("Visuals/Player/Sound ESP");p.chams=v.on("Visuals/Player/Enemy model / Chams");p.teammates=v.choice("Visuals/Player/Target group: Enemies / Teammates",1)==1;
 constexpr int materials[]{4,3,1,2};const int material=v.choice("Visuals/Player/Enemy model / Chams/Material: shaded / solid / glow / glow outline / iridescent / water flow / glossy",6);p.chams_material=material<4?materials[material]:4;
 p.enemy_color=v.color("Visuals/Player/Off-screen arrows/Color",visuals::Settings{}.enemy_color);
 p.distance=p.ammo=p.skeleton=p.glow=p.hitboxes=p.health_number=p.armor=p.snapline=p.weapon=false;
 p.enabled=p.box||p.name||p.health||p.arrows||p.sounds||p.chams||std::any_of(p.flags.begin(),p.flags.end(),[](bool on){return on;});
 c.hud.scope_lines=false;c.hud.inaccuracy_overlay=v.on("Visuals/World/HUD/Inaccuracy overlay");c.hud.crosshair_mode=v.on("Visuals/World/HUD/Crosshairs/Scoped crosshair")?1:0;
 auto& w=s.world;w.night_mode=v.on("Visuals/World/Miscellaneous/Ambience/Night mode");w.lighting_override=v.on("Visuals/World/Miscellaneous/Ambience/Sunlight override");w.fog_override=v.on("Visuals/World/Miscellaneous/Ambience/Fog");w.dof_override=v.on("Visuals/World/Miscellaneous/Ambience/Depth-of-field override");w.weather=v.choice("Visuals/World/Miscellaneous/Ambience/Weather",2);
 w.remove_flash=v.on("Visuals/World/Miscellaneous/Removals/Flashbang effects");w.remove_smoke=v.on("Visuals/World/Miscellaneous/Removals/Smoke rendering");w.remove_ragdolls=v.on("Visuals/World/Miscellaneous/Removals/Ragdolls: remove / force");
 w.world_override=w.sky_override=w.bloom_override=w.wetness_override=w.smoke_override=w.remove_fog=w.remove_shadows=w.remove_sky=w.remove_3d_sky=false;
 w.skybox=0;w.skybox_material.clear();w.exposure=0;
 w.brightness=w.vibrance=w.contrast=w.gamma=w.ambient=1;w.warmth=w.sharpness=w.vignette=w.haze=0;
 w.enabled=w.night_mode||w.lighting_override||w.fog_override||w.dof_override||w.weather||w.remove_flash||w.remove_smoke||w.remove_ragdolls;
 // One owner for the inventory mirror switch, regardless of where it was edited.
 const auto& both=v.item("Misc/Inventory/Apply to both teams");if(both.contains("enabled"))s.inventory.both_teams=v.on("Misc/Inventory/Apply to both teams");
}
cosmetics::Configuration runtime_configuration(const MenuState& s){auto c=s.cosmetics;c.gloves=s.inventory.gloves;if(!c.enabled){c.agents={};for(auto& model:c.custom_models)model.clear();}if(!enabled(s,"Misc/Inventory/Skins for selected item")){for(auto& entries:c.loadouts)entries.clear();for(auto& glove:c.gloves)glove.enabled=false;}return c;}
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
