#include "core/features/world/skyboxes.hpp"
#include "core/menu/config_io.hpp"
#include "core/menu/hotkeys.hpp"
#include "ext/json/json.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace iris::ui {
using Json=nlohmann::json;
namespace {
Json color(ImVec4 c){return Json::array({c.x,c.y,c.z,c.w});}
Json color(const std::array<float,4>& c){return Json::array({c[0],c[1],c[2],c[3]});}
void read_color(const Json& j,const char* key,ImVec4& c){if(j.contains(key)&&j[key].is_array()&&j[key].size()==4)for(int i=0;i<4;++i){float v=j[key][i].get<float>();if(!std::isfinite(v))throw std::runtime_error("Color is not finite");(&c.x)[i]=std::clamp(v,0.f,1.f);}}
void read_color(const Json& j,const char* key,std::array<float,4>& c){if(j.contains(key)&&j[key].is_array()&&j[key].size()==4)for(int i=0;i<4;++i){float v=j[key][i].get<float>();if(!std::isfinite(v))throw std::runtime_error("Color is not finite");c[i]=std::clamp(v,0.f,1.f);}}
Json selection(const cosmetics::Selection& s){return {{"definition",s.definition},{"skinId",s.paint},{"wear",s.wear},{"seed",s.seed},{"statTrak",s.stattrak>=0},{"statTrakCount",std::max(0,s.stattrak)},{"nametag",s.name.data()},{"enabled",s.enabled}};}
cosmetics::Selection read_selection(const Json& j,bool validate){
    cosmetics::Selection s;s.definition=j.value("definition",7);s.paint=j.value("skinId",j.value("paint",0));s.seed=std::clamp(j.value("seed",0),0,1000);s.wear=j.value("wear",.01f);s.enabled=j.value("enabled",true);
    s.stattrak=j.contains("stattrak")?j["stattrak"].get<int>():j.value("statTrak",false)?std::max(0,j.value("statTrakCount",0)):-1;
    const auto name=j.value("nametag",j.value("name",std::string{}));std::memcpy(s.name.data(),name.data(),std::min(name.size(),s.name.size()-1));
    if(!std::isfinite(s.wear))throw std::runtime_error("Wear is not finite");
    const auto* finish=cosmetics::find_finish(s.definition,s.paint);s.wear=std::clamp(s.wear,finish?finish->minimum_wear:0.f,finish?finish->maximum_wear:1.f);
    if(validate&&!cosmetics::valid(s))throw std::runtime_error("Unknown weapon or finish in config");return s;
}
float bounded(const Json& j,const char* key,float current,float min,float max){float value=j.value(key,current);if(!std::isfinite(value))throw std::runtime_error("Non-finite config value");return std::clamp(value,min,max);}
}
std::string encode_config(const MenuState& state){
    const auto& w=state.world;Json j{{"version",2},{"world",{{"night_mode",w.night_mode},{"night_intensity",w.night_intensity},{"weather",w.weather},{"weather_intensity",w.weather_intensity},{"sky_color",color(w.sky_color)},{"fog_color",color(w.fog_color)},{"ambient",w.ambient},{"brightness",w.brightness}}}};
    auto& world=j["world"];world["skybox_material"]=w.skybox_material;
    world["skybox"]=w.skybox;world["enabled"]=w.enabled;world["sky_override"]=w.sky_override;world["fog_override"]=w.fog_override;
    world["vibrance"]=w.vibrance;world["contrast"]=w.contrast;world["gamma"]=w.gamma;world["warmth"]=w.warmth;
    world["vignette"]=w.vignette;world["sharpness"]=w.sharpness;world["fog_density"]=w.fog_density;world["fog_distance"]=w.fog_distance;world["wind"]=w.wind;
    world["world_override"]=w.world_override;
    world["lighting_override"]=w.lighting_override;
    world["bloom_override"]=w.bloom_override;
    world["dof_override"]=w.dof_override;
    world["wetness_override"]=w.wetness_override;
    world["smoke_override"]=w.smoke_override;
    world["remove_fog"]=w.remove_fog;
    world["remove_shadows"]=w.remove_shadows;
    world["remove_sky"]=w.remove_sky;
    world["remove_3d_sky"]=w.remove_3d_sky;
    world["remove_flash"]=w.remove_flash;
    world["remove_smoke"]=w.remove_smoke;
    world["remove_ragdolls"]=w.remove_ragdolls;
    world["exposure"]=w.exposure;
    world["sky_brightness"]=w.sky_brightness;
    world["lighting_intensity"]=w.lighting_intensity;
    world["bloom"]=w.bloom;
    world["wetness"]=w.wetness;
    world["fog_start"]=w.fog_start;
    world["fog_falloff"]=w.fog_falloff;
    world["dof_near_blurry"]=w.dof_near_blurry;
    world["dof_near_crisp"]=w.dof_near_crisp;
    world["dof_far_crisp"]=w.dof_far_crisp;
    world["dof_far_blurry"]=w.dof_far_blurry;
    world["world_color"]=color(w.world_color);
    world["lighting_color"]=color(w.lighting_color);
    world["smoke_color"]=color(w.smoke_color);
    auto& applied=j["cosmetics"];
    const auto& f=state.cosmetics;
    applied["strafe_assist"]=f.strafe_assist;applied["strafe_strength"]=f.strafe_strength;
    applied["no_spread"]=f.no_spread;const auto& ns=f.no_spread_settings;applied["no_spread_settings"]={{"auto_fire",ns.auto_fire},{"compensate",ns.compensate},{"visualize_fov",ns.visualize_fov},{"hitboxes",ns.hitboxes},{"fov",ns.fov},{"through_walls",ns.through_walls},{"highlight_target",ns.highlight_target},{"minimum_damage",ns.minimum_damage},{"hit_chance",ns.hit_chance},{"silent_aim",ns.silent_aim},{"remove_recoil",ns.remove_recoil},{"multipoint",ns.multipoint},{"multipoint_scale",ns.multipoint_scale},{"quick_stop",ns.quick_stop},{"quick_scope",ns.quick_scope},{"delay_shot",ns.delay_shot},{"health_damage",ns.health_damage},{"shot_delay",ns.shot_delay}};applied["auto_accept"]=f.auto_accept;applied["preserve_killfeed"]=f.preserve_killfeed;applied["custom_fov"]=f.custom_fov;applied["fov"]=f.fov;
    const auto& aim=f.combat_settings;auto& legit=applied["legit"];
    for(const auto& t:combat::legit_toggles)legit[t.key]=aim.legit.*t.member;
    for(const auto& n:combat::legit_numbers)legit[n.key]=aim.legit.*n.member;
    legit["hitboxes"]=aim.legit.hitboxes;
    auto& movement=applied["movement_options"];const auto& options=f.movement_options;
    for(const auto& n:movement::numbers)movement[n.key]=options.*n.member;
    applied["hud"]={{"scope_lines",f.hud.scope_lines},{"inaccuracy_overlay",f.hud.inaccuracy_overlay},{"crosshair_mode",f.hud.crosshair_mode},{"scope_gap",f.hud.scope_gap},{"scope_length",f.hud.scope_length},{"scope_thickness",f.hud.scope_thickness},{"scope_color",color(f.hud.scope_color)},{"crosshair_color",color(f.hud.crosshair_color)}};
    applied["viewmodel_override"]=f.viewmodel_override;applied["viewmodel_fov_override"]=f.viewmodel_fov_override;applied["viewmodel_x"]=f.viewmodel_x;applied["viewmodel_y"]=f.viewmodel_y;applied["viewmodel_z"]=f.viewmodel_z;applied["viewmodel_fov"]=f.viewmodel_fov;applied["custom_models"]=f.custom_models;applied["enabled"]=state.cosmetics.enabled;applied["thirdperson"]=state.cosmetics.thirdperson;applied["bhop"]=state.cosmetics.bhop;
    const auto& pv=f.player_visuals;auto& visual=applied["player_visuals"];
    for(const auto& option:visuals::toggles)visual[option.key]=pv.*option.member;
    visual["box_style"]=pv.box_style;visual["weapon_mode"]=pv.weapon_mode;visual["max_distance"]=pv.max_distance;visual["thickness"]=pv.thickness;visual["arrow_size"]=pv.arrow_size;
    visual["flags"]=pv.flags;for(const auto& color:visuals::colors)visual[color.key]=pv.*color.member;
    visual["box_opacity"]=pv.box_opacity;visual["chams_opacity"]=pv.chams_opacity;visual["chams_material"]=pv.chams_material;visual["material_version"]=2;
    visual["box_max_distance"]=pv.box_max_distance;visual["chams_max_distance"]=pv.chams_max_distance;
    for(int team=0;team<2;++team){const char* key=team?"CT":"T";applied["loadouts"][key]=Json::array();for(const auto& s:state.cosmetics.loadouts[team])applied["loadouts"][key].push_back(selection(s));
        j["inventory"]["gloves"][key]=selection(state.inventory.gloves[team]);
        applied["agents"][key]=state.cosmetics.agents[team];
    }
    j["inventory"]["both_teams"]=state.inventory.both_teams;
    j["hotkey_version"]=2;j["hotkeys"]=Json::object();
    const auto bindings=hotkeys::normalize(state.hotkeys);
    if(!bindings.empty())j["hotkeys"]=Json::parse(bindings)["hotkeys"];
    j["preview_tree"]=state.preview_tree;
    return j.dump(2);
}
MenuState decode_config(const std::string& text){
    auto j=Json::parse(text);if(!j.is_object())throw std::runtime_error("Config must be a JSON object");MenuState state;state.cosmetics.enabled=true;
    if(j.contains("preview_tree") && j["preview_tree"].is_string())state.preview_tree=j["preview_tree"].get<std::string>();
    if(j.contains("hotkey_version") && j["hotkey_version"]==2 && j.contains("hotkeys"))state.hotkeys=hotkeys::normalize(Json{{"hotkey_version",2},{"hotkeys",j["hotkeys"]}}.dump());
    if(j.contains("world")){const auto& w=j["world"];auto& v=state.world;v.night_mode=w.value("night_mode",false);v.night_intensity=bounded(w,"night_intensity",v.night_intensity,0,1);v.weather=std::clamp(w.value("weather",0),0,2);v.weather_intensity=bounded(w,"weather_intensity",v.weather_intensity,0,1);read_color(w,"sky_color",v.sky_color);read_color(w,"fog_color",v.fog_color);v.ambient=bounded(w,"ambient",1,0,2);v.brightness=bounded(w,"brightness",1,.2f,2);}
    if(j.contains("world")){const auto& w=j["world"];auto& v=state.world;
      v.skybox=std::clamp(w.value("skybox",0),0,static_cast<int>(cosmetics::skyboxes.size())-1);v.enabled=w.value("enabled",false);v.sky_override=w.value("sky_override",false);v.fog_override=w.value("fog_override",false);
      v.vibrance=bounded(w,"vibrance",1,0,2);v.contrast=bounded(w,"contrast",1,.5f,1.5f);v.gamma=bounded(w,"gamma",1,.5f,2);
      v.warmth=bounded(w,"warmth",0,-1,1);v.vignette=bounded(w,"vignette",0,0,1);v.sharpness=bounded(w,"sharpness",0,0,1);
      v.skybox_material=w.value("skybox_material",std::string{});
      if(!cosmetics::valid_skybox_material(v.skybox_material))throw std::runtime_error("Skybox material must be a relative materials/*.vmat resource");
      v.world_override=w.value("world_override",false);
      v.lighting_override=w.value("lighting_override",false);
      v.bloom_override=w.value("bloom_override",false);
      v.dof_override=w.value("dof_override",false);
      v.wetness_override=w.value("wetness_override",false);
      v.smoke_override=w.value("smoke_override",false);
      v.remove_fog=w.value("remove_fog",false);
      v.remove_shadows=w.value("remove_shadows",false);
      v.remove_sky=w.value("remove_sky",false);
      v.remove_3d_sky=w.value("remove_3d_sky",false);
      v.remove_flash=w.value("remove_flash",false);
      v.remove_smoke=w.value("remove_smoke",false);
      v.remove_ragdolls=w.value("remove_ragdolls",false);
      v.exposure=bounded(w,"exposure",0,-4,4);
      v.sky_brightness=bounded(w,"sky_brightness",1,0,4);
      v.lighting_intensity=bounded(w,"lighting_intensity",1,0,4);
      v.bloom=bounded(w,"bloom",0.5,0,2);
      v.wetness=bounded(w,"wetness",1,0,5);
      v.fog_start=bounded(w,"fog_start",0,0,10000);
      v.fog_falloff=bounded(w,"fog_falloff",1,0.5,8.5);
      v.dof_near_blurry=bounded(w,"dof_near_blurry",0,0,50);
      v.dof_near_crisp=bounded(w,"dof_near_crisp",20,0,100);
      v.dof_far_crisp=bounded(w,"dof_far_crisp",1500,100,10000);
      v.dof_far_blurry=bounded(w,"dof_far_blurry",3000,200,20000);
      read_color(w,"world_color",v.world_color);
      read_color(w,"lighting_color",v.lighting_color);
      read_color(w,"smoke_color",v.smoke_color);
      v.dof_near_crisp=std::max(v.dof_near_crisp,v.dof_near_blurry);v.dof_far_crisp=std::max(v.dof_far_crisp,v.dof_near_crisp);v.dof_far_blurry=std::max(v.dof_far_blurry,v.dof_far_crisp);
      v.haze=0;v.fog_density=bounded(w,"fog_density",.35f,0,1);v.fog_distance=bounded(w,"fog_distance",1800,100,20000);v.wind=bounded(w,"wind",0,0,100);v.fog_distance=std::max(v.fog_distance,v.fog_start+1);
    }
    // Accept unversioned legacy cosmetics objects and array-form team loadouts.
    const auto& c=j.contains("cosmetics")?j["cosmetics"]:j;
    state.cosmetics.enabled=c.value("enabled",true);state.cosmetics.thirdperson=c.value("thirdperson",false);state.cosmetics.bhop=c.value("bhop",false);
    auto& f=state.cosmetics;
    if(c.contains("player_visuals")&&c["player_visuals"].is_object()){
      const auto& v=c["player_visuals"];auto& pv=f.player_visuals;
      for(const auto& option:visuals::toggles)pv.*option.member=v.value(option.key,pv.*option.member);
      pv.box_style=std::clamp(v.value("box_style",0),0,1);pv.weapon_mode=std::clamp(v.value("weapon_mode",0),0,2);pv.max_distance=bounded(v,"max_distance",150,5,500);pv.thickness=bounded(v,"thickness",1,1,3);pv.arrow_size=bounded(v,"arrow_size",12,6,24);
      if(v.contains("flags")&&v["flags"].is_array())for(std::size_t i=0;i<pv.flags.size()&&i<v["flags"].size();++i)if(v["flags"][i].is_boolean())pv.flags[i]=v["flags"][i].get<bool>();
      const auto read=[&](const char* key,std::array<float,4>& out){ImVec4 value{out[0],out[1],out[2],out[3]};read_color(v,key,value);out={value.x,value.y,value.z,value.w};};
      read("enemy_color",pv.enemy_color);read("ally_color",pv.ally_color);read("hidden_color",pv.hidden_color);
      pv.box_enemy_color=pv.chams_color=pv.enemy_color;pv.box_ally_color=pv.ally_color;pv.box_hidden_color=pv.chams_hidden_color=pv.hidden_color;
      for(const auto& color:visuals::colors)read(color.key,pv.*color.member);
      pv.box_visible_only=v.value("box_visible_only",pv.visible_only);
      pv.box_opacity=bounded(v,"box_opacity",1,0,1);pv.chams_opacity=bounded(v,"chams_opacity",1,0,1);pv.chams_material=std::clamp(v.value("chams_material",0),0,4);if(v.value("material_version",1)<2 && pv.chams_material==1)pv.chams_material=0;
      pv.box_max_distance=bounded(v,"box_max_distance",pv.max_distance,5,500);pv.chams_max_distance=bounded(v,"chams_max_distance",pv.max_distance,5,500);
    }
    f.strafe_assist=c.value("strafe_assist",false)||c.value("air_strafe",false);f.strafe_strength=bounded(c,"strafe_strength",.65f,0,1);
    f.no_spread=c.value("no_spread",false);
    if(c.contains("no_spread_settings") && c["no_spread_settings"].is_object()) {
      const auto& n=c["no_spread_settings"];auto& ns=f.no_spread_settings;
      ns.auto_fire=n.value("auto_fire",true);ns.compensate=n.value("compensate",true);ns.visualize_fov=n.value("visualize_fov",false);ns.fov=bounded(n,"fov",30,1,180);
      ns.through_walls=n.value("through_walls",false);ns.highlight_target=n.value("highlight_target",true);ns.minimum_damage=bounded(n,"minimum_damage",1,1,130);ns.hit_chance=bounded(n,"hit_chance",50,0,100);
      ns.silent_aim=n.value("silent_aim",true);ns.remove_recoil=n.value("remove_recoil",true);ns.multipoint=n.value("multipoint",false);ns.multipoint_scale=bounded(n,"multipoint_scale",.5f,.1f,1);ns.quick_stop=n.value("quick_stop",false);ns.quick_scope=n.value("quick_scope",false);ns.delay_shot=n.value("delay_shot",false);ns.health_damage=n.value("health_damage",false);ns.shot_delay=bounded(n,"shot_delay",0,0,1000);
      if(n.contains("hitboxes") && n["hitboxes"].is_array())for(size_t i=0;i<ns.hitboxes.size() && i<n["hitboxes"].size();++i)if(n["hitboxes"][i].is_boolean())ns.hitboxes[i]=n["hitboxes"][i].get<bool>();
    }
    if(c.contains("legit") && c["legit"].is_object()) {
      const auto& aim_json=c["legit"];auto& s=f.combat_settings.legit;
      for(const auto& t:combat::legit_toggles)s.*t.member=aim_json.value(t.key,s.*t.member);
      for(const auto& n:combat::legit_numbers)s.*n.member=bounded(aim_json,n.key,s.*n.member,n.low,n.high);
      if(aim_json.contains("hitboxes")&&aim_json["hitboxes"].is_array())for(size_t i=0;i<s.hitboxes.size()&&i<aim_json["hitboxes"].size();++i)if(aim_json["hitboxes"][i].is_boolean())s.hitboxes[i]=aim_json["hitboxes"][i].get<bool>();
    }
    if(c.contains("movement_options") && c["movement_options"].is_object()){const auto& movement_json=c["movement_options"];auto& o=f.movement_options;
      for(const auto& n:movement::numbers)o.*n.member=bounded(movement_json,n.key,o.*n.member,n.low,n.high);o.attempt_limit=std::floor(o.attempt_limit);
    }
    if(c.contains("hud")&&c["hud"].is_object()){const auto& h=c["hud"];auto& out=f.hud;out.scope_lines=h.value("scope_lines",false);out.inaccuracy_overlay=h.value("inaccuracy_overlay",false);out.crosshair_mode=static_cast<int>(bounded(h,"crosshair_mode",0,0,1));out.scope_gap=bounded(h,"scope_gap",48,0,240);out.scope_length=bounded(h,"scope_length",70,0,4000);out.scope_thickness=bounded(h,"scope_thickness",1.5f,.5f,8);read_color(h,"scope_color",out.scope_color);read_color(h,"crosshair_color",out.crosshair_color);}
    f.auto_accept=c.value("auto_accept",false);f.preserve_killfeed=c.value("preserve_killfeed",false);f.custom_fov=c.value("custom_fov",false);f.fov=bounded(c,"fov",90,60,140);
    f.viewmodel_override=c.value("viewmodel_override",false);f.viewmodel_fov_override=c.value("viewmodel_fov_override",true);f.viewmodel_x=bounded(c,"viewmodel_x",1,-10,10);f.viewmodel_y=bounded(c,"viewmodel_y",1,-10,10);f.viewmodel_z=bounded(c,"viewmodel_z",-1,-10,10);f.viewmodel_fov=bounded(c,"viewmodel_fov",60,30,120);
    if(c.contains("custom_models"))f.custom_models=c["custom_models"].get<std::array<std::string,2>>();
    if(c.contains("agents")) for(int team=0;team<2;++team) {
      const auto key=team?"CT":"T"; state.cosmetics.agents[team]=c["agents"].value(key,0);
      const auto* agent=cosmetics::find_agent(state.cosmetics.agents[team]);
      if(state.cosmetics.agents[team] && (!agent || !(agent->teams&(1u<<team)))) throw std::runtime_error("Unknown or incompatible agent model");
    }
    if(c.contains("loadouts"))for(int team=0;team<2;++team){const auto& teams=c["loadouts"];const char* key=team?"CT":"T";
        const Json* values=teams.is_array()&&teams.size()>static_cast<unsigned>(team)?&teams[team]:teams.is_object()&&teams.contains(key)?&teams[key]:nullptr;
        if(values){if(!values->is_array()||values->size()>128)throw std::runtime_error("Invalid loadout size");for(const auto& entry:*values){auto s=read_selection(entry,true);if(cosmetics::is_knife(s.definition)&&!(cosmetics::find_weapon(s.definition)->teams&(1u<<team))){s.definition=team?42:59;s.paint=0;}
            if(std::any_of(state.cosmetics.loadouts[team].begin(),state.cosmetics.loadouts[team].end(),[&](const auto& old){return old.definition==s.definition||(cosmetics::is_knife(old.definition)&&cosmetics::is_knife(s.definition));}))throw std::runtime_error("Duplicate config loadout slot");state.cosmetics.loadouts[team].push_back(s);}}
    }
    if(j.contains("inventory")){const auto& i=j["inventory"];state.inventory.both_teams=i.value("both_teams",false);
        for(int team=0;team<2;++team){const char* key=team?"CT":"T";if(i.contains("gloves")&&i["gloves"].contains(key))state.inventory.gloves[team]=read_selection(i["gloves"][key],false);

        }
    }
    for(int team=0;team<2;++team){auto& glove=state.inventory.gloves[team];
      if(glove.definition==0)glove.enabled=false;
      if(glove.enabled){if(!cosmetics::is_glove(glove.definition)||!cosmetics::valid(glove))throw std::runtime_error("Unknown glove or finish in config");
        if(!(cosmetics::find_weapon(glove.definition)->teams&(1u<<team))){glove.definition=team?5029:5028;glove.paint=0;}
        glove.stattrak=-1;glove.name.fill(0);}
      state.cosmetics.gloves[team]=glove;
    }
    cosmetics::validate_configuration(state.cosmetics);
    return state;
}
std::uint64_t state_hash(const MenuState& s){
    // No JSON allocation in the render path. Hash fields, never padding or revision.
    std::uint64_t h=1469598103934665603ull;auto bytes=[&](const auto& v){const auto* p=reinterpret_cast<const unsigned char*>(&v);for(std::size_t i=0;i<sizeof(v);++i){h^=p[i];h*=1099511628211ull;}};
    auto item=[&](const cosmetics::Selection& v){bytes(v.definition);bytes(v.paint);bytes(v.wear);bytes(v.seed);bytes(v.stattrak);for(char c:v.name){bytes(c);if(!c)break;}bytes(v.enabled);};
    bytes(s.cosmetics.enabled);bytes(s.cosmetics.thirdperson);bytes(s.cosmetics.bhop);bytes(s.cosmetics.agents);
    const auto& f=s.cosmetics;bytes(f.no_spread);const auto& ns=f.no_spread_settings;bytes(ns.auto_fire);bytes(ns.compensate);bytes(ns.visualize_fov);bytes(ns.fov);for(bool selected:ns.hitboxes)bytes(selected);bytes(ns.silent_aim);bytes(ns.remove_recoil);bytes(ns.multipoint);bytes(ns.multipoint_scale);bytes(ns.quick_stop);bytes(ns.quick_scope);bytes(ns.delay_shot);bytes(ns.health_damage);bytes(ns.shot_delay);bytes(f.strafe_assist);bytes(f.strafe_strength);bytes(f.auto_accept);bytes(f.preserve_killfeed);bytes(f.hud.scope_lines);bytes(f.hud.inaccuracy_overlay);bytes(f.hud.crosshair_mode);bytes(f.hud.scope_gap);bytes(f.hud.scope_length);bytes(f.hud.scope_thickness);for(float c:f.hud.scope_color)bytes(c);for(float c:f.hud.crosshair_color)bytes(c);bytes(f.custom_fov);bytes(f.fov);bytes(f.viewmodel_override);bytes(f.viewmodel_fov_override);bytes(f.viewmodel_x);bytes(f.viewmodel_y);bytes(f.viewmodel_z);bytes(f.viewmodel_fov);for(const auto& model:f.custom_models){bytes(model.size());for(char c:model)bytes(c);}
    bytes(ns.through_walls);bytes(ns.highlight_target);bytes(ns.minimum_damage);bytes(ns.hit_chance);
    const auto& aim=f.combat_settings;
    for(const auto& t:combat::legit_toggles)bytes(aim.legit.*t.member);
    for(const auto& n:combat::legit_numbers)bytes(aim.legit.*n.member);
    for(bool selected:aim.legit.hitboxes)bytes(selected);
    const auto& movement=s.cosmetics.movement_options;
    for(const auto& n:movement::numbers)bytes(movement.*n.member);
    const auto& pv=f.player_visuals;for(const auto& option:visuals::toggles)bytes(pv.*option.member);
    bytes(pv.box_style);bytes(pv.weapon_mode);bytes(pv.max_distance);bytes(pv.thickness);bytes(pv.arrow_size);for(bool flag:pv.flags)bytes(flag);
    for(const auto& color:visuals::colors)for(float component:pv.*color.member)bytes(component);
    bytes(pv.box_opacity);bytes(pv.chams_opacity);bytes(pv.chams_material);
    bytes(pv.box_max_distance);bytes(pv.chams_max_distance);
    for(const auto& team:s.cosmetics.loadouts){bytes(team.size());for(const auto& v:team)item(v);}
    const auto& w=s.world;bytes(w.skybox_material.size());for(char c:w.skybox_material)bytes(c);bytes(w.skybox);bytes(w.enabled);bytes(w.sky_override);bytes(w.fog_override);bytes(w.vibrance);bytes(w.contrast);bytes(w.gamma);bytes(w.warmth);bytes(w.vignette);bytes(w.sharpness);bytes(w.fog_density);bytes(w.fog_distance);bytes(w.wind);bytes(w.night_mode);bytes(w.night_intensity);bytes(w.weather);bytes(w.weather_intensity);bytes(w.sky_color);bytes(w.fog_color);bytes(w.ambient);bytes(w.brightness);bytes(w.world_override);bytes(w.lighting_override);bytes(w.bloom_override);bytes(w.dof_override);bytes(w.wetness_override);bytes(w.smoke_override);bytes(w.remove_fog);bytes(w.remove_shadows);bytes(w.remove_sky);bytes(w.remove_3d_sky);bytes(w.remove_flash);bytes(w.remove_smoke);bytes(w.remove_ragdolls);bytes(w.exposure);bytes(w.sky_brightness);bytes(w.lighting_intensity);bytes(w.bloom);bytes(w.wetness);bytes(w.fog_start);bytes(w.fog_falloff);bytes(w.dof_near_blurry);bytes(w.dof_near_crisp);bytes(w.dof_far_crisp);bytes(w.dof_far_blurry);bytes(w.world_color);bytes(w.lighting_color);bytes(w.smoke_color);
    bytes(s.inventory.both_teams);for(const auto& g:s.inventory.gloves)item(g);for(char c:s.hotkeys)bytes(c);for(char c:s.preview_tree)bytes(c);return h;
}
}
