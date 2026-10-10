#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
#include "core/features/world/native.hpp"
#include "core/sdk/world_bindings.hpp"
#include <fstream>
#include "core/features/world/environment.hpp"

namespace iris::cosmetics::detail {
void update_skyboxes(Runtime& state,EnvironmentEntity& saved) {
    const auto& settings=state.environment;
    const auto offset=[&](const char* type,const char* name){return environment_offset(state,type,name);};
    const auto color=[](const std::array<float,3>& rgb){
        return 0xff000000u | static_cast<unsigned>(std::clamp(rgb[0],0.f,1.f)*255+.5f) |
            (static_cast<unsigned>(std::clamp(rgb[1],0.f,1.f)*255+.5f)<<8) |
            (static_cast<unsigned>(std::clamp(rgb[2],0.f,1.f)*255+.5f)<<16);
    };

            const auto skybox=std::clamp(settings.skybox,0,static_cast<int>(skyboxes.size())-1);
            if(!settings.sky && !skybox && settings.skybox_material.empty() && settings.sky_brightness==1 && !settings.remove_sky){restore_environment_entity(saved);return;}
            const int material=offset("C_EnvSky","m_hSkyMaterial");
            if(skybox || !settings.skybox_material.empty()){
                const auto path=settings.skybox_material.empty() ? skyboxes[skybox].material : settings.skybox_material.c_str();
                if(const auto resource=load_resource(state,path,0x74616d76))environment_resource(saved,material,resource);
                else restore_environment_field(saved,material);
            }else restore_environment_field(saved,material);
            if(settings.remove_sky)environment_write(saved,offset("C_EnvSky","m_bEnabled"),false);
            else restore_environment_field(saved,offset("C_EnvSky","m_bEnabled"));
            const int tint=offset("C_EnvSky","m_vTintColor"),lighting_tint=offset("C_EnvSky","m_vTintColorLightingOnly");
            if(settings.sky){environment_write(saved,tint,color(settings.sky_color));environment_write(saved,lighting_tint,color(settings.sky_color));}
            else {restore_environment_field(saved,tint);restore_environment_field(saved,lighting_tint);}
            const int brightness=offset("C_EnvSky","m_flBrightnessScale");
            if(brightness>=0 && settings.sky_brightness!=1){
                float baseline=get<float>(saved.address+brightness);
                const auto found=saved.fields.find(brightness);
                if(found!=saved.fields.end() && found->second.written.size()==sizeof(float)){
                    float written{};std::memcpy(&written,found->second.written.data(),sizeof(float));
                    if(baseline==written)std::memcpy(&baseline,found->second.original.data(),sizeof(float));
                }
                environment_write(saved,brightness,baseline*settings.sky_brightness);
            }else restore_environment_field(saved,brightness);
            if(saved.dirty)reinterpret_cast<SubclassChanged>(state.client+bindings::sky_state_changed)(reinterpret_cast<void*>(saved.address));
        }

}
