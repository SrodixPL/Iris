#include "core/sdk/binding_values.hpp"
#include "core/features/combat/nospread.hpp"
#include "core/features/combat/spread_solver.hpp"
#include "core/features/combat/targeting.hpp"
#include "core/features/combat/aim_math.hpp"
#include "core/features/combat/workers.hpp"
#include <unordered_map>
#include "core/features/combat/shot_policy.hpp"
#include "core/features/combat/scan_policy.hpp"
#include "core/features/movement/requests.hpp"
#include "core/features/movement/landing_policy.hpp"
#include "core/features/movement/command_policy.hpp"
#include "core/features/player/visuals.hpp"
#include "core/features/player/camera.hpp"
#include "core/sdk/experimental_commands.hpp"
#include "core/sdk/client.hpp"
#include "core/sdk/entities.hpp"
#include "core/sdk/generated/client_bindings.hpp"
#include "core/log.hpp"
#include "core/render/renderer.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <memory>
#include <mutex>
#include <vector>
#include <chrono>
#include <deque>
#include <limits>
#include <intrin.h>
namespace iris::nospread {
namespace {
using namespace sdk;
using namespace sdk::experimental;
template<class T> T read_native(Address address) {
    T value{};
    if(!memory::read_local(address,std::as_writable_bytes(std::span{&value,std::size_t{1}})))throw std::runtime_error("Game-thread object became unreadable; combat suspended.");
    return value;
}
template<class T> bool write_native(Address address,const T& value) noexcept {
    return memory::write_local(address,std::as_bytes(std::span{&value,std::size_t{1}}));
}
using Command=sdk::experimental::Command;
namespace reviewed=sdk::experimental::bindings;
const auto& seed_rva=sdk::binding_values::combat_seed_rva;
const auto& spread_rva=sdk::binding_values::combat_spread_rva;
const auto& inaccuracy_rva=sdk::binding_values::combat_inaccuracy_rva;
const auto& base_spread_rva=sdk::binding_values::combat_base_spread_rva;
const auto& punch_rva=sdk::binding_values::combat_punch_rva;
const auto& trace_rva=sdk::binding_values::combat_trace_rva;
const auto& filter_rva=sdk::binding_values::combat_filter_rva;
using CreateMove=void(__fastcall*)(void*,int,bool);
using Prepare=void(__fastcall*)(Address);
using Encode=bool(__fastcall*)(Address,Address,Address,int);
struct Fields {int pawn{},player_pawn{},health{},team{},scene{},origin{},view{},dormant{},services{},active{},clip{},reload{},next{},ratio{},recoil{},definition{},punch{},steam{},hltv{},immune{},bullets{},model{},model_handle{},camera{},camera_fov{},damage{},head_multiplier{},armor_ratio{},range{},range_modifier{},penetration{},armor{},items{},helmet{},velocity{},flags{},move_type{},movement{},command_forward{},command_left{},scoped{},flash{},identity{},collision{},minimum{},maximum{},interacts{},gravity_scale{},duck{},maximum_speed{},friction{},movement_button_value{};};
// The legacy protobuf command number may be absent. Encoding matches seed and tick;
// raw command number and tick deduplicate preparation without treating the seed as a sequence.
struct Plan {int seed{},tick{},history_index{},shot_tick{};Address pawn{},weapon{};std::uint32_t target{};Solution solution;Vector point{},view{};Settings config;bool automatic{};int region{};std::uint32_t pawn_handle{},weapon_handle{};std::int64_t raw_number{};bool camera_applied{};};
struct ShotRequest {std::int64_t number{};int tick{},seed{};std::uint32_t pawn{},weapon{};Settings config;bool stop{},blocked{},scope{};};
struct ViewSample {int tick{};float fraction{};Vector sent{},source{};};
struct ViewPlan {int seed{},tick{};Address pawn{},weapon{};std::uint32_t target{};Vector point{},sent{},delta{};combat::Settings config;bool rage{},camera_applied{};std::vector<ViewSample> samples;std::uint32_t pawn_handle{},weapon_handle{};std::int64_t raw_number{};Vector source{},camera_source{},command_sent{};};
struct TriggerPlan {int seed{},tick{},player_tick{};float player_fraction{};std::uint32_t pawn{},weapon{},target{};Vector view{};std::shared_ptr<const combat::Settings> generation;};
struct MovementObservation {std::int64_t number{};int tick{},seed{};std::uint32_t pawn{};bool grounded{};Vector origin{},velocity{};float duck{};};
struct MovementEdit {std::int64_t number{};int tick{},seed{};std::uint32_t pawn{};movement::Axes terminal{},baseline{};movement::Settings config;};
struct SteeringPlan {std::int64_t number{};int tick{},seed{};std::uint32_t pawn{};movement::Settings config;bool rage_stop{};float yaw{};movement::SteeringDecision decision;};
struct BulletStorage {
    alignas(16) std::array<std::byte,0x2000> bytes{};
    BulletStorage() {
        const auto set=[&](std::size_t offset,const auto& value){std::memcpy(bytes.data()+offset,&value,sizeof(value));};
        set(0x1d10,1.f);set(4,52.f);set(8,reinterpret_cast<Address>(bytes.data()+0x18));set(0x10,128);set(0x14,0x80000000u);
        set(0x1c28,reinterpret_cast<Address>(bytes.data()+0x1c38));set(0x1c30,8);set(0x1c34,0x80000000u);
    }
    void reset() noexcept {
        std::array<std::byte,0x18> contacts{},hits{};
        std::memcpy(contacts.data(),bytes.data(),contacts.size());std::memcpy(hits.data(),bytes.data()+0x1c20,hits.size());
        bytes.fill(std::byte{});
        std::memcpy(bytes.data(),contacts.data(),contacts.size());std::memcpy(bytes.data()+0x1c20,hits.data(),hits.size());
        const int zero{};const float initial=52.f;
        std::memcpy(bytes.data(),&zero,4);std::memcpy(bytes.data()+4,&initial,4);const float fraction=1.f;std::memcpy(bytes.data()+0x1d10,&fraction,4);std::memcpy(bytes.data()+0x1c20,&zero,4);std::memcpy(bytes.data()+0x1c24,&zero,4);
    }
    BulletStorage(const BulletStorage&)=delete;
    BulletStorage& operator=(const BulletStorage&)=delete;
};
enum class WireObject {proto,base,angles,buttons,history,history_angles,move};
struct WireEdit {WireObject object{};int index{};std::size_t offset{};std::vector<std::byte> bytes;};
struct WireSnapshot {int seed{},tick{},move_count{};std::vector<WireEdit> edits;std::vector<std::byte> crc;};
struct JumpAction {std::uint64_t button{};bool pressed{};float when{};};
constexpr std::uint64_t movement_directions=(1ull<<3)|(1ull<<4)|(1ull<<9)|(1ull<<10);
struct JumpPlan {
    std::int64_t number{};int tick{},seed{};std::uint32_t pawn{};movement::Settings config;
    bool owns_jump{},owns_duck{},jump{},resilient_jump{};std::vector<JumpAction> actions;
    std::uint64_t original{},written{};
    bool held{};
};
struct State {
    Address client{},trace_manager{};
    const char* native_stage{"idle"};
    Fields f;
    BulletStorage bullet_storage;
    std::recursive_mutex mutex;
    std::unique_ptr<hooks::Hook> move,prepare,encode,prediction;
    CreateMove original_move{};Prepare original_prepare{};Encode original_encode{};
    void(__fastcall* original_prediction)(Address,Address){};
    std::uint64_t prediction_calls{};
    double prediction_plan_ms{},prediction_plan_peak{},prediction_native_ms{},prediction_native_peak{};
    std::uint64_t native_steering{};
    std::uint64_t physical_directions{},last_steering_directions{};
    std::shared_ptr<const movement::Settings> input_generation;
    bool physical_input_valid{};
    bool prediction_active{};
    std::optional<Plan> plan;
    combat::RageGate rage_gate;
    Settings gate_config;
    std::optional<ShotRequest> shot_request;
    std::deque<WireSnapshot> wire;
    std::optional<ViewPlan> view_plan;
    std::shared_ptr<const combat::Settings> view_generation;
    std::optional<std::array<std::int64_t,5>> legit_decision;
    std::uint64_t jump_writes{},jump_retries{};
    Vector observed_velocity{};int observed_landing{};bool observed_grounded{};
    bool observed_held{},observed_activation{},observed_native_input{};
    float observed_support{-1},observed_cap{},observed_acceleration{},movement_basis_yaw{};
    movement::Axes observed_input{};
    combat::RecoilState recoil;
    Address view_pawn{},view_weapon{};
    std::uint32_t view_pawn_handle{},view_weapon_handle{};
    std::uint32_t locked_target{};
    Vector smooth_rates{};
    std::chrono::steady_clock::time_point last_view{},acquired{};
    combat::ShotConfirmation shot_confirmation;
    combat::workers::Aim aim_worker{combat::workers::RankAim{}};
    combat::workers::Trigger trigger_worker{combat::workers::ConfirmTrigger{}};
    std::uint32_t trigger_seen{};std::uint64_t trigger_epoch{};
    std::deque<TriggerPlan> trigger_plans;
    float acquisition_delay{},trigger_delay{};
    Vector previous_camera{};
    bool previous_camera_valid{};
    std::chrono::steady_clock::time_point last_trigger{};
    std::int64_t processed{};int processed_tick{},processed_seed{};Address processed_pawn{};std::uint32_t processed_handle{},processed_weapon{};Settings processed_config;
    ULONGLONG preview_sampled{};
    std::int64_t observed_command{};int observed_legacy{},observed_seed{},observed_history{};std::uint32_t observed_presence{};
    movement::Steering steering;std::optional<std::array<float,2>> movement_axes;std::int64_t movement_number{};int movement_tick{};std::uint32_t movement_handle{};
    movement::Settings movement_config;movement::JumpState jump_state;std::optional<JumpPlan> jump_plan;
    std::deque<JumpPlan> jump_plans;
    std::deque<MovementObservation> movement_observations;
    std::deque<SteeringPlan> steering_plans;
    std::deque<MovementEdit> movement_edits;
    std::shared_ptr<const movement::Settings> movement_generation;
    std::vector<Step> steering_steps;
    int movement_seed{};
    std::array<float,2> observed_wish{},observed_impulse{},observed_move{};int observed_steps{};bool observed_analog{};std::int64_t observed_move_number{};std::uint64_t observed_keys{},observed_corrected_keys{};
    ULONGLONG logged{},performance_log{};
    ULONGLONG rejection_logged{};std::uint64_t rejected_commands{};
    ULONGLONG hud_sampled{},transition_logged{};std::uint32_t hud_pawn{},hud_weapon{};bool hud_scoped{};
    const char* last_movement_status{};
    std::optional<combat::Settings> logged_aim;std::optional<movement::Settings> logged_movement;
    std::uint64_t prepared_shots{},encoded_shots{},rejected_shots{},chance_samples{},chance_hits{},chance_budget{},native_encode_failures{};
    bool encoding_shot{},encoding_rejected{};float observed_accuracy{},observed_spread{},observed_damage{};std::uint32_t observed_target{};
    int scan_tick{-1};std::uint32_t scan_pawn{},scan_weapon{};Settings scan_config;bool scan_manual{};
    std::uint64_t scans{},scan_skips{},scan_exhausted{},scan_candidates{};
    std::uint64_t movement_commands{},frames{},rays{},penetration_traces{},wall_hits{},wall_stops{},empty_traces{},other_contacts{},seed_calls{};
    std::uint64_t encodes{};
    std::array<double,4> process_stages{},process_stage_peaks{};
    std::array<double,6> encode_stages{},encode_stage_peaks{};
};
State* state{};
bool context() {
    DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    const auto aim=aim_settings.load();const auto motion=movement_settings.load();
    return !stopped && (hud_enabled || (motion && motion->active()) || (aim && aim->active())) && pid==GetCurrentProcessId() && !renderer::menu_open();
}
Address controller_at(int index) {
    const auto chunks=read_native<Address>(state->client+cosmetics::bindings::entity_chunks);
    const auto chunk=chunks?read_native<Address>(chunks):0;
    if(!chunk)return 0;
    const auto identity=chunk+index*sizeof(EntityIdentity);
    const auto handle=read_native<std::uint32_t>(identity+offsetof(EntityIdentity,handle));
    const auto controller=(handle&0x7fff)==static_cast<unsigned>(index)?read_native<Address>(identity):0;
    return controller && read_native<Address>(controller)==state->client+iris::sdk::binding_values::layout_controller_vtable?controller:0;
}
Vector position(Address pawn,float height=1) {
    const auto& f=state->f;const auto scene=read_native<Address>(pawn+f.scene);
    if(!scene || read_native<bool>(scene+f.dormant))throw std::runtime_error("No spread: player pose unavailable");
    const auto origin=read_native<Vector>(scene+f.origin),view=read_native<Vector>(pawn+f.view);
    if(!finite(origin)||!finite(view))throw std::runtime_error("No spread: invalid player position");
    return origin+view*height;
}
struct HitboxPoint {int region{};Vector point{};bool secondary{};std::array<Vector,8> corners{};};
std::vector<HitboxPoint> hitbox_points(Address scene,bool multipoint=false,float multipoint_scale=.5f,bool include_limbs=false) {
    const auto model_state=scene+state->f.model;
    const auto handle=read_native<Address>(model_state+state->f.model_handle);
    const auto model=handle?read_native<Address>(handle):0;
    if(!model)return {};
    using Refresh=void(__fastcall*)(Address,unsigned);
    state->native_stage="hitbox pose";
    reinterpret_cast<Refresh>(state->client+iris::sdk::binding_values::layout_refresh_bones)(scene,0x100);
    const auto cache=read_native<Address>(model_state+iris::sdk::binding_values::layout_bone_cache);if(!cache)return {};
    const auto meshes=read_native<Address>(model+iris::sdk::binding_values::layout_model_meshes);const auto mesh_count=read_native<int>(model+iris::sdk::binding_values::layout_model_mesh_count);
    if(!meshes || mesh_count<=0 || mesh_count>64)return {};
    const auto mesh=read_native<Address>(meshes);if(!mesh)return {};
    const auto count_sets=read_native<int>(mesh+iris::sdk::binding_values::layout_mesh_set_count);
    if(count_sets<=0 || count_sets>32 || !(read_native<unsigned>(mesh+iris::sdk::binding_values::layout_mesh_set_flags)&0x7fffffff))return {};
    const auto set=read_native<Address>(mesh+iris::sdk::binding_values::layout_mesh_sets);if(!set)return {};
    const auto count=read_native<int>(set+iris::sdk::binding_values::layout_set_shape_count);const auto shapes=read_native<Address>(set+iris::sdk::binding_values::layout_set_shapes);
    if(count<=0 || count>32 || !shapes)return {};
    struct BoneReference {Address shape;int mesh{};int reserved{};};
    using Resolve=int(__fastcall*)(Address,const BoneReference*);
    std::vector<HitboxPoint> result,secondary;
    for(int i=0;i<count;++i) {
        const auto shape=shapes+iris::sdk::binding_values::layout_shape_stride*i;
        // Shape-owned transforms need a separate native path; skip them.
        if(read_native<bool>(shape+iris::sdk::binding_values::layout_shape_custom_transform))continue;
        const auto group=read_native<int>(shape+iris::sdk::binding_values::layout_shape_group);
        const auto region=group==1?0:group==2?1:group==3?(i==2?3:2):group==4||group==5?4:group==6||group==7?5:-1;
        if(region<0)continue;
        const BoneReference reference{shape};
        const auto index=reinterpret_cast<Resolve>(state->client+iris::sdk::binding_values::layout_resolve_bone)(model,&reference);
        if(index<0 || index>=256)continue;
        const auto transform=memory::read<BoneTransform>(cache+32*index);if(!transform)continue;
        const auto mins=read_native<Vector>(shape+iris::sdk::binding_values::layout_shape_minimum),maxs=read_native<Vector>(shape+iris::sdk::binding_values::layout_shape_maximum);
        if(!finite(mins)||!finite(maxs)||length(mins)>1000||length(maxs)>1000)continue;
        const auto point=transform_point(*transform,(mins+maxs)*.5f);
        HitboxPoint geometry{region,point.value_or(Vector{})};
        bool valid=bool(point);
        if(multipoint){
            const auto bounds=visuals::local_bounds(mins,maxs,read_native<std::uint8_t>(shape+binding_values::layout_shape_type),read_native<float>(shape+binding_values::layout_shape_radius));
            valid=valid && bool(bounds);
            if(bounds){for(int j=0;j<8;++j){const auto lo=(*bounds)[0],hi=(*bounds)[1];const auto corner=transform_point(*transform,{j&1?hi.x:lo.x,j&2?hi.y:lo.y,j&4?hi.z:lo.z});if(!corner){valid=false;break;}geometry.corners[j]=*corner;}}
        }
        if(valid)result.push_back(geometry);
        if(multipoint && (region<4||include_limbs)){const float spread=.3f*std::clamp(multipoint_scale,.1f,1.f);for(const float fraction:{.5f-spread,.5f+spread}){
            const auto alternate=transform_point(*transform,mins+(maxs-mins)*fraction);
            if(alternate && point && length(*alternate-*point)>.25f){auto extra=geometry;extra.point=*alternate;extra.secondary=true;if(valid)secondary.push_back(extra);}
        }}
    }
    result.insert(result.end(),secondary.begin(),secondary.end());
    return result;
}
struct TraceResult {std::array<std::byte,8> surface{};Address entity{},hitbox{};std::array<std::byte,0xf8> rest{};};
bool trace_ray(Address pawn,Vector eye,Vector point,TraceResult& result) {
    ++state->rays;
    alignas(16) std::array<std::byte,0x48> filter{};
    alignas(16) std::array<std::byte,0x30> ray{};
    using Filter=void(__fastcall*)(void*,Address,std::uint64_t,std::uint8_t,int);
    using Trace=bool(__fastcall*)(Address,void*,const Vector*,const Vector*,const void*,TraceResult*);
    state->native_stage="visibility trace";
    reinterpret_cast<Filter>(state->client+filter_rva)(filter.data(),pawn,0x1c300b,3,7);
    const auto manager=read_native<Address>(state->client+state->trace_manager);
    return manager && reinterpret_cast<Trace>(state->client+trace_rva)(manager,ray.data(),&eye,&point,filter.data(),&result);
}
bool visible(Address pawn,Address target,Vector eye,Vector point) {
    TraceResult result;return trace_ray(pawn,eye,point,result)&&result.entity==target;
}
int contact_region(Address target,Address hitbox,int fallback) {
    const auto& f=state->f;const auto scene=read_native<Address>(target+f.scene);if(!scene||!hitbox)return fallback;
    const auto handle=read_native<Address>(scene+f.model+f.model_handle),model=handle?read_native<Address>(handle):0;
    if(!model)return fallback;
    const auto meshes=read_native<Address>(model+binding_values::layout_model_meshes);const auto count=read_native<int>(model+binding_values::layout_model_mesh_count);
    if(!meshes||count<=0||count>64)return fallback;
    const auto mesh=read_native<Address>(meshes);if(!mesh)return fallback;
    const auto sets=read_native<int>(mesh+binding_values::layout_mesh_set_count);if(sets<=0||sets>32)return fallback;
    const auto set=read_native<Address>(mesh+binding_values::layout_mesh_sets);if(!set)return fallback;
    const auto shapes=read_native<Address>(set+binding_values::layout_set_shapes);const auto size=read_native<int>(set+binding_values::layout_set_shape_count);
    if(!shapes||size<=0||size>32)return fallback;
    for(int i=0;i<size;++i)if(shapes+binding_values::layout_shape_stride*i==hitbox) {
        const auto group=read_native<int>(hitbox+binding_values::layout_shape_group);
        return group==1?0:group==2?1:group==3?(i==2?3:2):group==4||group==5?4:group==6||group==7?5:fallback;
    }
    return fallback;
}
Vector camera_angles(Address input) {
    using GetView=Address(__fastcall*)(Address,int);
    state->native_stage="camera getter";
    return read_native<Vector>(reinterpret_cast<GetView>(state->client+binding_values::combat_get_view_rva)(input,0));
}
void publish_camera(Address input,const Vector& angles) {
    using SetView=void(__fastcall*)(Address,int,const Vector*);
    state->native_stage="camera setter";
    reinterpret_cast<SetView>(state->client+binding_values::combat_set_view_rva)(input,0,&angles);
}
float damage_at(Address weapon,Address target,Vector eye,Vector point,int region) {
    const auto& f=state->f;const auto data=read_native<Address>(weapon+iris::sdk::binding_values::layout_weapon_vdata);
    if(!data)return 0;
    const auto range=read_native<float>(data+f.range);
    if(!std::isfinite(range)||range<=0||length(point-eye)>range)return 0;
    const auto items=read_native<Address>(target+f.items);
    return combat::direct_damage(static_cast<float>(read_native<int>(data+f.damage)),read_native<float>(data+f.head_multiplier),read_native<float>(data+f.range_modifier),length(point-eye),region,read_native<int>(target+f.armor),items && read_native<bool>(items+f.helmet),read_native<float>(data+f.armor_ratio));
}
// The optional contact outputs let Trigger use the same reviewed bullet path
// once for the actual crosshair ray, rather than simulate every enemy center.
float shot_damage(Address pawn,Address weapon,Address target,Vector eye,Vector point,int region,bool walls,int* contacted_region=nullptr,Address* contacted_entity=nullptr) {
    if(contacted_region)*contacted_region=-1;if(contacted_entity)*contacted_entity=0;
    const auto eligible=[&](Address entity){
        if(target)return entity==target;
        if(!entity||entity==pawn)return false;
        for(int i=1;i<=64;++i){const auto controller=controller_at(i);if(!controller||read_native<bool>(controller+state->f.hltv))continue;
            if(sdk::entity(state->client,read_native<std::uint32_t>(controller+state->f.player_pawn))!=entity)continue;
            return read_native<int>(entity+state->f.health)>0&&!read_native<bool>(entity+state->f.immune)&&read_native<std::uint8_t>(entity+state->f.team)!=read_native<std::uint8_t>(pawn+state->f.team);
        }return false;
    };
    TraceResult direct;
    const bool unobstructed=trace_ray(pawn,eye,point,direct)&&eligible(direct.entity);
    if(!walls || unobstructed) {
        if(!unobstructed)return 0;
        const auto& result=direct;
        Vector end{};std::memcpy(&end,reinterpret_cast<const std::byte*>(&result)+0x84,sizeof(end));
        const auto hit_region=contact_region(result.entity,result.hitbox,region);
        if(!finite(end)||hit_region<0||hit_region>=6)return 0;
        if(contacted_region)*contacted_region=hit_region;if(contacted_entity)*contacted_entity=result.entity;
        return damage_at(weapon,result.entity,eye,end,hit_region);
    }
    const auto& f=state->f;const auto data=read_native<Address>(weapon+binding_values::layout_weapon_vdata);
    if(!data || !finite(point-eye))return 0;
    const auto range=read_native<float>(data+f.range),distance=length(point-eye);
    if(!std::isfinite(range)||range<=0||distance<=0||distance>range)return 0;
    const auto penetration=read_native<float>(data+f.penetration),modifier=read_native<float>(data+f.range_modifier);
    const auto damage=static_cast<float>(read_native<int>(data+f.damage));
    if(!std::isfinite(penetration)||penetration<0||!std::isfinite(modifier)||modifier<=0||damage<=0)return 0;
    auto& storage=state->bullet_storage;
    const auto trace=reinterpret_cast<Address>(storage.bytes.data());
    // Native vectors may replace their inline storage. Keep their allocations across traces.
    storage.reset();++state->penetration_traces;
    alignas(16) std::array<std::byte,0x48> filter{};
    using Filter=void(__fastcall*)(void*,Address,std::uint64_t,std::uint8_t,int);
    using Init=void(__fastcall*)(Address,const Vector*,const Vector*,void*,int,bool);
    using Simulate=void(__fastcall*)(Address,float,float,float,int,int,Address);
    reinterpret_cast<Filter>(state->client+filter_rva)(filter.data(),pawn,0x1c300b,3,15);
    const auto delta=(point-eye)*(range/distance);
    state->native_stage="bullet initialization";
    reinterpret_cast<Init>(state->client+binding_values::combat_bullet_init_rva)(trace,&eye,&delta,filter.data(),4,true);
    state->native_stage="bullet simulation";
    reinterpret_cast<Simulate>(state->client+binding_values::combat_bullet_simulate_rva)(trace,damage,penetration,modifier,4,read_native<std::uint8_t>(pawn+f.team),0);
    state->native_stage="bullet contacts";
    const auto count=read_native<int>(trace+0x1c20),contacts=read_native<int>(trace);
    const auto hits=read_native<Address>(trace+0x1c28),surfaces=read_native<Address>(trace+8);
    if(count<0||count>4096||contacts<0||contacts>4096||!hits||!surfaces||count>read_native<int>(trace+0x1c30)||contacts>read_native<int>(trace+0x10))return 0;
    if(!count)++state->empty_traces;
    for(int i=0;i<count;++i) {
        const auto hit=hits+0x18*i;const auto remaining=read_native<float>(hit+8);
        if(!std::isfinite(remaining)||remaining<=0){++state->wall_stops;break;}
        if(read_native<std::uint8_t>(hit+0x14)&1){if(read_native<float>(hit+4)>=1){++state->wall_stops;break;}continue;}
        const auto contact=read_native<std::uint16_t>(hit+0x10)&0x7fff;
        if(contact>=contacts)return 0;
        const auto entity=sdk::entity(state->client,read_native<std::uint32_t>(surfaces+0x38*contact+0x2c));
        if(!entity)continue;
        if(!eligible(entity)){++state->other_contacts;continue;}
        ++state->wall_hits;
        const auto hit_region=contact_region(entity,read_native<Address>(surfaces+0x38*contact+0x10),region);
        if(hit_region<0||hit_region>=6)return 0;
        if(contacted_region)*contacted_region=hit_region;if(contacted_entity)*contacted_entity=entity;
        const auto items=read_native<Address>(entity+f.items);
        return combat::direct_damage(remaining,read_native<float>(data+f.head_multiplier),1,0,hit_region,read_native<int>(entity+f.armor),items&&read_native<bool>(items+f.helmet),read_native<float>(data+f.armor_ratio));
    }
    return 0;
}
bool firearm(Address weapon) {
    if(!weapon)return false;
    const auto& f=state->f;const auto data=read_native<Address>(weapon+iris::sdk::binding_values::layout_weapon_vdata);
    constexpr std::array definitions{1,2,3,4,7,8,9,10,11,13,14,16,17,19,23,24,26,28,30,32,33,34,36,38,39,40,60,61,63};
    const auto definition=read_native<std::uint16_t>(weapon+f.definition);
    return data && read_native<int>(data+f.bullets)==1 && std::find(definitions.begin(),definitions.end(),definition)!=definitions.end();
}
Vector aim_punch(Address pawn) {
    const auto services=read_native<Address>(pawn+state->f.punch);Vector punch{};
    if(!services)throw std::runtime_error("Aim punch services unavailable");
    using Punch=void(__fastcall*)(Address,Vector*,unsigned);
    reinterpret_cast<Punch>(state->client+punch_rva)(services,&punch,0);
    if(!finite(punch)||std::abs(punch.x)>90||std::abs(punch.y)>180)throw std::runtime_error("Invalid aim punch");
    return punch;
}
Address current_command(Address controller) {
    using Ring=Address(__fastcall*)(Address);using Get=Address(__fastcall*)(Address,int);
    const auto ring=reinterpret_cast<Ring>(state->client+reviewed::command_ring)(controller);
    const auto sequence=ring?read_native<int>(ring+reviewed::ring_sequence):0;
    return sequence>0?reinterpret_cast<Get>(state->client+reviewed::command)(controller,sequence):0;
}
struct Saved {Address address{};std::vector<std::byte> bytes;};
struct NativeString {std::array<std::byte,16> data{};std::uint64_t size{},capacity{15};};
bool assign_crc(Address field,const NativeString* input,Address arena) noexcept {
    using Assign=void(__fastcall*)(Address,const NativeString*,Address);
    __try {reinterpret_cast<Assign>(state->client+binding_values::commands_copy_crc)(field,input,arena);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
NativeString string_view(std::span<const std::byte> bytes) {
    NativeString input;input.size=bytes.size();
    if(bytes.size()<16)std::memcpy(input.data.data(),bytes.data(),bytes.size());
    else {const auto pointer=bytes.data();std::memcpy(input.data.data(),&pointer,sizeof(pointer));input.capacity=bytes.size();}
    return input;
}
struct Transaction {
    std::vector<Saved> saved;bool committed{};
    Address crc_field{},crc_arena{},original_crc{};std::vector<std::byte> crc_bytes;
    ~Transaction(){rollback();}
    void rollback() noexcept {
        if(committed)return;
        bool okay=true;
        for(auto i=saved.rbegin();i!=saved.rend();++i)okay=memory::write_local(i->address,std::span<const std::byte>(i->bytes))&&okay;
        if(crc_field){
            if(original_crc){const auto input=string_view(crc_bytes);okay=assign_crc(crc_field,&input,crc_arena)&&okay;}
            else okay=memory::write(crc_field,original_crc)&&okay;
        }
        saved.clear();crc_field=0;crc_bytes.clear();
        if(!okay){enabled=false;stopped=true;status="Command rollback failed; restart the game";overlay_log("Input restoration FAILED: command writes disabled; restart the host. Preserve this log and matching PDB.");}
    }
    template<class T> void put(Address address,const T& value) {
        Saved previous{address,std::vector<std::byte>(sizeof(T))};
        if(!memory::read_local(address,previous.bytes))throw std::runtime_error("No spread: command read failed");
        if(std::memcmp(previous.bytes.data(),&value,sizeof(T))==0)return;
        saved.push_back(std::move(previous));
        if(!write_native(address,value))throw std::runtime_error("No spread: command write failed");
    }
    template<class T> void remember(Address address) {
        Saved previous{address,std::vector<std::byte>(sizeof(T))};
        if(!memory::read_local(address,previous.bytes))throw std::runtime_error("Native pose snapshot failed");
        saved.push_back(std::move(previous));
    }
};
void checksum(Address address,const Base& base,const Buttons& buttons,const Angles& angles,Transaction& tx) {
    std::array<std::byte,64> bytes{};std::size_t size{};
    const auto byte=[&](unsigned v){bytes[size++]=static_cast<std::byte>(v);};
    const auto fixed=[&](const auto& v){std::memcpy(bytes.data()+size,&v,sizeof(v));size+=sizeof(v);};
    const std::array values{buttons.value,buttons.changed,buttons.scroll};
    const unsigned button_size=9u*static_cast<unsigned>((values[0]!=0)+(values[1]!=0)+(values[2]!=0));
    if(button_size){byte(0x1a);byte(button_size);for(unsigned i=0;i<3;++i)if(values[i]){byte(9+8*i);fixed(values[i]);}}
    const std::array directions{angles.x,angles.y,angles.z};
    const unsigned angle_size=5u*static_cast<unsigned>((directions[0]!=0)+(directions[1]!=0)+(directions[2]!=0));
    if(angle_size){byte(0x22);byte(angle_size);for(unsigned i=0;i<3;++i)if(directions[i]!=0){byte(13+8*i);fixed(directions[i]);}}
    auto arena=base.header.arena&~Address{3};if(base.header.arena&1)arena=read_native<Address>(arena);
    if(tx.crc_field&&tx.crc_field!=address+offsetof(Base,crc))throw std::runtime_error("Checksum transaction spans multiple commands");
    if(!tx.crc_field){tx.original_crc=base.crc;tx.crc_arena=arena;
    if(base.crc){
        const auto old=base.crc&~Address{3},old_size=read_native<std::uint64_t>(old+16),old_capacity=read_native<std::uint64_t>(old+24);
        if(old_size>64 || old_capacity<old_size || old_capacity>1048576)throw std::runtime_error("No spread: invalid original command checksum");
        tx.crc_bytes.resize(static_cast<std::size_t>(old_size));
        if(!memory::read(old_capacity<16?old:read_native<Address>(old),tx.crc_bytes))throw std::runtime_error("No spread: original checksum unavailable");
    }
    tx.crc_field=address+offsetof(Base,crc);
    }
    const auto input=string_view(std::span<const std::byte>(bytes.data(),size));
    if(!assign_crc(tx.crc_field,&input,arena))throw std::runtime_error("No spread: native checksum assignment failed");
    const auto string=read_native<Address>(address+offsetof(Base,crc))&~Address{3};
    const auto length=read_native<std::uint64_t>(string+16),capacity=read_native<std::uint64_t>(string+24);
    const auto data=capacity<16?string:read_native<Address>(string);
    std::array<std::byte,64> actual{};
    if(length!=size || capacity<length || capacity>1048576 || !memory::read(data,std::span(actual.data(),size)) || std::memcmp(actual.data(),bytes.data(),size))throw std::runtime_error("No spread: command checksum mismatch");
    tx.put(address+offsetof(Base,has),read_native<std::uint32_t>(address+offsetof(Base,has))|7u);
}
#include "core/features/movement/native_pipeline.inl"
#include "core/features/combat/rage.inl"
#include "core/features/combat/legit.inl"
void suspend(const char* error) {
    visuals::wall_target.store(nullptr);state->plan.reset();state->shot_request.reset();state->processed=0;reset_view();
    status="Command rejected; check diagnostics";++state->rejected_commands;
    const auto now=GetTickCount64();
    if(now-state->rejection_logged>=1000||!state->rejection_logged){
        state->rejection_logged=now;char message[512]{};
        std::snprintf(message,sizeof(message),"Input rejection: stage=%s raw=%lld tick=%d seed=%d count=%llu reason=%s; next command may retry",state->native_stage,state->observed_command,state->processed_tick,state->observed_seed,state->rejected_commands,error);overlay_log(message);
    }
}
void sample_hud() {
    if(!hud_enabled){hud_snapshot.store(nullptr);return;}
    const auto& f=state->f;const auto controller=read_native<Address>(state->client+cosmetics::bindings::local_controller);
    const auto pawn=controller?sdk::entity(state->client,read_native<std::uint32_t>(controller+f.pawn)):0;
    player_alive=pawn&&read_native<int>(pawn+f.health)>0;
    if(!player_alive){hud_snapshot.store(nullptr);return;}
    const auto camera=read_native<Address>(pawn+f.camera);const auto value=camera?read_native<unsigned>(camera+f.camera_fov):90u;
    const auto services=read_native<Address>(pawn+f.services);const auto weapon_handle=services?read_native<std::uint32_t>(services+f.active):0xffffffffu;const auto weapon=services?sdk::entity(state->client,weapon_handle):0;
    const auto handle=read_native<std::uint32_t>(controller+f.pawn);const bool scoped=read_native<bool>(pawn+f.scoped);const auto now=GetTickCount64();
    if(state->hud_pawn==handle&&state->hud_weapon==weapon_handle&&state->hud_scoped==scoped&&now-state->hud_sampled<33)return;
    state->hud_pawn=handle;state->hud_weapon=weapon_handle;state->hud_scoped=scoped;state->hud_sampled=now;
    float accuracy{};
    if(firearm(weapon)){
        using Inaccuracy=float(__fastcall*)(Address,float*,float*);
        const auto result=reinterpret_cast<Inaccuracy>(state->client+inaccuracy_rva)(weapon,nullptr,nullptr);
        if(std::isfinite(result)&&result>=0)accuracy=std::min(result,1.f);
    }
    hud_snapshot.store(std::make_shared<const HudSnapshot>(HudSnapshot{read_native<bool>(pawn+f.scoped),accuracy,cosmetics::camera_fov(value>1&&value<179?static_cast<float>(value):90.f,camera),GetTickCount64()}));
}
void safe_process(Address command,bool view) {
    const auto phase=[&](int index,const char* name,const auto& action){
        state->native_stage=name;const auto before=std::chrono::steady_clock::now();
        try{action();}catch(const std::exception& e){suspend(e.what());}
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();
        state->process_stages[index]+=elapsed;state->process_stage_peaks[index]=std::max(state->process_stage_peaks[index],elapsed);
    };
    if(view&&!stopped){
        phase(1,"HUD sampling",[]{sample_hud();});
        phase(2,"movement planning",[&]{movement_command(command);const auto motion=movement_settings.load();if(motion&&motion->active()&&!round_frozen)plan_jumps(command,*motion,(GetAsyncKeyState(VK_SPACE)&0x8000)!=0);});
        const auto config=aim_settings.load();
        if(config&&config->active())phase(3,"aim planning",[&]{process_view(command,true);});
    }
    ++state->frames;
}
int report_native_fault(EXCEPTION_POINTERS* exception) noexcept {
    char message[256]{};
    std::snprintf(message,sizeof(message),"Combat native fault: stage=%s code=0x%08lX instruction=%p; combat suspended",state->native_stage,exception->ExceptionRecord->ExceptionCode,exception->ExceptionRecord->ExceptionAddress);
    overlay_log(message);enabled=false;stopped=true;status="Native call fault; restart the game";
    return EXCEPTION_EXECUTE_HANDLER;
}
void guarded_process(Address command,bool view=false) {
    __try {state->native_stage="command preparation";safe_process(command,view);}__except(report_native_fault(GetExceptionInformation())){}
}
WireSnapshot capture_wire(Address proto,const Transaction& tx) {
    const auto base_address=read_native<Address>(proto+binding_values::layout_proto_base);const auto base=read_native<Base>(base_address);
    const auto history=read_native<Repeated>(proto+binding_values::layout_proto_history);
    WireSnapshot wire;wire.seed=base.random_seed;wire.tick=base.tick;wire.move_count=base.moves.size;
    for(const auto& saved:tx.saved) {
        bool found=false;
        const auto map=[&](WireObject kind,int index,Address start,std::size_t size,std::size_t skip=0) {
            if(!start || saved.address<start || saved.address+saved.bytes.size()>start+size)return;
            const auto begin=std::max(saved.address,start+skip),end=saved.address+saved.bytes.size();
            if(begin<end){WireEdit edit{kind,index,static_cast<std::size_t>(begin-start),std::vector<std::byte>(end-begin)};
                if(!memory::read(begin,edit.bytes))throw std::runtime_error("Cannot retain encoded command baseline");wire.edits.push_back(std::move(edit));}
            found=true;
        };
        map(WireObject::proto,0,proto,0x40,16);map(WireObject::base,0,base_address,sizeof(Base),16);
        map(WireObject::angles,0,base.angles,sizeof(Angles),16);map(WireObject::buttons,0,base.buttons,sizeof(Buttons),16);
        for(int i=0;i<history.size;++i){const auto at=read_native<Address>(history.rep+8+8*i);map(WireObject::history,i,at,sizeof(History),16);map(WireObject::history_angles,i,read_native<History>(at).messages[0],sizeof(Angles),16);}
        for(int i=0;i<base.moves.size;++i)map(WireObject::move,i,read_native<Address>(base.moves.rep+8+8*i),sizeof(Step),16);
        if(!found)throw std::runtime_error("Unmapped encoded command edit");
    }
    for(int i=0;i<base.moves.size;++i){const auto at=read_native<Address>(base.moves.rep+8+8*i);
        WireEdit edit{WireObject::move,i,sizeof(Header),std::vector<std::byte>(sizeof(Step)-sizeof(Header))};
        if(!memory::read_local(at+sizeof(Header),edit.bytes))throw std::runtime_error("Cannot retain native step payload");wire.edits.push_back(std::move(edit));
    }
    if(tx.crc_field){const auto string=read_native<NativeString>(read_native<Address>(tx.crc_field)&~Address{3});
        if(string.size>64)throw std::runtime_error("Invalid encoded checksum size");wire.crc.resize(static_cast<std::size_t>(string.size));
        const auto data=string.capacity<16?(read_native<Address>(tx.crc_field)&~Address{3}):read_native<Address>(read_native<Address>(tx.crc_field)&~Address{3});
        if(!memory::read(data,wire.crc))throw std::runtime_error("Cannot retain encoded checksum");}
    return wire;
}
bool restore_wire(Address proto,const WireSnapshot& wire,Transaction& tx) {
    const auto base_address=read_native<Address>(proto+binding_values::layout_proto_base);const auto base=read_native<Base>(base_address);
    if(base.random_seed!=wire.seed || base.tick!=wire.tick)return false;
    const auto history=read_native<Repeated>(proto+binding_values::layout_proto_history);
    if(wire.move_count<0||wire.move_count>maximum_steps||!movement_timeline_supported(base))return false;
    if(history.size<0||history.size>maximum_steps||history.capacity<history.size||history.capacity>4096||
       (history.size&&(!history.rep||read_native<int>(history.rep)<history.size)))return false;
    for(const auto& edit:wire.edits){
        if(edit.offset<sizeof(Header))return false;
        if((edit.object==WireObject::history||edit.object==WireObject::history_angles)&&
           (edit.index<0||edit.index>=history.size||!history.rep))return false;
        if(edit.object==WireObject::history_angles&&!read_native<History>(read_native<Address>(history.rep+8+8*edit.index)).messages[0])return false;
        if(edit.object==WireObject::move&&(edit.index<0||edit.index>=wire.move_count))return false;
    }
    // Clones retain the local step count, not the temporary transmitted count.
    while(read_native<Base>(base_address).moves.size<wire.move_count)append_step(base_address,Step{},&tx);
    tx.put(base_address+offsetof(Base,moves)+offsetof(Repeated,size),wire.move_count);
    const auto replay_moves=read_native<Base>(base_address).moves;
    for(const auto& edit:wire.edits){Address address{};
        switch(edit.object){
        case WireObject::proto:address=proto;break;
        case WireObject::base:address=base_address;break;
        case WireObject::angles:address=base.angles;break;
        case WireObject::buttons:address=base.buttons;break;
        case WireObject::history:case WireObject::history_angles:
            if(edit.index>=history.size || !history.rep)return false;
            address=read_native<Address>(history.rep+8+8*edit.index);if(edit.object==WireObject::history_angles)address=read_native<History>(address).messages[0];break;
        case WireObject::move:if(edit.index>=replay_moves.size || !replay_moves.rep)return false;address=read_native<Address>(replay_moves.rep+8+8*edit.index);break;
        }
        if(!address)return false;
        Saved original{address+edit.offset,std::vector<std::byte>(edit.bytes.size())};
        if(!memory::read(original.address,original.bytes))throw std::runtime_error("Cannot read command baseline");
        tx.saved.push_back(std::move(original));if(!memory::write(address+edit.offset,std::span<const std::byte>(edit.bytes)))throw std::runtime_error("Cannot restore command baseline");
    }
    if(!wire.crc.empty()){
        if(!base.crc)return false;tx.original_crc=base.crc;const auto string=read_native<NativeString>(base.crc&~Address{3});
        if(string.size>64)throw std::runtime_error("Invalid original checksum size");tx.crc_bytes.resize(static_cast<std::size_t>(string.size));
        const auto data=string.capacity<16?(base.crc&~Address{3}):read_native<Address>(base.crc&~Address{3});
        if(!memory::read(data,tx.crc_bytes))throw std::runtime_error("Cannot preserve command checksum");
        tx.crc_field=base_address+offsetof(Base,crc);tx.crc_arena=base.header.arena&~Address{3};
        const auto input=string_view(wire.crc);if(!assign_crc(tx.crc_field,&input,tx.crc_arena))throw std::runtime_error("Cannot restore encoded checksum");
    }
    return true;
}
bool safe_apply(Address proto,Transaction* transient) {
    try {
        state->encoding_shot=state->encoding_rejected=false;
        const auto base=read_native<Base>(read_native<Address>(proto+binding_values::layout_proto_base));
        bool applied=false;
        if(state->view_plan&&state->view_plan->seed==base.random_seed&&state->view_plan->tick==base.tick)applied=apply_view(proto,*state->view_plan,transient);
        return apply_trigger(proto,*transient)||applied;
    }catch(const std::exception& e){state->encoding_rejected=true;suspend(e.what());return false;}
}
bool guarded_apply(Address proto,Transaction* transient) {
    __try {state->native_stage="command encoding";return safe_apply(proto,transient);}__except(report_native_fault(GetExceptionInformation())){return false;}
}
Address guarded_current_command(Address controller) noexcept {
    __try {return current_command(controller);}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
void __fastcall create_move(void* input,int slot,bool active) {
    state->original_move(input,slot,active);
    if(slot)return;
    std::lock_guard lock(state->mutex);
    if(!active || !context()){hud_snapshot.store(nullptr);player_alive=false;reset_view();return;}
    try {const auto controller=read_native<Address>(state->client+cosmetics::bindings::local_controller);if(controller){const auto command=guarded_current_command(controller);if(command){guarded_process(command,true);
        if(!stopped && state->view_plan && !state->view_plan->camera_applied) {
            auto& plan=*state->view_plan;
            if(!apply_view(command+offsetof(Command,proto),plan)){state->view_plan.reset();state->previous_camera_valid=false;}
            else if(publish_view_camera(reinterpret_cast<Address>(input),plan))plan.camera_applied=true;
            else state->view_plan.reset();
        }
    }}}
    catch(const std::exception&){reset_view();}
}
void __fastcall prepare(Address command) {
    state->original_prepare(command);
    std::lock_guard lock(state->mutex);
    if(context()){try{capture_direction_input(command);}catch(const std::exception&){}}
}
bool align_impulse(Address address,const Base& base,movement::Axes actual,Transaction& tx,std::optional<movement::Axes> wanted={}) {
    auto initial=movement::Axes{base.forward,base.left};std::vector<Step> steps;int first=-1;
    for(int i=0;i<base.moves.size;++i){const auto step=read_native<Step>(read_native<Address>(base.moves.rep+8+8*i));
        if((step.has&1)&&(step.button&movement_directions))return false;
        if(!(step.has&1)||!step.button){const float when=step.has&4?step.when:0;
            if(when>0){if(step.has&8)initial[0]-=step.forward;if(step.has&0x10)initial[1]-=step.left;}
            else if(first<0)first=i;
        }
        steps.push_back(step);
    }
    if(wanted)initial=*wanted;
    const movement::Axes delta{initial[0]-actual[0],initial[1]-actual[1]};
    if(first<0){if(std::abs(delta[0])<1e-6f&&std::abs(delta[1])<1e-6f)return true;
        Step step;step.has=0x1c;step.forward=delta[0];step.left=delta[1];steps.insert(steps.begin(),step);first=0;
    }
    for(std::size_t i=0;i<steps.size();++i){auto& step=steps[i];if((step.has&1)&&step.button)continue;
        if((step.has&4)&&step.when>0)continue;
        step.has=(step.has&~3u)|0x1cu;step.button=0;step.pressed=false;step.forward=static_cast<int>(i)==first?delta[0]:0;step.left=static_cast<int>(i)==first?delta[1]:0;
    }
    if(steps.size()>maximum_steps)return false;
    while(read_native<Base>(address).moves.size<static_cast<int>(steps.size()))append_step(address,Step{},&tx);
    const auto moves=read_native<Base>(address).moves;
    for(std::size_t i=0;i<steps.size();++i){const auto at=read_native<Address>(moves.rep+8+8*i);steps[i].header=read_native<Step>(at).header;steps[i].cached=0;tx.put(at,steps[i]);}
    return true;
}
bool reconcile_steering(Address services,Address command) {
    const auto config=movement_settings.load();if(stopped||round_frozen||!config||!config->active())return false;
    const auto cmd=read_native<Command>(command);if(cmd.vtable!=state->client+reviewed::command_vtable||!cmd.base)return false;
    const auto base=read_native<Base>(cmd.base);if(!base.buttons||!base.angles||!movement_timeline_supported(base))return false;
    MovementEdit* owner{};
    for(auto i=state->movement_edits.rbegin();i!=state->movement_edits.rend();++i)if(i->number==cmd.number&&i->tick==base.tick&&i->seed==base.random_seed&&i->config==*config&&i->terminal==movement::Axes{base.forward,base.left}){owner=&*i;break;}
    if(!owner)return false;
    const auto controller=read_native<Address>(state->client+cosmetics::bindings::local_controller);
    if(!controller||read_native<std::uint32_t>(controller+state->f.pawn)!=owner->pawn)return false;
    const auto pawn=sdk::entity(state->client,owner->pawn);
    if(!pawn||read_native<int>(pawn+state->f.health)<=0||read_native<Address>(pawn+state->f.movement)!=services)return false;
    movement::Axes actual{read_native<float>(services+state->f.command_forward),read_native<float>(services+state->f.command_left)};
    if(!std::isfinite(actual[0])||!std::isfinite(actual[1]))return false;
    if(state->f.movement_button_value){const auto held=read_native<std::uint64_t>(services+state->f.movement_button_value);if(!(held&0x18))actual[0]=0;if(!(held&0x600))actual[1]=0;}
    actual=movement::limit(actual);
    const auto before=state->jump_plan&&state->jump_plan->number==cmd.number&&state->jump_plan->tick==base.tick&&state->jump_plan->seed==base.random_seed?jump_signature(base):0;
    Transaction tx;if(!align_impulse(cmd.base,base,actual,tx))return false;
    if(!tx.saved.empty())checksum(cmd.base,read_native<Base>(cmd.base),read_native<Buttons>(base.buttons),read_native<Angles>(base.angles),tx);
    tx.committed=true;owner->baseline=actual;state->observed_impulse=actual;++state->native_steering;
    if(before&&state->jump_plan&&state->jump_plan->written==before)state->jump_plan->written=jump_signature(read_native<Base>(cmd.base));
    return true;
}
void safe_reconcile_steering(Address services,Address command) noexcept {
    try{reconcile_steering(services,command);}catch(const std::exception&){}
}
void guarded_reconcile_steering(Address services,Address command) noexcept {
    __try{safe_reconcile_steering(services,command);}__except(report_native_fault(GetExceptionInformation())){}
}
void __fastcall run_prediction(Address services,Address command) {
    std::unique_lock lock(state->mutex,std::try_to_lock);
    const bool previous=state->prediction_active;
    if(lock.owns_lock()&&context()){
        const auto before=std::chrono::steady_clock::now();guarded_prepare_movement(services,command);
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();
        state->prediction_plan_ms+=elapsed;state->prediction_plan_peak=std::max(state->prediction_plan_peak,elapsed);
    }
    if(lock.owns_lock())state->prediction_active=true;
    if(lock.owns_lock()&&context())guarded_reconcile_steering(services,command);
    if(lock.owns_lock())state->prediction_active=previous;
    if(lock.owns_lock())++state->prediction_calls;
    const auto native_started=std::chrono::steady_clock::now();
    state->original_prediction(services,command);
    if(lock.owns_lock()){
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-native_started).count();
        state->prediction_native_ms+=elapsed;state->prediction_native_peak=std::max(state->prediction_native_peak,elapsed);
    }
    if(lock.owns_lock())state->prediction_active=previous;
}
std::optional<movement::Axes> owned_steering(Address proto) {
    const auto config=movement_settings.load();if(stopped||round_frozen||!config||!config->active())return {};
    const auto at=read_native<Address>(proto+binding_values::layout_proto_base);if(!at)return {};const auto base=read_native<Base>(at);
    for(const auto& edit:state->movement_edits)if(edit.tick==base.tick&&edit.seed==base.random_seed&&edit.config==*config&&edit.terminal==movement::Axes{base.forward,base.left}){
        const auto controller=read_native<Address>(state->client+cosmetics::bindings::local_controller);if(!controller||read_native<std::uint32_t>(controller+state->f.pawn)!=edit.pawn)return {};
        const auto pawn=sdk::entity(state->client,edit.pawn);if(pawn&&read_native<int>(pawn+state->f.health)>0)return edit.baseline;
    }
    return {};
}
bool align_encoded_steering(Address from,Address to,Transaction& tx,movement::Axes source) {
    if(stopped||round_frozen||!from||from==to)return false;
    const auto previous_at=read_native<Address>(from+binding_values::layout_proto_base),current_at=read_native<Address>(to+binding_values::layout_proto_base);
    if(!previous_at||!current_at)return false;
    const auto previous=read_native<Base>(previous_at),current=read_native<Base>(current_at);
    if(!previous.buttons||!current.buttons||!current.angles||!movement_timeline_supported(current))return false;
    const auto keys=read_native<Buttons>(previous.buttons).value;
    const movement::Axes actual{(previous.has&0x40)&&(keys&0x18)?previous.forward:0.f,(previous.has&0x80)&&(keys&0x600)?previous.left:0.f};
    if(!std::isfinite(actual[0])||!std::isfinite(actual[1]))return false;
    for(int i=0;i<current.moves.size;++i){const auto step=read_native<Step>(read_native<Address>(current.moves.rep+8+8*i));
        if(((step.has&1)&&step.button)||((step.has&4)&&step.when>0))continue;
        if(step.has&8)source[0]+=step.forward;if(step.has&0x10)source[1]+=step.left;
    }
    const auto count=tx.saved.size();if(!align_impulse(current_at,current,movement::limit(actual),tx,source))return false;
    if(tx.saved.size()!=count)checksum(current_at,read_native<Base>(current_at),read_native<Buttons>(current.buttons),read_native<Angles>(current.angles),tx);
    return true;
}
void log_input_changes() {
    const auto now=GetTickCount64();char message[768]{};
    const auto aim=aim_settings.load();const auto motion=movement_settings.load();
    if(aim&&(!state->logged_aim||*state->logged_aim!=*aim)){
        state->logged_aim=*aim;
        const auto definition=legit_weapon_definition.load();const auto selected=aim->resolve(definition);
        std::snprintf(message,sizeof(message),"Settings Legit: master=%d weapon=%u enabled=%d mode=%d activation=%d recoil=%d trigger=%d aim_delay=%.0f-%.0f trigger_delay=%.0f-%.0f chance=%.0f",aim->master_enabled,static_cast<unsigned>(definition),selected.enabled,selected.aim_mode,selected.activation,selected.recoil_control,selected.trigger,selected.reaction_time,selected.reaction_max,selected.trigger_reaction,selected.trigger_reaction_max,selected.trigger_hit_chance);overlay_log(message);
    }
    if(motion&&(!state->logged_movement||*state->logged_movement!=*motion)){
        state->logged_movement=*motion;const auto& o=motion->options;
        std::snprintf(message,sizeof(message),"Settings movement: bhop=%d humanize=%d chance=%.0f limit=%.0f assist=%d strength=%.2f",motion->bhop,o.humanize,o.chance,o.attempt_limit,motion->keys,motion->strength);overlay_log(message);
    }
    if(now-state->transition_logged>=250&&(state->last_movement_status!=movement_status.load())){
        state->transition_logged=now;state->last_movement_status=movement_status.load();
        std::snprintf(message,sizeof(message),"Input state: stopped=%d freeze=%d raw=%lld seed=%d movement=%s",stopped.load(),round_frozen.load(),state->observed_command,state->observed_seed,state->last_movement_status);overlay_log(message);
    }
}
bool __fastcall encode(Address from,Address to,Address buffer,int limit) {
    std::lock_guard lock(state->mutex);
    const auto started=std::chrono::steady_clock::now();
    bool applied{};bool result{},called{};
    auto stage_started=started;
    const auto stage=[&](std::size_t index){const auto now=std::chrono::steady_clock::now();const auto elapsed=std::chrono::duration<double,std::milli>(now-stage_started).count();state->encode_stages[index]+=elapsed;state->encode_stage_peaks[index]=std::max(state->encode_stage_peaks[index],elapsed);stage_started=now;};
    {
        Transaction baseline,transient;
        try {
            const auto from_base=from?read_native<Address>(from+binding_values::layout_proto_base):0;const auto base=from_base?read_native<Base>(from_base):Base{};
            for(auto i=state->wire.rbegin();from_base && from!=to && i!=state->wire.rend();++i)if(i->seed==base.random_seed && i->tick==base.tick){
                if(!restore_wire(from,*i,baseline)){
                    // A rebuilt native timeline can legitimately stop matching our cache.
                    // Restore partial replay before forwarding the untouched command.
                    baseline.rollback();state->wire.clear();
                    suspend("Encoded baseline no longer matches command layout; forwarding native command");
                    return state->original_encode(from,to,buffer,limit);
                }
                break;
            }
            stage(0);state->encoding_shot=state->encoding_rejected=false;
            if(context()){
                const auto steering=owned_steering(to);
                const auto current_base=steering?read_native<Base>(read_native<Address>(to+binding_values::layout_proto_base)):Base{};
                const float original_yaw=current_base.angles?read_native<Angles>(current_base.angles).y:0;
                applied=guarded_apply(to,&transient);
                if(state->encoding_rejected)throw std::runtime_error("Command edit rejected before native encoding");
                if(!applied)transient.rollback();
                if(steering){auto source=*steering;
                    if(applied&&current_base.angles)source=movement::rotate(source,std::remainder(read_native<Angles>(current_base.angles).y-original_yaw,360.f));
                    applied=align_encoded_steering(from,to,transient,source)||applied;
                }
            }
            if(!applied)transient.rollback();stage(1);
            std::optional<WireSnapshot> wire;if(applied)wire=capture_wire(to,transient);stage(2);
            called=true;result=state->original_encode(from,to,buffer,limit);
            if(result&&state->encoding_shot)++state->encoded_shots;
            if(!result)++state->native_encode_failures;
            stage(3);
            if(result && wire){std::erase_if(state->wire,[&](const auto& previous){return previous.seed==wire->seed && previous.tick==wire->tick;});state->wire.push_back(std::move(*wire));if(state->wire.size()>150)state->wire.pop_front();}stage(4);
        }catch(const std::exception& e){transient.rollback();baseline.rollback();suspend(e.what());if(!called)++state->native_encode_failures;}
        transient.rollback();baseline.rollback();stage(5);
    }
    ++state->encodes;
    log_input_changes();
    if(GetTickCount64()-state->performance_log>10000) {
        state->performance_log=GetTickCount64();char message[768]{};
        const auto frames=static_cast<double>(std::max<std::uint64_t>(state->frames,1));
        std::snprintf(message,sizeof(message),"Input stages mean/peak ms: HUD=%.3f/%.3f movement=%.3f/%.3f aim=%.3f/%.3f; rays=%llu native_encode_failed=%llu rejected_commands=%llu",state->process_stages[1]/frames,state->process_stage_peaks[1],state->process_stages[2]/frames,state->process_stage_peaks[2],state->process_stages[3]/frames,state->process_stage_peaks[3],state->rays,state->native_encode_failures,state->rejected_commands);overlay_log(message);
        state->process_stages.fill(0);state->process_stage_peaks.fill(0);state->native_encode_failures=0;
        const auto prediction_count=static_cast<double>(std::max<std::uint64_t>(state->prediction_calls,1));
        std::snprintf(message,sizeof(message),"Input prediction mean/peak ms: movement_plan=%.3f/%.3f native_RunCommand=%.3f/%.3f calls=%llu",state->prediction_plan_ms/prediction_count,state->prediction_plan_peak,state->prediction_native_ms/prediction_count,state->prediction_native_peak,state->prediction_calls);overlay_log(message);
        state->prediction_plan_ms=state->prediction_plan_peak=state->prediction_native_ms=state->prediction_native_peak=0;
        const auto count=static_cast<double>(std::max<std::uint64_t>(state->encodes,1));
        std::snprintf(message,sizeof(message),"Combat encode stages (mean/peak ms): baseline=%.3f/%.3f apply=%.3f/%.3f capture=%.3f/%.3f native=%.3f/%.3f cache=%.3f/%.3f rollback=%.3f/%.3f",state->encode_stages[0]/count,state->encode_stage_peaks[0],state->encode_stages[1]/count,state->encode_stage_peaks[1],state->encode_stages[2]/count,state->encode_stage_peaks[2],state->encode_stages[3]/count,state->encode_stage_peaks[3],state->encode_stages[4]/count,state->encode_stage_peaks[4],state->encode_stages[5]/count,state->encode_stage_peaks[5]);overlay_log(message);
        state->encode_stages.fill(0);state->encode_stage_peaks.fill(0);
        const auto motion=movement_settings.load();
        if(motion){const auto v=state->observed_velocity;std::snprintf(message,sizeof(message),"Input planner: bhop=%d chance=%.0f limit=%.0f jump_writes=%llu retries=%llu grounded=%d speed=%.1f assist=%d strength=%.2f status=%s",motion->bhop,motion->options.chance,motion->options.attempt_limit,state->jump_writes,state->jump_retries,state->observed_grounded,std::hypot(v.x,v.y),motion->keys,motion->strength,movement_status.load());overlay_log(message);}
        state->jump_writes=state->jump_retries=0;
        state->frames=state->rays=state->encodes=state->prediction_calls=state->native_steering=0;
    }
    return result;
}
}
bool camera_view(std::array<float,3>& view) noexcept {
    if(!state || !state->client || stopped)return false;
    __try {
        const auto input=read_native<Address>(state->client+cosmetics::bindings::input);
        if(!input)return false;
        const auto angles=camera_angles(input);if(!finite(angles))return false;
        view={angles.x,angles.y,angles.z};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void bind(Address client,Address scope,hooks::Runtime& hooks) {
    if(state)return;
    if(sdk::sha256_file(reinterpret_cast<HMODULE>(client))!=cosmetics::bindings::sha256 || read_native<Address>(client+reviewed::input_vtable+iris::sdk::binding_values::slots_input_create_move*sizeof(Address))!=client+reviewed::create_move)throw std::runtime_error("No spread: unsupported client binding");
    auto value=std::make_unique<State>();value->client=client;
    // Decoded RIP-relative trace-manager operand from the reviewed client.
    value->trace_manager=iris::sdk::binding_values::layout_trace_manager;
    auto& f=value->f;
    const auto field=[&](const char* type,const char* name){return schema_field(scope,type,name);};
    f.pawn=sdk::active_pawn_field(scope);f.player_pawn=field("CCSPlayerController","m_hPlayerPawn");f.health=field("C_BaseEntity","m_iHealth");f.team=field("C_BaseEntity","m_iTeamNum");f.scene=field("C_BaseEntity","m_pGameSceneNode");f.origin=field("CGameSceneNode","m_vecAbsOrigin");f.dormant=field("CGameSceneNode","m_bDormant");f.view=field("C_BaseModelEntity","m_vecViewOffset");
    f.services=field("C_BasePlayerPawn","m_pWeaponServices");f.active=field("CPlayer_WeaponServices","m_hActiveWeapon");f.clip=field("C_BasePlayerWeapon","m_iClip1");f.reload=field("C_CSWeaponBase","m_bInReload");f.next=field("C_BasePlayerWeapon","m_nNextPrimaryAttackTick");f.ratio=field("C_BasePlayerWeapon","m_flNextPrimaryAttackTickRatio");f.recoil=field("C_CSWeaponBase","m_flRecoilIndex");
    f.definition=field("C_EconEntity","m_AttributeManager")+field("C_AttributeContainer","m_Item")+field("C_EconItemView","m_iItemDefinitionIndex");f.punch=field("C_CSPlayerPawn","m_pAimPunchServices");f.steam=field("CBasePlayerController","m_steamID");f.hltv=field("CBasePlayerController","m_bIsHLTV");f.immune=field("C_CSPlayerPawn","m_bGunGameImmunity");f.bullets=field("CCSWeaponBaseVData","m_nNumBullets");
    f.model=field("CSkeletonInstance","m_modelState");f.model_handle=field("CModelState","m_hModel");f.camera=field("C_BasePlayerPawn","m_pCameraServices");f.camera_fov=field("CCSPlayerBase_CameraServices","m_iFOV");
    f.collision=field("C_BaseModelEntity","m_Collision");f.minimum=field("CCollisionProperty","m_vecMins");f.maximum=field("CCollisionProperty","m_vecMaxs");f.interacts=field("CCollisionProperty","m_collisionAttribute")+field("VPhysicsCollisionAttribute_t","m_nInteractsWith");f.gravity_scale=field("C_BaseEntity","m_flActualGravityScale");f.duck=field("CCSPlayer_MovementServices","m_flDuckAmount");f.identity=field("CEntityInstance","m_pEntity");f.scoped=field("C_CSPlayerPawn","m_bIsScoped");f.flash=field("C_CSPlayerPawnBase","m_flFlashDuration");f.movement=field("C_BasePlayerPawn","m_pMovementServices");f.command_forward=field("CPlayer_MovementServices","m_flCmdForwardMove");f.command_left=field("CPlayer_MovementServices","m_flCmdLeftMove");
    f.move_type=field("C_BaseEntity","m_nActualMoveType");f.penetration=field("CCSWeaponBaseVData","m_flPenetration");f.damage=field("CCSWeaponBaseVData","m_nDamage");f.head_multiplier=field("CCSWeaponBaseVData","m_flHeadshotMultiplier");f.armor_ratio=field("CCSWeaponBaseVData","m_flArmorRatio");f.range=field("CCSWeaponBaseVData","m_flRange");f.range_modifier=field("CCSWeaponBaseVData","m_flRangeModifier");
    f.armor=field("C_CSPlayerPawn","m_ArmorValue");f.items=field("C_BasePlayerPawn","m_pItemServices");f.helmet=field("CCSPlayer_ItemServices","m_bHasHelmet");f.velocity=field("C_BaseEntity","m_vecAbsVelocity");f.flags=field("C_BaseEntity","m_fFlags");
    f.maximum_speed=field("CPlayer_MovementServices","m_flMaxspeed");f.friction=field("CPlayer_MovementServices_Humanoid","m_flSurfaceFriction");f.movement_button_value=field("CPlayer_MovementServices","m_nButtons")+8;
    if(f.model!=0x140 || f.model_handle!=0xa0)throw std::runtime_error("No spread: skeleton layout differs from reviewed bindings");
    value->move=std::make_unique<hooks::Hook>(hooks,reinterpret_cast<void*>(client+reviewed::create_move),reinterpret_cast<void*>(&create_move));value->original_move=value->move->original<CreateMove>();
    value->prepare=std::make_unique<hooks::Hook>(hooks,reinterpret_cast<void*>(client+reviewed::prepare_command),reinterpret_cast<void*>(&prepare));value->original_prepare=value->prepare->original<Prepare>();
    value->encode=std::make_unique<hooks::Hook>(hooks,reinterpret_cast<void*>(client+reviewed::encode_command_delta),reinterpret_cast<void*>(&encode));value->original_encode=value->encode->original<Encode>();
    value->prediction=std::make_unique<hooks::Hook>(hooks,reinterpret_cast<void*>(client+binding_values::movement_run_command),reinterpret_cast<void*>(&run_prediction));value->original_prediction=value->prediction->original<decltype(value->original_prediction)>();
    state=value.release();state->move->enable();state->prepare->enable();state->encode->enable();state->prediction->enable();status="Ready";legit_status="Ready";
    overlay_log("Input rewrite: pre-prediction movement plans, clone-owned wire replay enabled.");
}
}
