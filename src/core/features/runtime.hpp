#pragma once
#include "core/features/inventory/inventory.hpp"
#include "core/features/inventory/music.hpp"
#include "core/features/inventory/native_backend.hpp"
#include "core/features/inventory/requests.hpp"
#include "core/features/inventory/preview_receipts.hpp"
#include "core/features/player/models.hpp"
#include "core/features/player/sound_cue.hpp"
#include "core/sdk/generated/client_bindings.hpp"
#include "util/memory.hpp"
#include "core/hooks/inline_hook.hpp"
#include "core/log.hpp"
#include "core/render/renderer.hpp"
#include "core/module.hpp"
#include "core/thread_state.hpp"
#include "core/features/world/skyboxes.hpp"
#include <Windows.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

#include "core/sdk/client.hpp"
#include "core/sdk/types.hpp"
namespace iris::cosmetics::detail {
using namespace iris::sdk;
struct ViewmodelOverride { Address pawn{}; std::array<float,3> offsets{}; float fov{}; bool fov_override{true}; bool offsets_override{true}; };
struct HudKnife { int team{}; Address vdata{}; };
struct SavedField { Address address; std::vector<std::byte> bytes; Address scene{}; };
struct ChangedWeapon {
    Address entity{}; std::uint32_t handle{}; Selection selection; std::vector<SavedField> fields;
    int original_definition{}, team{};
    std::string original_model;
    Address scene{};
    std::uint64_t original_mesh{};
    bool model_changed{};
    unsigned refresh_ticks{};
    bool finish_override{};
    bool menu_weapon{};
};
struct NativePreview {
    alignas(16) std::array<std::byte, bindings::item_size> item{};
    std::optional<Selection> requested, rendering;
    ULONGLONG changed{}, touched{}, started{}, sampled{}, diagnostic{};
    std::uint64_t job{};
    bool constructed{}, failed{}, modality{}, retiring{};
    PreviewImage image;
    sdk::preview::CameraSettings camera_settings;
    PreviewPose pose,submitted_pose;
    bool camera_supported{};
    ULONGLONG camera_changed{},camera_retry{};
};
struct EnvironmentField { std::vector<std::byte> original, written; bool resource{}; };
struct EnvironmentEntity { Address address{}; std::string kind; std::map<int,EnvironmentField> fields; bool dirty{}; };
struct Runtime {
    std::recursive_mutex mutex;
    Configuration requested;
    std::unique_ptr<inventory::NativeBackend> inventory_backend;
    std::unique_ptr<inventory::OwnedItems> inventory_items;
    using InventoryEquip=bool(__fastcall*)(void*,int,int,std::uint64_t);
    std::unique_ptr<hooks::Hook> inventory_equip_guard;
    InventoryEquip original_inventory_equip{};
    Configuration inventory_accepted;
    std::uint64_t inventory_revision{};
    ULONGLONG inventory_poll{};
    InventoryReceipt inventory_receipt;
    std::string inventory_fault;
    std::condition_variable_any inventory_ready;
    NativePreview preview;
    PreviewReceipts preview_receipts;
    Environment environment;
    std::vector<std::uint32_t> weather_particles;
    Address weather_manager{}, weather_pawn{}, weather_chunks{};
    int weather_mode{};
    std::map<std::string,Address> resource_cache;
    ULONGLONG weather_retry{};
    std::atomic<bool> accept_enabled{}, killfeed_enabled{};
    ULONGLONG accept_check{};
    std::unique_ptr<hooks::Hook> deathnotice_hook;
    HudUpdate original_deathnotice{};
    using ScopeRender=void(__fastcall*)(void*,std::uint8_t*);
    std::unique_ptr<hooks::Hook> scope_render_hook;
    ScopeRender original_scope_render{};
    using FlashEffect=void(__fastcall*)(Address,Address,float*);
    std::unique_ptr<hooks::Hook> flash_effect_hook;
    FlashEffect original_flash_effect{};
    Address schema_scope{};
    std::map<std::string,int> environment_offsets;
    std::map<std::uint32_t,EnvironmentEntity> environment_entities;
    std::map<std::uint32_t,EnvironmentEntity> smoke_volumes;
    ULONGLONG environment_scan{}, environment_tick{};
    EnvironmentEntity viewmodel_fields, camera_fields;
    std::map<std::uint32_t,visuals::SoundCue> player_sounds;
    ULONGLONG visual_fault_at{}, world_visual_fault_at{}, music_fault_at{};
    std::map<Address,EnvironmentEntity> native_variables;
    std::array<ULONGLONG,4> environment_fault_at{};
    std::uint32_t camera_pawn_handle{0xffffffff};
    Address camera_pawn{};
    Status report;
    EquippedLoadout roster;
    ULONGLONG next_roster_poll{};
    std::optional<ChangedWeapon> glove, agent;
    std::optional<ChangedMusic> music;
    std::string agent_resource;
    std::map<std::uint32_t,ChangedWeapon> menu_players;
    std::map<std::uint32_t,Address> menu_preview_players;
    ULONGLONG menu_model_tick{};
    bool menu_model_refresh{};
    std::atomic<unsigned> glove_reads{};
    ULONGLONG glove_refresh_until{}, glove_next_refresh{}, glove_log_at{}, agent_next_refresh{};
    float strafe_filtered{};
    float previous_movement_yaw{};
    bool movement_yaw_valid{}, strafe_alternate{};
    Address movement_pawn{};
    int movement_tick{-1};
    std::uint32_t rules_handle{0xffffffff};
    ULONGLONG rules_scan{};
    Address client{}, chunks{}, camera{};
    Fields fields{};
    std::unique_ptr<hooks::Runtime> hooks;
    std::unique_ptr<hooks::Hook> frame, glove_update_hook;
    GloveUpdate original_glove_update{};
    std::unique_ptr<hooks::Hook> default_hand_hook;
    DefaultHandModel original_default_hand{};
    std::unique_ptr<hooks::Hook> camera_hook;
    std::unique_ptr<hooks::Hook> forward_input_hook,side_input_hook;
    MovementInput original_forward_input{},original_side_input{};
    std::atomic<bool> strafe_trace_seen{};
    std::atomic<float> strafe_axis{},forward_axis{};
    std::atomic<bool> movement_override{};
    std::unique_ptr<hooks::Hook> viewmodel_hook, pawn_viewmodel_hook;
    ViewmodelSettings original_viewmodel{};
    PawnViewmodelSettings original_pawn_viewmodel{};
    std::atomic<std::shared_ptr<const ViewmodelOverride>> viewmodel_override;
    CameraThink original_camera_think{};
    std::atomic<bool> camera_enabled{};
    std::atomic<float> camera_distance{120};
    std::unique_ptr<hooks::Hook> paint_hook, seed_hook, wear_hook;
    std::unique_ptr<hooks::Hook> attributes_hook;
    IterateAttributes original_attributes{};
    std::unique_ptr<hooks::Hook> hud_update_hook;
    std::unique_ptr<hooks::Hook> name_hook;
    ItemName original_name{};
    NameContext name_context{};
    RebuildName rebuild_name{};
    HudUpdate original_hud_update{};
    SubclassChanged clear_hud{};
    ItemInteger original_paint{}, original_seed{};
    ItemWear original_wear{};
    FrameStage original{};
    Command regenerate{}, third{}, first{};
    SetModel set_model{};
    SubclassChanged subclass_changed{};
    MakeToken make_token{};
    RebuildMaterials rebuild_materials{};
    std::map<std::uint32_t, ChangedWeapon> changed;
    std::atomic<std::shared_ptr<const std::map<Address, Selection>>> finishes;
    std::atomic<std::shared_ptr<const std::map<Address, Selection>>> menu_finishes;
    std::atomic<std::shared_ptr<const HudKnife>> hud_knife;
    bool stopping{}, camera_saved{}, original_camera{}, camera_applied{};
    Address last_pawn{};
    bool first_update{true};
    unsigned stages_seen{};
};

Runtime& runtime();
void update_player_visuals(Runtime& state);
bool guarded_player_visuals(Runtime* state);
extern ThreadState<std::map<void*, Selection>> name_cache;
extern ThreadState<std::map<void*, HudKnife>> hud_cache;
inline Address entity(Runtime& state, std::uint32_t handle) { return sdk::entity(state.client, handle); }
template<class T> void environment_write(EnvironmentEntity& saved,int offset,const T& value) {
    if(offset<0)return;
    const auto address=saved.address+offset;
    std::vector<std::byte> current(sizeof(T));
    if(!memory::read(address,current))return;
    auto& field=saved.fields[offset];
    // A new network value becomes the restoration baseline; preserve it.
    if(field.original.empty() || current!=field.written)field.original=current;
    const auto bytes=std::as_bytes(std::span{&value,std::size_t{1}});
    if(current.size()==bytes.size() && std::equal(current.begin(),current.end(),bytes.begin()))return;
    if(memory::write(address,bytes)){field.written.assign(bytes.begin(),bytes.end());saved.dirty=true;}
}

bool automatic_movement(Runtime& state,int slot,float* movement);
void __fastcall forward_input_hook(void* input,int slot,float* movement);
void __fastcall side_input_hook(void* input,int slot,float* movement);
void release_strafe(Runtime& state);
void assist_strafe(Runtime& state,Address pawn,int horizontal);
void publish_movement(Runtime& state);
void __fastcall viewmodel_hook(std::array<float,3>* offsets,float* fov,bool clamp);
void __fastcall pawn_viewmodel_hook(void* pawn,std::array<float,3>* offsets,float* fov);
void __fastcall camera_hook(void* self,int slot);
void restore_camera_fields(Runtime& state);
void update_camera_fields(Runtime& state);
int environment_offset(Runtime& state,const char* type,const char* field);
void assign_resource(Address* target,const Address* source);
Address load_resource(Runtime& state,const char* path,std::uint32_t type);
void environment_resource(EnvironmentEntity& saved,int offset,Address value);
void restore_environment_field(EnvironmentEntity& saved,int offset,bool entity_valid=true);
void restore_environment_entity(EnvironmentEntity& saved,bool entity_valid=true);
void update_environment(Runtime& state);
void update_native_variables(Runtime& state);
void clear_weather(Runtime& state);
void update_weather(Runtime& state);
int environment_exception(Runtime* state,int phase,EXCEPTION_POINTERS* exception);
bool guarded_environment(Runtime* state,int phase);
void publish_finishes(Runtime& state);
std::optional<Selection> overridden_finish(void* item);
int __fastcall paint_hook(void* item);
int __fastcall seed_hook(void* item);
float __fastcall wear_hook(void* item, float fallback);
bool __fastcall attributes_hook(void* item, void* visitor);
const char* __fastcall name_hook(void* item, bool short_name);
void __fastcall hud_update_hook(void* self, void* pawn);
void restore(Runtime& state, ChangedWeapon& weapon);
void read_roster(Runtime& state);
bool guarded_roster(Runtime* state);
void restore_glove(Runtime& state);
void update_glove(Runtime& state, Address pawn, std::uint32_t handle, bool alive, int team);
bool __fastcall default_hand_hook(void* pawn);
void __fastcall glove_update_hook(void* self);
void restore_agent(Runtime& state);
void update_menu_models(Runtime& state);
bool guarded_menu_models(Runtime* state);
std::vector<std::pair<Address,int>> weapon_owners(Runtime& state,Address pawn,bool alive,int team);
void update_agent(Runtime& state,Address pawn,std::uint32_t handle,bool alive,int team);
void update(Runtime& state);
bool guarded_update(Runtime* state);
Address preview_generator(const Runtime& state);
void clear_preview(Runtime& state);
void update_preview(Runtime& state);
bool guarded_preview(Runtime* state);
Runtime& runtime();
void recover(Runtime& state);
void update_owned_inventory(Runtime& state);
bool __fastcall inventory_equip_hook(void* manager,int team,int slot,std::uint64_t id);
void restore_inventory_views(Runtime& state);
void restore_owned_inventory(Runtime& state);
bool guarded_recover(Runtime* state);
void __fastcall frame_hook(void* self, int stage);
void bind(Runtime& state);
bool guarded_bind(Runtime* state);
void __fastcall deathnotice_hook(void* self,void* event);
}
