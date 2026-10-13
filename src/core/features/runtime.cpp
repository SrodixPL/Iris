#include "core/sdk/binding_values.hpp"
#include "core/features/inventory/weapon_mesh.hpp"
#include "core/features/runtime.hpp"
#include "core/features/world/environment.hpp"
#include "core/features/world/native.hpp"
#include "core/features/world/esp.hpp"
#include "core/features/combat/nospread.hpp"
#include "core/features/player/visuals.hpp"
#include "core/features/player/esp/pipeline.hpp"
#include "core/features/player/visual_native.hpp"
namespace iris::cosmetics::detail {
// The overlay already pins the DLL. This hook/trampoline also remains valid until
// process exit: stop restores state on the game thread and leaves it forwarding.
Runtime* runtime_value{};
std::once_flag runtime_once;
ThreadState<std::map<void*, Selection>> name_cache;
ThreadState<std::map<void*, HudKnife>> hud_cache;
void __fastcall scope_render(void* self,std::uint8_t* output) {
    auto& state=runtime();state.original_scope_render(self,output);
    if(!output||state.stopping)return;
    std::lock_guard lock(state.mutex);
    const auto snapshot=nospread::hud_snapshot.load();
    DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if((state.requested.hud.scope_lines||state.requested.hud.scope_hide)&&!nospread::stopped&&foreground==GetCurrentProcessId()&&!renderer::menu_open()&&snapshot&&snapshot->scoped&&GetTickCount64()-snapshot->sampled<=150)output[4]=0;
}
Runtime& runtime() {
    std::call_once(runtime_once, [] { runtime_value = new Runtime; });
    return *runtime_value;
}
void recover(Runtime& state) {
    // A failed update can have written only part of a weapon. Keep its snapshot
    // and attempt restoration on this same game thread before suspending writes.
    state.stopping = true;
    nospread::enabled=false;
    nospread::stopped=true;
    visuals::snapshot.store(nullptr);
    world::esp::stop();
    clear_world_visuals(state);
    try {restore_music(state);}catch(const std::exception& error){overlay_log(error.what());}
    update_player_visuals(state);
    update_environment(state);
    update_native_variables(state);
    restore_camera_fields(state);
    clear_weather(state);
    state.camera_enabled = false;
    state.hud_knife.store(nullptr);
    release_strafe(state);
    restore_agent(state);
    update_menu_models(state);
    restore_glove(state);
    try {restore_hud_weapon_meshes(state);}catch(const std::exception& error){overlay_log(error.what());}
    for (auto it = state.changed.begin(); it != state.changed.end();) {
        try { restore(state, it->second); it = state.changed.erase(it); }
        catch (...) { ++it; }
    }
    restore_owned_inventory(state);
    if (state.camera_saved) {
        (state.original_camera ? state.third : state.first)();
        state.camera_saved = state.camera_applied = false;
    }
    state.report.updated_weapons = static_cast<unsigned>(state.changed.size());
}
bool guarded_recover(Runtime* state) {
    __try { recover(*state); return true; }
    __except (GetExceptionCode() == 0xe06d7363 ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void __fastcall frame_hook(void* self, int stage) {
    auto& state = runtime();
    unsigned unobserved=0;
    state.preview_receipts.thread.compare_exchange_strong(unobserved,GetCurrentThreadId(),std::memory_order_release,std::memory_order_relaxed);
    if (stage == 0 && state.report.available) {
        if(state.accept_enabled.load() && GetTickCount64()>=state.accept_check){
            state.accept_check=GetTickCount64()+500;
            // This object exists only while the native ready-up prompt is alive.
            // Its state becomes 2 when accepted; native code owns cancellation.
            const auto prompt=memory::read<Address>(state.client+bindings::matchmaking_session).value_or(0);
            const auto ready=prompt?memory::read<int>(prompt+iris::sdk::binding_values::layout_matchmaking_ready):std::optional<int>{};
            if(ready && *ready>=0 && *ready<2)
                reinterpret_cast<AcceptMatch>(state.client+bindings::accept_match)(nullptr,"");
        }
        try { std::lock_guard lock(state.mutex); publish_movement(state); }
        catch (const std::exception& error) { overlay_log(error.what()); }
    }
    state.original(self, stage);
    std::lock_guard lock(state.mutex);
    if (stage >= 0 && stage < 32 && !(state.stages_seen & (1u << stage))) {
        state.stages_seen |= 1u << stage;
        char message[100]{};
        std::snprintf(message, sizeof(message), "Cosmetic frame callback observed stage %d.", stage);
        overlay_log(message);
    }
    if(stage==0 && state.report.available)try {
        update_owned_inventory(state);
        if(!guarded_menu_models(&state))throw std::runtime_error("Menu model update raised an exception.");
    }catch(const std::exception& error){state.menu_model_tick=GetTickCount64()+10000;overlay_log(error.what());}
    if (stage != 6) return;
    if (!state.report.available) return;
    if(GetTickCount64()>=state.world_visual_fault_at)try {
        if(!guarded_world_visuals(&state))throw std::runtime_error("World visual reader raised an exception.");
    }catch(const std::exception& error){clear_world_visuals(state);state.world_visual_fault_at=GetTickCount64()+10000;overlay_log(error.what());}
    if(GetTickCount64()>=state.music_fault_at)try {update_music(state);}
    catch(const std::exception& error){state.music_fault_at=GetTickCount64()+10000;overlay_log(error.what());}
    if(GetTickCount64()>=state.visual_fault_at)try {
        if(!guarded_player_visuals(&state))throw std::runtime_error("Player visual reader raised an exception.");
    }catch(const std::exception& error){visuals::snapshot.store(nullptr);visuals::native::stop();state.visual_fault_at=GetTickCount64()+10000;overlay_log(error.what());}
    visuals::esp::publish();
    if (!state.stopping && GetTickCount64() >= state.next_roster_poll) {
        state.next_roster_poll = GetTickCount64() + 1000;
        try {
            if (!guarded_roster(&state)) throw std::runtime_error("Equipped inventory reader raised an exception.");
        } catch (const std::exception& error) {
            state.roster.available = false;
            state.roster.detail = error.what();
            state.roster.teams = {};
        }
    }
    for(int phase=0;phase<4;++phase)try {
        if(GetTickCount64()<state.environment_fault_at[phase])continue;
        (void)guarded_environment(&state,phase);
    } catch(const std::exception& error){
        if(GetTickCount64()>=state.environment_fault_at[phase]){
            state.environment_fault_at[phase]=GetTickCount64()+10000;overlay_log(error.what());
        }
    }
    try {
        if (!guarded_preview(&state)) throw std::runtime_error("Preview renderer raised an exception.");
    } catch (const std::exception& error) {
        state.preview.failed = true;
        state.preview.image.detail = error.what();
        overlay_log(error.what());
    }
    try {
        if (!guarded_update(&state)) throw std::runtime_error("Game update raised an exception; cosmetic writes are suspended.");
    } catch (const std::exception& error) {
        state.report.available = false;
        state.report.detail = error.what();
        overlay_log(error.what());
        try {
            if (!guarded_recover(&state) || !state.changed.empty() || !state.hud_weapon_meshes.empty() || state.glove || state.agent || state.music ||
                (state.inventory_items && state.inventory_items->recovery_pending())) {
                state.report.detail += " Restoration was incomplete; restart the map.";
                overlay_log("Cosmetic restoration was incomplete after a failed update.");
            }
        } catch (...) { overlay_log("Cosmetic restoration failed after a failed update."); }
    }
}
void bind(Runtime& state) {
    const auto client = GetModuleHandleW(L"client.dll");
    if (sha256_file(client) != bindings::sha256) throw std::runtime_error("Unsupported client binary. Cosmetic writes disabled pending a binding update.");
    state.client = reinterpret_cast<Address>(client);
    const auto nt = state.client + get<std::uint32_t>(state.client + 0x3c);
    if (get<std::uint32_t>(nt + 8) != bindings::timestamp || get<std::uint32_t>(nt + 24 + 56) != bindings::image_size)
        throw std::runtime_error("Loaded client differs from the reviewed binary.");
    const auto schema = reinterpret_cast<Address>(interface(L"schemasystem.dll", "SchemaSystem_001"));
    const auto client_interface = reinterpret_cast<Address>(interface(L"client.dll", "Source2Client002"));
    if (method(client_interface,static_cast<int>(iris::sdk::binding_values::slots_client_frame_stage)) != state.client + bindings::frame_stage)
        throw std::runtime_error("Client update callback differs from the reviewed interface.");
    using Scope = Address(__fastcall*)(void*, const char*, void*);
    const auto scope = reinterpret_cast<Scope>(method(schema,static_cast<int>(iris::sdk::binding_values::slots_schema_module_scope)))(reinterpret_cast<void*>(schema), "client.dll", nullptr);
    if (!scope) throw std::runtime_error("Client schema scope unavailable.");
    state.schema_scope = scope;
    bind_music(state);
    auto& f = state.fields;
    f.pawn = active_pawn_field(scope);
    f.health = schema_field(scope, "C_BaseEntity", "m_iHealth");
    f.flags = schema_field(scope, "C_BaseEntity", "m_fFlags");
    f.move_type = schema_field(scope, "C_BaseEntity", "m_nActualMoveType");
    f.owner = schema_field(scope, "C_BaseEntity", "m_hOwnerEntity");
    f.team = schema_field(scope, "C_BaseEntity", "m_iTeamNum");
    f.services = schema_field(scope, "C_BasePlayerPawn", "m_pWeaponServices");
    f.weapons = schema_field(scope, "CPlayer_WeaponServices", "m_hMyWeapons");
    f.attributes = schema_field(scope, "C_EconEntity", "m_AttributeManager");
    f.item = schema_field(scope, "C_AttributeContainer", "m_Item");
    f.definition = schema_field(scope, "C_EconItemView", "m_iItemDefinitionIndex");
    f.id_high = schema_field(scope, "C_EconItemView", "m_iItemIDHigh");
    f.name = schema_field(scope, "C_EconItemView", "m_szCustomNameOverride");
    f.paint = schema_field(scope, "C_EconEntity", "m_nFallbackPaintKit");
    f.seed = schema_field(scope, "C_EconEntity", "m_nFallbackSeed");
    f.wear = schema_field(scope, "C_EconEntity", "m_flFallbackWear");
    f.stattrak = schema_field(scope, "C_EconEntity", "m_nFallbackStatTrak");
    f.scene = schema_field(scope, "C_BaseEntity", "m_pGameSceneNode");
    f.model = schema_field(scope, "CSkeletonInstance", "m_modelState");
    f.mesh = schema_field(scope, "CModelState", "m_MeshGroupMask");
    f.model_name = schema_field(scope, "CModelState", "m_ModelName");
    f.subclass = schema_field(scope, "C_BaseEntity", "m_nSubclassID");
    f.gloves = schema_field(scope, "C_CSPlayerPawn", "m_EconGloves");
    f.glove_reapply = schema_field(scope, "C_CSPlayerPawn", "m_bNeedToReApplyGloves");
    f.initialized = schema_field(scope, "C_EconItemView", "m_bInitialized");
    f.quality = schema_field(scope, "C_EconItemView", "m_iEntityQuality");
    state.set_model = reinterpret_cast<SetModel>(state.client + bindings::set_model);
    state.subclass_changed = reinterpret_cast<SubclassChanged>(state.client + bindings::subclass_changed);
    state.rebuild_materials = reinterpret_cast<RebuildMaterials>(state.client + bindings::rebuild_weapon_materials);
    state.set_mesh_group_mask = reinterpret_cast<SetMeshGroupMask>(state.client + iris::sdk::binding_values::client_set_mesh_group_mask);
    state.make_token = reinterpret_cast<MakeToken>(GetProcAddress(GetModuleHandleW(L"tier0.dll"), "?Make@CUtlStringToken@@SA?AV1@PEBD@Z"));
    if (!state.make_token) throw std::runtime_error("Native subclass token function is unavailable.");
    state.regenerate = reinterpret_cast<Command>(state.client + bindings::regenerate);
    state.third = reinterpret_cast<Command>(state.client + bindings::thirdperson);
    state.first = reinterpret_cast<Command>(state.client + bindings::firstperson);
    state.hooks = std::make_unique<hooks::Runtime>();
    state.paint_hook = std::make_unique<hooks::Hook>(*state.hooks, reinterpret_cast<void*>(state.client + bindings::item_paint), reinterpret_cast<void*>(&paint_hook));
    state.seed_hook = std::make_unique<hooks::Hook>(*state.hooks, reinterpret_cast<void*>(state.client + bindings::item_seed), reinterpret_cast<void*>(&seed_hook));
    state.wear_hook = std::make_unique<hooks::Hook>(*state.hooks, reinterpret_cast<void*>(state.client + bindings::item_wear), reinterpret_cast<void*>(&wear_hook));
    state.original_paint = state.paint_hook->original<ItemInteger>();
    state.original_seed = state.seed_hook->original<ItemInteger>();
    state.original_wear = state.wear_hook->original<ItemWear>();
    state.attributes_hook = std::make_unique<hooks::Hook>(*state.hooks, reinterpret_cast<void*>(state.client + bindings::item_iterate_attributes), reinterpret_cast<void*>(&attributes_hook));
    state.original_attributes = state.attributes_hook->original<IterateAttributes>();
    state.name_context = reinterpret_cast<NameContext>(state.client + bindings::name_context);
    state.rebuild_name = reinterpret_cast<RebuildName>(state.client + bindings::rebuild_item_name);
    state.name_hook = std::make_unique<hooks::Hook>(*state.hooks, reinterpret_cast<void*>(state.client + bindings::item_name), reinterpret_cast<void*>(&name_hook));
    state.original_name = state.name_hook->original<ItemName>();
    state.clear_hud = reinterpret_cast<SubclassChanged>(state.client + bindings::clear_hud);
    state.hud_update_hook = std::make_unique<hooks::Hook>(*state.hooks, reinterpret_cast<void*>(state.client + bindings::hud_update), reinterpret_cast<void*>(&hud_update_hook));
    state.original_hud_update = state.hud_update_hook->original<HudUpdate>();
    state.glove_update_hook = std::make_unique<hooks::Hook>(*state.hooks, reinterpret_cast<void*>(state.client + bindings::glove_update), reinterpret_cast<void*>(&glove_update_hook));
    state.original_glove_update = state.glove_update_hook->original<GloveUpdate>();
    state.default_hand_hook = std::make_unique<hooks::Hook>(*state.hooks, reinterpret_cast<void*>(state.client + bindings::default_hand_model), reinterpret_cast<void*>(&default_hand_hook));
    state.original_default_hand = state.default_hand_hook->original<DefaultHandModel>();
    state.camera_hook = std::make_unique<hooks::Hook>(*state.hooks, reinterpret_cast<void*>(state.client + bindings::camera_think), reinterpret_cast<void*>(&camera_hook));
    state.original_camera_think = state.camera_hook->original<CameraThink>();
    state.forward_input_hook=std::make_unique<hooks::Hook>(*state.hooks,reinterpret_cast<void*>(state.client+bindings::movement_forward),reinterpret_cast<void*>(&forward_input_hook));
    state.side_input_hook=std::make_unique<hooks::Hook>(*state.hooks,reinterpret_cast<void*>(state.client+bindings::movement_side),reinterpret_cast<void*>(&side_input_hook));
    state.original_forward_input=state.forward_input_hook->original<MovementInput>();
    state.original_side_input=state.side_input_hook->original<MovementInput>();
    state.viewmodel_hook=std::make_unique<hooks::Hook>(*state.hooks,reinterpret_cast<void*>(state.client+bindings::viewmodel_settings),reinterpret_cast<void*>(&viewmodel_hook));
    state.pawn_viewmodel_hook=std::make_unique<hooks::Hook>(*state.hooks,reinterpret_cast<void*>(state.client+bindings::pawn_viewmodel_settings),reinterpret_cast<void*>(&pawn_viewmodel_hook));
    state.original_viewmodel=state.viewmodel_hook->original<ViewmodelSettings>();
    state.original_pawn_viewmodel=state.pawn_viewmodel_hook->original<PawnViewmodelSettings>();
    state.frame = std::make_unique<hooks::Hook>(*state.hooks, reinterpret_cast<void*>(state.client + bindings::frame_stage), reinterpret_cast<void*>(&frame_hook));
    state.original = state.frame->original<FrameStage>();
    const auto manager=state.client+bindings::inventory_manager;
    if(get<Address>(manager)!=state.client+bindings::inventory_vtable ||
        method(manager,static_cast<int>(iris::sdk::binding_values::slots_inventory_equip))!=
            state.client+iris::sdk::binding_values::client_inventory_manager_equip)
        throw std::runtime_error("Inventory equip callback differs from the reviewed build.");
    state.inventory_equip_guard=std::make_unique<hooks::Hook>(*state.hooks,
        reinterpret_cast<void*>(state.client+iris::sdk::binding_values::client_inventory_manager_equip),
        reinterpret_cast<void*>(&inventory_equip_hook));
    state.original_inventory_equip=state.inventory_equip_guard->original<Runtime::InventoryEquip>();
    if (!module::pin()) throw std::runtime_error("Cannot retain cosmetic callback lifetime.");
    state.deathnotice_hook=std::make_unique<hooks::Hook>(*state.hooks,reinterpret_cast<void*>(state.client+bindings::deathnotice_event),reinterpret_cast<void*>(&deathnotice_hook));
    state.original_deathnotice=state.deathnotice_hook->original<HudUpdate>();
    state.scope_render_hook=std::make_unique<hooks::Hook>(*state.hooks,reinterpret_cast<void*>(state.client+bindings::scope_render),reinterpret_cast<void*>(&scope_render));
    state.original_scope_render=state.scope_render_hook->original<Runtime::ScopeRender>();
    bind_preview_receipts(state);
    state.report.available = true;
    state.inventory_equip_guard->enable();
    state.deathnotice_hook->enable();
    state.scope_render_hook->enable();
    bind_flash(state);
    enable_preview_receipts(state);
    state.paint_hook->enable();
    state.seed_hook->enable();
    state.wear_hook->enable();
    state.attributes_hook->enable();
    state.name_hook->enable();
    state.hud_update_hook->enable();
    state.glove_update_hook->enable();
    state.default_hand_hook->enable();
    state.camera_hook->enable();
    state.forward_input_hook->enable();state.side_input_hook->enable();
    state.viewmodel_hook->enable();state.pawn_viewmodel_hook->enable();
    nospread::bind(state.client,state.schema_scope,*state.hooks);
    world::bind(state.client,*state.hooks);
    visuals::native::bind(state.client,*state.hooks);
    state.frame->enable();
    state.report.detail = "Runtime connected. Waiting for a practice-map player.";
    overlay_log("Cosmetic runtime: client fingerprint verified; schema fields resolved; game update hook installed.");
}
bool guarded_bind(Runtime* state) {
    __try { bind(*state); return true; }
    __except (GetExceptionCode() == 0xe06d7363 ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

namespace iris::cosmetics {
using namespace detail;
void start() noexcept {
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    state.stopping = false;
    if (state.frame) return;
    if (!GetModuleHandleW(L"client.dll")) {
        overlay_log("Game runtime: client.dll is absent; game features remain unavailable.");
        return;
    }
    try {
        if (!guarded_bind(&state)) throw std::runtime_error("Game interface layout mismatch. Cosmetic writes disabled.");
    } catch (const std::exception& error) {
        state.report.available = false;
        state.report.detail = error.what();
        overlay_log(error.what());
    }
}
void submit(const Configuration& configuration) {
    validate_configuration(configuration);
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    for(int team=0;team<2;++team){
        const auto& resource=configuration.custom_models[team];
        if(resource.empty() || resource==state.requested.custom_models[team])continue;
        const auto path=models::game_content()/(resource+"_c");
        const auto canonical=std::filesystem::weakly_canonical(path);
        const auto relative=canonical.lexically_relative(std::filesystem::weakly_canonical(models::game_content())).generic_string();
        if(relative!=resource+"_c" || !models::compiled_model(canonical))throw std::runtime_error("Custom model is missing or invalid: "+resource);
    }
    if(state.requested.custom_models!=configuration.custom_models || state.requested.agents!=configuration.agents){
        state.menu_model_tick=0;
        state.menu_model_refresh=true;
    }
    state.requested = configuration;
    state.inventory_receipt={configuration.revision,InventoryAcceptance::Pending,{}};
    state.inventory_poll=0;
    nospread::settings.store(std::make_shared<const nospread::Settings>(configuration.no_spread_settings));
    nospread::aim_settings.store(std::make_shared<const combat::Settings>(configuration.combat_settings));
    nospread::enabled=configuration.no_spread;
    nospread::hud_enabled=configuration.hud.scope_lines||configuration.hud.scope_hide||configuration.hud.inaccuracy_overlay||configuration.hud.crosshair_mode==1;
    state.accept_enabled=configuration.auto_accept;state.killfeed_enabled=configuration.preserve_killfeed;
}
Status status() {
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    auto report=state.report;
    if(state.inventory_receipt.acceptance==InventoryAcceptance::Rejected && !state.inventory_receipt.detail.empty())
        report.detail=state.inventory_receipt.detail;
    else if(state.inventory_receipt.acceptance==InventoryAcceptance::Accepted && state.inventory_items) {
        report.applied_revision=state.inventory_receipt.revision;
        report.detail="Local inventory synced: "+std::to_string(state.inventory_items->owned_count())+" Iris items.";
    }
    return report;
}
InventoryReceipt await_inventory(std::uint64_t revision,unsigned timeout_ms) {
    auto& state=runtime();
    std::unique_lock lock(state.mutex);
    if(!state.report.available || state.stopping)
        return {revision,InventoryAcceptance::Rejected,state.report.available?"Inventory updates are stopped.":state.report.detail};
    const auto completed=[&]{return state.stopping || !state.report.available ||
        state.inventory_receipt.revision!=revision || state.inventory_receipt.acceptance!=InventoryAcceptance::Pending;};
    if(!state.inventory_ready.wait_for(lock,std::chrono::milliseconds(timeout_ms),completed))
        return {revision,InventoryAcceptance::Rejected,"Timed out waiting for the game-thread inventory transaction."};
    if(state.inventory_receipt.revision!=revision)
        return {revision,InventoryAcceptance::Rejected,"The inventory request was superseded before acceptance."};
    if(state.stopping || !state.report.available)
        return {revision,InventoryAcceptance::Rejected,"The game inventory became unavailable before acceptance."};
    return state.inventory_receipt;
}
EquippedLoadout equipped_loadout() {
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    return state.roster;
}
void submit_environment(const Environment& environment) {
    auto& state=runtime();std::lock_guard lock(state.mutex);
    if(state.stopping || state.environment==environment)return;
    state.environment=environment;
    world::publish(environment);
}
void request_preview(const Selection& selection,const PreviewPose& pose) {
    if (!valid(selection) || !sdk::preview::valid_control(pose)) return;
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    if (!state.report.available) return;
    auto& preview = state.preview;
    preview.touched = GetTickCount64();
    if(preview.pose!=pose){preview.pose=pose;preview.camera_changed=preview.touched;}
    if (!preview.requested || *preview.requested != selection) {
        preview.requested = selection; preview.changed = preview.touched;
    }
}
PreviewImage preview_image(std::uint64_t after_revision) {
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    if (state.preview.image.revision == after_revision) return {};
    return state.preview.image;
}
void stop() noexcept {
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    state.stopping = true;
    state.inventory_poll=0;
    state.inventory_ready.notify_all();
    nospread::enabled=false;
    nospread::stopped=true;
    visuals::snapshot.store(nullptr);
    world::stop();
    world::esp::stop();
    visuals::native::stop();
    state.strafe_axis=0;
    state.viewmodel_override.store(nullptr);
    state.accept_enabled=false;state.killfeed_enabled=false;
    state.camera_enabled = false;
}
}
