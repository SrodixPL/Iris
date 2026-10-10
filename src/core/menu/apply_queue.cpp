#include "core/menu/apply_queue.hpp"
#include "core/menu/config_io.hpp"
#include "core/menu/feature_switches.hpp"
#include "core/menu/inventory_model.hpp"
#include "core/menu/image_cache.hpp"
#include <algorithm>
#include <chrono>
#include <set>
#include <Windows.h>
#include <stdexcept>
namespace iris::ui {
namespace {
std::shared_ptr<void> make_event() {
  auto handle=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!handle)throw std::runtime_error("Apply dispatch event creation failed");
  return {handle,[](void* value){CloseHandle(value);}};
}
bool same(ImVec4 a,ImVec4 b) { return a.x==b.x && a.y==b.y && a.z==b.z && a.w==b.w; }
template<class T> bool same(const T& a,const T& b) { return a==b; }
template<class T> void field(const T& before,const T& requested,const T& actual,T& target,bool publish) {
  if(!same(before,requested) && (!publish || same(target,requested))) target=publish ? actual : requested;
}
int slot_key(const cosmetics::Selection& value) { return cosmetics::is_knife(value.definition) ? knife_slot : value.definition; }
std::optional<cosmetics::Selection> find(const std::vector<cosmetics::Selection>& values,int slot) {
  for(const auto& value:values) if(slot_key(value)==slot)return value;return {};
}
void selections(const std::vector<cosmetics::Selection>& before,const std::vector<cosmetics::Selection>& requested,const std::vector<cosmetics::Selection>& actual,std::vector<cosmetics::Selection>& target,bool publish) {
  std::set<int> slots;for(const auto& value:before)slots.insert(slot_key(value));for(const auto& value:requested)slots.insert(slot_key(value));
  for(int slot:slots) {
    const auto old=find(before,slot),next=find(requested,slot);
    if(old==next || (publish && find(target,slot)!=next))continue;
    const auto value=publish ? find(actual,slot) : next;
    std::erase_if(target,[&](const auto& entry){return slot_key(entry)==slot;});if(value)target.push_back(*value);
  }
}
void merge(const MenuState& before,const MenuState& requested,const MenuState& actual,MenuState& target,bool publish) {
  feature_switches::reconcile(before,requested,actual,target,publish);
  field(before.world.skybox_material,requested.world.skybox_material,actual.world.skybox_material,target.world.skybox_material,publish);
  field(before.world.enabled,requested.world.enabled,actual.world.enabled,target.world.enabled,publish);
  field(before.world.sky_override,requested.world.sky_override,actual.world.sky_override,target.world.sky_override,publish);
  field(before.world.fog_override,requested.world.fog_override,actual.world.fog_override,target.world.fog_override,publish);
  field(before.world.vibrance,requested.world.vibrance,actual.world.vibrance,target.world.vibrance,publish);
  field(before.world.contrast,requested.world.contrast,actual.world.contrast,target.world.contrast,publish);
  field(before.world.gamma,requested.world.gamma,actual.world.gamma,target.world.gamma,publish);
  field(before.world.warmth,requested.world.warmth,actual.world.warmth,target.world.warmth,publish);
  field(before.world.vignette,requested.world.vignette,actual.world.vignette,target.world.vignette,publish);
  field(before.world.sharpness,requested.world.sharpness,actual.world.sharpness,target.world.sharpness,publish);
  field(before.world.haze,requested.world.haze,actual.world.haze,target.world.haze,publish);
  field(before.world.fog_density,requested.world.fog_density,actual.world.fog_density,target.world.fog_density,publish);
  field(before.world.fog_distance,requested.world.fog_distance,actual.world.fog_distance,target.world.fog_distance,publish);
  field(before.world.wind,requested.world.wind,actual.world.wind,target.world.wind,publish);
  field(before.world.night_mode,requested.world.night_mode,actual.world.night_mode,target.world.night_mode,publish);
  field(before.world.night_intensity,requested.world.night_intensity,actual.world.night_intensity,target.world.night_intensity,publish);
  field(before.world.weather,requested.world.weather,actual.world.weather,target.world.weather,publish);
  field(before.world.skybox,requested.world.skybox,actual.world.skybox,target.world.skybox,publish);
  field(before.world.weather_intensity,requested.world.weather_intensity,actual.world.weather_intensity,target.world.weather_intensity,publish);
  field(before.world.sky_color,requested.world.sky_color,actual.world.sky_color,target.world.sky_color,publish);
  field(before.world.fog_color,requested.world.fog_color,actual.world.fog_color,target.world.fog_color,publish);
  field(before.world.ambient,requested.world.ambient,actual.world.ambient,target.world.ambient,publish);
  field(before.world.brightness,requested.world.brightness,actual.world.brightness,target.world.brightness,publish);
  field(before.world.world_override,requested.world.world_override,actual.world.world_override,target.world.world_override,publish);
  field(before.world.lighting_override,requested.world.lighting_override,actual.world.lighting_override,target.world.lighting_override,publish);
  field(before.world.bloom_override,requested.world.bloom_override,actual.world.bloom_override,target.world.bloom_override,publish);
  field(before.world.dof_override,requested.world.dof_override,actual.world.dof_override,target.world.dof_override,publish);
  field(before.world.wetness_override,requested.world.wetness_override,actual.world.wetness_override,target.world.wetness_override,publish);
  field(before.world.smoke_override,requested.world.smoke_override,actual.world.smoke_override,target.world.smoke_override,publish);
  field(before.world.remove_fog,requested.world.remove_fog,actual.world.remove_fog,target.world.remove_fog,publish);
  field(before.world.remove_shadows,requested.world.remove_shadows,actual.world.remove_shadows,target.world.remove_shadows,publish);
  field(before.world.remove_sky,requested.world.remove_sky,actual.world.remove_sky,target.world.remove_sky,publish);
  field(before.world.remove_3d_sky,requested.world.remove_3d_sky,actual.world.remove_3d_sky,target.world.remove_3d_sky,publish);
  field(before.world.remove_flash,requested.world.remove_flash,actual.world.remove_flash,target.world.remove_flash,publish);
  field(before.world.remove_smoke,requested.world.remove_smoke,actual.world.remove_smoke,target.world.remove_smoke,publish);
  field(before.world.remove_ragdolls,requested.world.remove_ragdolls,actual.world.remove_ragdolls,target.world.remove_ragdolls,publish);
  field(before.world.exposure,requested.world.exposure,actual.world.exposure,target.world.exposure,publish);
  field(before.world.sky_brightness,requested.world.sky_brightness,actual.world.sky_brightness,target.world.sky_brightness,publish);
  field(before.world.lighting_intensity,requested.world.lighting_intensity,actual.world.lighting_intensity,target.world.lighting_intensity,publish);
  field(before.world.bloom,requested.world.bloom,actual.world.bloom,target.world.bloom,publish);
  field(before.world.wetness,requested.world.wetness,actual.world.wetness,target.world.wetness,publish);
  field(before.world.fog_start,requested.world.fog_start,actual.world.fog_start,target.world.fog_start,publish);
  field(before.world.fog_falloff,requested.world.fog_falloff,actual.world.fog_falloff,target.world.fog_falloff,publish);
  field(before.world.dof_near_blurry,requested.world.dof_near_blurry,actual.world.dof_near_blurry,target.world.dof_near_blurry,publish);
  field(before.world.dof_near_crisp,requested.world.dof_near_crisp,actual.world.dof_near_crisp,target.world.dof_near_crisp,publish);
  field(before.world.dof_far_crisp,requested.world.dof_far_crisp,actual.world.dof_far_crisp,target.world.dof_far_crisp,publish);
  field(before.world.dof_far_blurry,requested.world.dof_far_blurry,actual.world.dof_far_blurry,target.world.dof_far_blurry,publish);
  field(before.world.world_color,requested.world.world_color,actual.world.world_color,target.world.world_color,publish);
  field(before.world.lighting_color,requested.world.lighting_color,actual.world.lighting_color,target.world.lighting_color,publish);
  field(before.world.smoke_color,requested.world.smoke_color,actual.world.smoke_color,target.world.smoke_color,publish);
  field(before.cosmetics.enabled,requested.cosmetics.enabled,actual.cosmetics.enabled,target.cosmetics.enabled,publish);
  field(before.cosmetics.thirdperson,requested.cosmetics.thirdperson,actual.cosmetics.thirdperson,target.cosmetics.thirdperson,publish);
  field(before.cosmetics.bhop,requested.cosmetics.bhop,actual.cosmetics.bhop,target.cosmetics.bhop,publish);
  field(before.cosmetics.no_spread,requested.cosmetics.no_spread,actual.cosmetics.no_spread,target.cosmetics.no_spread,publish);
  const auto& nb=before.cosmetics.no_spread_settings;const auto& nr=requested.cosmetics.no_spread_settings;const auto& na=actual.cosmetics.no_spread_settings;auto& nt=target.cosmetics.no_spread_settings;
  field(nb.auto_fire,nr.auto_fire,na.auto_fire,nt.auto_fire,publish);field(nb.compensate,nr.compensate,na.compensate,nt.compensate,publish);
  field(nb.visualize_fov,nr.visualize_fov,na.visualize_fov,nt.visualize_fov,publish);field(nb.through_walls,nr.through_walls,na.through_walls,nt.through_walls,publish);field(nb.highlight_target,nr.highlight_target,na.highlight_target,nt.highlight_target,publish);
  field(nb.fov,nr.fov,na.fov,nt.fov,publish);field(nb.minimum_damage,nr.minimum_damage,na.minimum_damage,nt.minimum_damage,publish);field(nb.hit_chance,nr.hit_chance,na.hit_chance,nt.hit_chance,publish);
  field(nb.silent_aim,nr.silent_aim,na.silent_aim,nt.silent_aim,publish);field(nb.remove_recoil,nr.remove_recoil,na.remove_recoil,nt.remove_recoil,publish);field(nb.multipoint,nr.multipoint,na.multipoint,nt.multipoint,publish);field(nb.multipoint_scale,nr.multipoint_scale,na.multipoint_scale,nt.multipoint_scale,publish);field(nb.quick_stop,nr.quick_stop,na.quick_stop,nt.quick_stop,publish);field(nb.quick_scope,nr.quick_scope,na.quick_scope,nt.quick_scope,publish);field(nb.delay_shot,nr.delay_shot,na.delay_shot,nt.delay_shot,publish);field(nb.health_damage,nr.health_damage,na.health_damage,nt.health_damage,publish);field(nb.shot_delay,nr.shot_delay,na.shot_delay,nt.shot_delay,publish);
  for(size_t i=0;i<nb.hitboxes.size();++i)field(nb.hitboxes[i],nr.hitboxes[i],na.hitboxes[i],nt.hitboxes[i],publish);
  const auto& ab=before.cosmetics.combat_settings;const auto& ar=requested.cosmetics.combat_settings;const auto& aa=actual.cosmetics.combat_settings;auto& at=target.cosmetics.combat_settings;
  for(const auto& t:combat::legit_toggles)field(ab.legit.*t.member,ar.legit.*t.member,aa.legit.*t.member,at.legit.*t.member,publish);
  for(const auto& n:combat::legit_numbers)field(ab.legit.*n.member,ar.legit.*n.member,aa.legit.*n.member,at.legit.*n.member,publish);
  for(size_t i=0;i<ab.legit.hitboxes.size();++i)field(ab.legit.hitboxes[i],ar.legit.hitboxes[i],aa.legit.hitboxes[i],at.legit.hitboxes[i],publish);
  const auto& mb=before.cosmetics.movement_options;const auto& mr=requested.cosmetics.movement_options;const auto& ma=actual.cosmetics.movement_options;auto& mt=target.cosmetics.movement_options;
  for(const auto& n:movement::numbers)field(mb.*n.member,mr.*n.member,ma.*n.member,mt.*n.member,publish);
  const auto& vb=before.cosmetics.player_visuals;const auto& vr=requested.cosmetics.player_visuals;const auto& va=actual.cosmetics.player_visuals;auto& vt=target.cosmetics.player_visuals;
  for(const auto& option:visuals::toggles)field(vb.*option.member,vr.*option.member,va.*option.member,vt.*option.member,publish);
  field(vb.box_style,vr.box_style,va.box_style,vt.box_style,publish);field(vb.max_distance,vr.max_distance,va.max_distance,vt.max_distance,publish);
  field(vb.weapon_mode,vr.weapon_mode,va.weapon_mode,vt.weapon_mode,publish);
  field(vb.thickness,vr.thickness,va.thickness,vt.thickness,publish);field(vb.arrow_size,vr.arrow_size,va.arrow_size,vt.arrow_size,publish);
  for(std::size_t i=0;i<vb.flags.size();++i)field(vb.flags[i],vr.flags[i],va.flags[i],vt.flags[i],publish);
  for(const auto& color:visuals::colors)field(vb.*color.member,vr.*color.member,va.*color.member,vt.*color.member,publish);
  field(vb.box_opacity,vr.box_opacity,va.box_opacity,vt.box_opacity,publish);field(vb.chams_opacity,vr.chams_opacity,va.chams_opacity,vt.chams_opacity,publish);field(vb.chams_material,vr.chams_material,va.chams_material,vt.chams_material,publish);
  field(vb.box_max_distance,vr.box_max_distance,va.box_max_distance,vt.box_max_distance,publish);field(vb.chams_max_distance,vr.chams_max_distance,va.chams_max_distance,vt.chams_max_distance,publish);
  field(before.cosmetics.strafe_assist,requested.cosmetics.strafe_assist,actual.cosmetics.strafe_assist,target.cosmetics.strafe_assist,publish);
  field(before.cosmetics.auto_accept,requested.cosmetics.auto_accept,actual.cosmetics.auto_accept,target.cosmetics.auto_accept,publish);
  field(before.cosmetics.preserve_killfeed,requested.cosmetics.preserve_killfeed,actual.cosmetics.preserve_killfeed,target.cosmetics.preserve_killfeed,publish);
  const auto& hb=before.cosmetics.hud;const auto& hr=requested.cosmetics.hud;const auto& ha=actual.cosmetics.hud;auto& ht=target.cosmetics.hud;
  field(hb.scope_lines,hr.scope_lines,ha.scope_lines,ht.scope_lines,publish);field(hb.inaccuracy_overlay,hr.inaccuracy_overlay,ha.inaccuracy_overlay,ht.inaccuracy_overlay,publish);field(hb.crosshair_mode,hr.crosshair_mode,ha.crosshair_mode,ht.crosshair_mode,publish);
  field(hb.scope_gap,hr.scope_gap,ha.scope_gap,ht.scope_gap,publish);field(hb.scope_length,hr.scope_length,ha.scope_length,ht.scope_length,publish);field(hb.scope_thickness,hr.scope_thickness,ha.scope_thickness,ht.scope_thickness,publish);
  for(std::size_t i=0;i<hb.scope_color.size();++i){field(hb.scope_color[i],hr.scope_color[i],ha.scope_color[i],ht.scope_color[i],publish);field(hb.crosshair_color[i],hr.crosshair_color[i],ha.crosshair_color[i],ht.crosshair_color[i],publish);}
  field(before.cosmetics.strafe_strength,requested.cosmetics.strafe_strength,actual.cosmetics.strafe_strength,target.cosmetics.strafe_strength,publish);
  field(before.cosmetics.custom_fov,requested.cosmetics.custom_fov,actual.cosmetics.custom_fov,target.cosmetics.custom_fov,publish);
  field(before.cosmetics.viewmodel_fov_override,requested.cosmetics.viewmodel_fov_override,actual.cosmetics.viewmodel_fov_override,target.cosmetics.viewmodel_fov_override,publish);
  field(before.cosmetics.viewmodel_override,requested.cosmetics.viewmodel_override,actual.cosmetics.viewmodel_override,target.cosmetics.viewmodel_override,publish);
  field(before.cosmetics.fov,requested.cosmetics.fov,actual.cosmetics.fov,target.cosmetics.fov,publish);
  field(before.cosmetics.viewmodel_x,requested.cosmetics.viewmodel_x,actual.cosmetics.viewmodel_x,target.cosmetics.viewmodel_x,publish);
  field(before.cosmetics.viewmodel_y,requested.cosmetics.viewmodel_y,actual.cosmetics.viewmodel_y,target.cosmetics.viewmodel_y,publish);
  field(before.cosmetics.viewmodel_z,requested.cosmetics.viewmodel_z,actual.cosmetics.viewmodel_z,target.cosmetics.viewmodel_z,publish);
  field(before.cosmetics.viewmodel_fov,requested.cosmetics.viewmodel_fov,actual.cosmetics.viewmodel_fov,target.cosmetics.viewmodel_fov,publish);
  field(before.inventory.both_teams,requested.inventory.both_teams,actual.inventory.both_teams,target.inventory.both_teams,publish);
  for(int team=0;team<2;++team) {
    field(before.inventory.gloves[team],requested.inventory.gloves[team],actual.inventory.gloves[team],target.inventory.gloves[team],publish);
    field(before.cosmetics.agents[team],requested.cosmetics.agents[team],actual.cosmetics.agents[team],target.cosmetics.agents[team],publish);
    field(before.cosmetics.custom_models[team],requested.cosmetics.custom_models[team],actual.cosmetics.custom_models[team],target.cosmetics.custom_models[team],publish);
    selections(before.cosmetics.loadouts[team],requested.cosmetics.loadouts[team],actual.cosmetics.loadouts[team],target.cosmetics.loadouts[team],publish);
  }
  target.cosmetics.gloves=target.inventory.gloves;
}
}
ApplyQueue::ApplyQueue(MenuState initial,Submit submit,Status status,Roster roster):requested_(initial),wake_(make_event()),worker_([this,initial=std::move(initial),submit,status,roster]() mutable {
  MenuRuntime owned;owned.values=std::move(initial);owned.startup_loading=true;owned.submit_adapter=submit;owned.gameplay_wired=true;
  std::uint64_t preview_revision{};std::shared_ptr<const cosmetics::PreviewImage> preview;
  for(;;) {
    if(read_.load(std::memory_order_relaxed)==written_.load(std::memory_order_acquire) && !refresh_.exchange(false) && !stopping_)
      WaitForSingleObject(wake_.get(),500);
    std::optional<Job> job;
    const auto read=read_.load(std::memory_order_relaxed);
    if(read!=written_.load(std::memory_order_acquire)) {
      job=std::move(jobs_[read%capacity]);jobs_[read%capacity].reset();read_.store(read+1,std::memory_order_release);
    } else if(stopping_)return;
    if(job) {
      ApplyResult result;result.before=std::move(job->before);result.requested=std::move(job->requested);result.context=std::move(job->context);result.load=job->load;
      const auto previous=owned.values;
      // Merge deltas so failed earlier requests cannot reappear in later queued edits.
      merge(result.before,result.requested,owned.values,owned.values,false);
      if(!submit_features(owned)){owned.values=previous;result.error=owned.error;}
      result.actual=owned.values;
      {std::lock_guard lock(mutex_);results_.push_back(std::move(result));}
    }
    try{
      if(GetTickCount64()<preview_until_.load())if(const auto request=preview_request_.load())cosmetics::request_preview(*request);
      auto image=cosmetics::preview_image(preview_revision);
      if(image.revision && image.revision!=preview_revision){preview_revision=image.revision;preview=std::make_shared<const cosmetics::PreviewImage>(std::move(image));}
    }catch(const std::exception&){preview.reset();}
    auto current=RuntimeSnapshot{status(),roster(),preview};
    {std::lock_guard lock(mutex_);snapshot_=std::move(current);}
  }
}) {}
ApplyQueue::~ApplyQueue(){stopping_=true;SetEvent(wake_.get());if(worker_.joinable())worker_.join();}
bool ApplyQueue::submit(const MenuState& requested,const std::string& context,bool load) {
  const auto written=written_.load(std::memory_order_relaxed);
  if(stopping_ || written-read_.load(std::memory_order_acquire)>=capacity)return false;
  // One UI producer and one worker consumer publish ownership without a render-thread lock.
  jobs_[written%capacity]=Job{requested_,requested,context,load};written_.store(written+1,std::memory_order_release);
  requested_=requested;++pending_;SetEvent(wake_.get());return true;
}
std::optional<ApplyResult> ApplyQueue::poll() {
  std::unique_lock lock(mutex_,std::try_to_lock);if(!lock || results_.empty())return {};
  auto result=std::move(results_.front());results_.pop_front();--pending_;if(!pending_)requested_=result.actual;return result;
}
void ApplyQueue::preview(const cosmetics::Selection& selection){
  preview_until_=GetTickCount64()+800;
  const auto previous=preview_request_.load();
  if(!previous || *previous!=selection){preview_request_.store(std::make_shared<const cosmetics::Selection>(selection));SetEvent(wake_.get());}
}
void ApplyQueue::refresh(){refresh_=true;SetEvent(wake_.get());}
std::optional<RuntimeSnapshot> ApplyQueue::snapshot(){std::unique_lock lock(mutex_,std::try_to_lock);if(!lock)return {};return snapshot_;}
void publish_apply(MenuState& displayed,const ApplyResult& result){merge(result.before,result.requested,result.actual,displayed,true);displayed.cosmetics.revision=result.actual.cosmetics.revision;}
void tick_bindings(MenuRuntime& state) {
  if(!state.apply_queue)return;
  bool completed=false;std::optional<MenuState> settled;
  while(auto result=state.apply_queue->poll()) {
    publish_apply(state.values,*result);settled=result->actual;completed=true;if(result->load)state.load_accepted=result->error.empty();
    if(!result->error.empty()){state.error=result->context.empty() ? result->error : result->context+": "+result->error;state.inventory.error=state.error;state.inventory.wear_editing=false;state.feedback="Apply failed; previous values restored.";}
    else if(state.error.empty())state.feedback="Applied";
  }
  if(auto snapshot=state.apply_queue->snapshot()) {state.runtime_status=std::move(snapshot->status);state.equipped_roster=std::move(snapshot->roster);state.preview_image=std::move(snapshot->preview);}
  state.apply_pending=state.apply_queue->pending();
  state.inventory.both_teams=state.values.inventory.both_teams;
  if(completed && !state.apply_pending) {
    auto editors=std::move(state.values.inventory.editors);auto hotkeys=std::move(state.values.hotkeys);state.values=std::move(*settled);state.values.inventory.editors=std::move(editors);state.values.hotkeys=std::move(hotkeys);
    for(int team=0;team<2;++team)for(auto& editor:state.values.inventory.editors[team]) {
      if(const auto* actual=applied_selection(state.values,team,editor.slot)){editor.value=*actual;if(actual->stattrak>=0)editor.cached_counter=actual->stattrak;}
      else {editor.value={};editor.value.definition=editor.slot>=0?editor.slot:0;editor.cached_counter=0;}
    }
    state.persist_deferred=false;
    if(state.finish_load){auto finish=std::move(state.finish_load);finish(state.load_accepted);}
    else if(state_hash(state.values)!=state.saved_hash)persist_features(state);
    state.dirty=state_hash(state.values)!=state.saved_hash;
  }
  state.inventory.both_teams=state.values.inventory.both_teams;
}
}
