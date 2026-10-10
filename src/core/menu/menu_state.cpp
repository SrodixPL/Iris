#include "core/menu/menu_state.hpp"
#include "core/menu/config_io.hpp"
#include "core/menu/apply_queue.hpp"
#include "core/menu/feature_switches.hpp"
#include <exception>
namespace iris::ui {
namespace {
cosmetics::Environment environment(const WorldState& w) {
    cosmetics::Environment native;
    native.skybox_material=w.skybox_material;
    native.enabled=w.enabled;native.sky=w.sky_override;native.fog=w.fog_override;
    native.sky_color={w.sky_color.x,w.sky_color.y,w.sky_color.z};native.fog_color={w.fog_color.x,w.fog_color.y,w.fog_color.z};
    native.ambient=w.ambient;native.sky_brightness=w.sky_brightness;native.fog_density=w.fog_density;native.fog_distance=w.fog_distance;native.wind=w.wind;native.skybox=w.skybox;native.weather=w.weather;native.weather_intensity=w.weather_intensity;
    native.world_override=w.world_override;
    native.lighting_override=w.lighting_override;
    native.bloom_override=w.bloom_override;
    native.dof_override=w.dof_override;
    native.wetness_override=w.wetness_override;
    native.smoke_override=w.smoke_override;
    native.remove_fog=w.remove_fog;
    native.remove_shadows=w.remove_shadows;
    native.remove_sky=w.remove_sky;
    native.remove_3d_sky=w.remove_3d_sky;
    native.remove_flash=w.remove_flash;
    native.remove_smoke=w.remove_smoke;
    native.remove_ragdolls=w.remove_ragdolls;
    native.exposure=w.exposure;
    native.lighting_intensity=w.lighting_intensity;
    native.bloom=w.bloom;
    native.wetness=w.wetness;
    native.fog_start=w.fog_start;
    native.fog_falloff=w.fog_falloff;
    native.dof_near_blurry=w.dof_near_blurry;
    native.dof_near_crisp=w.dof_near_crisp;
    native.dof_far_crisp=w.dof_far_crisp;
    native.dof_far_blurry=w.dof_far_blurry;
    native.world_color={w.world_color.x,w.world_color.y,w.world_color.z};
    native.lighting_color={w.lighting_color.x,w.lighting_color.y,w.lighting_color.z};
    native.smoke_color={w.smoke_color.x,w.smoke_color.y,w.smoke_color.z};
    return native;
}
}
bool submit_features(MenuRuntime& state){
    if(!state.gameplay_wired){state.error.clear();state.dirty=true;persist_features(state);return true;}
    feature_switches::project(state.values);
    if(state.apply_queue){if(!state.apply_queue->submit(state.values,state.apply_context,state.load_submission)){state.error="Apply queue is busy; try again.";return false;}if(!state.apply_pending){state.error.clear();state.inventory.error.clear();}state.apply_pending=true;state.dirty=true;state.persist_deferred=true;state.feedback="Applying...";return true;}
    try{state.values.cosmetics.gloves=state.values.inventory.gloves;++state.values.cosmetics.revision;(state.submit_adapter?state.submit_adapter:cosmetics::submit)(feature_switches::runtime_configuration(state.values));cosmetics::submit_environment(environment(state.values.world));state.error.clear();state.dirty=true;persist_features(state);return true;}
    catch(const std::exception& error){state.error=error.what();return false;}
}
}
