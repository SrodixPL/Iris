#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
#include "core/features/inventory/patch.hpp"

namespace iris::cosmetics::detail {
void restore_glove(Runtime& state) {
    if (!state.glove) return;
    auto& saved = *state.glove;
    saved.finish_override = false;
    publish_finishes(state);
    if (entity(state, saved.handle) == saved.entity) {
        for (auto it = saved.fields.rbegin(); it != saved.fields.rend(); ++it)
            if (!memory::write(it->address, std::span<const std::byte>(it->bytes))) throw std::runtime_error("Cannot restore glove field.");
        if (!memory::write(saved.entity + state.fields.glove_reapply, true)) throw std::runtime_error("Cannot refresh restored gloves.");
    }
    state.glove.reset();
}
void update_glove(Runtime& state, Address pawn, std::uint32_t handle, bool alive, int team) {
    const Selection* desired = alive && team >= 2 && team <= 3 && state.requested.enabled && !state.stopping &&
        state.requested.gloves[team - 2].enabled ? &state.requested.gloves[team - 2] : nullptr;
    if (state.glove && (!desired || state.glove->handle != handle || state.glove->entity != pawn || state.glove->selection != *desired)) restore_glove(state);
    if (!desired) return;
    const auto item = pawn + state.fields.gloves;
    if (!state.glove) {
        state.glove.emplace(ChangedWeapon{pawn, handle, *desired, {}});
        auto& saved = *state.glove;
        patch(saved, item + state.fields.definition, static_cast<std::uint16_t>(desired->definition));
        patch(saved, item + state.fields.id_high, std::uint32_t{0xffffffff});
        patch(saved, item + state.fields.quality, 3);
        patch(saved, item + state.fields.initialized, true);
        saved.finish_override = true;
        state.glove_refresh_until = GetTickCount64() + 2500;
        state.glove_next_refresh = 0;
        state.glove_log_at = GetTickCount64() + 3000;
        state.glove_reads = 0;
        char message[160]{};
        std::snprintf(message,sizeof(message),"Glove request: team=%d definition=%d paint=%d wear=%.3f seed=%d.",team,desired->definition,desired->paint,desired->wear,desired->seed);
        overlay_log(message);
        publish_finishes(state);
        if (!memory::write(pawn + state.fields.glove_reapply, true)) throw std::runtime_error("Cannot refresh glove materials.");
    } else if (get<std::uint16_t>(item + state.fields.definition) != desired->definition ||
               !get<bool>(item + state.fields.initialized) ||
               get<std::uint32_t>(item + state.fields.id_high) != 0xffffffffu) {
        // Network updates can repopulate the inline view; retain the original
        // snapshot and reapply only our fields, never its attribute allocations.
        if (!memory::write(item + state.fields.definition, static_cast<std::uint16_t>(desired->definition)) ||
            !memory::write(item + state.fields.id_high, std::uint32_t{0xffffffff}) ||
            !memory::write(item + state.fields.initialized, true) ||
            !memory::write(item + state.fields.quality, 3) ||
            !memory::write(pawn + state.fields.glove_reapply, true)) throw std::runtime_error("Cannot reapply glove fields.");
    }
    const auto now=GetTickCount64();
    if (now < state.glove_refresh_until && now >= state.glove_next_refresh) {
        state.glove_next_refresh=now+250;
        if (!memory::write(pawn + state.fields.glove_reapply, true)) throw std::runtime_error("Cannot refresh gloves after loading.");
    }
}
// This component reads the network-owned inline item immediately before
// constructing the glove model/materials. Stage 6 alone is too late on frames
// where network item replication and this component run before that stage.
ThreadValue custom_glove_pawn;
bool __fastcall default_hand_hook(void* pawn) {
    auto& state=runtime();
    // The native default-agent path retires the separate glove entity on the
    // frame after its reapply flag clears. Only suppress that specific branch
    // while our selected glove is being updated; all other tag queries forward.
    if(custom_glove_pawn==reinterpret_cast<Address>(pawn) && custom_glove_pawn &&
       reinterpret_cast<Address>(_ReturnAddress())==state.client+bindings::glove_default_branch_return)
        return false;
    return state.original_default_hand(pawn);
}
void __fastcall glove_update_hook(void* self) {
    auto& state = runtime();
    Address selected_pawn{};
    try {
        std::lock_guard lock(state.mutex);
        if (state.report.available && !state.stopping) {
            const auto controller = get<Address>(state.client + bindings::local_controller);
            const auto handle = controller ? get<std::uint32_t>(controller + state.fields.pawn) : 0xffffffffu;
            const auto pawn = entity(state, handle);
            const auto component_pawn = get<Address>(reinterpret_cast<Address>(self) + bindings::glove_component_pawn);
            if (pawn && pawn == component_pawn) {
                update_glove(state, pawn, handle, get<int>(pawn + state.fields.health) > 0,
                             get<std::uint8_t>(pawn + state.fields.team));
                if(state.glove && state.glove->entity==pawn)selected_pawn=pawn;
            }
        }
    } catch (const std::exception& error) { overlay_log(error.what()); }
    struct Scope {
        Address previous;
        explicit Scope(Address pawn):previous(custom_glove_pawn){custom_glove_pawn=pawn;}
        ~Scope(){custom_glove_pawn=previous;}
    } scope(selected_pawn);
    state.original_glove_update(self);
    try {
        std::lock_guard lock(state.mutex);
        if(state.glove && state.glove_log_at && GetTickCount64()>=state.glove_log_at &&
           get<Address>(reinterpret_cast<Address>(self)+bindings::glove_component_pawn)==state.glove->entity) {
            state.glove_log_at=0;
            const auto glove_entity=entity(state,get<std::uint32_t>(reinterpret_cast<Address>(self)));
            std::string model="not created";
            if(glove_entity) {
                const auto scene=get<Address>(glove_entity+state.fields.scene);
                if(scene) { const auto name=get<Address>(scene+state.fields.model+state.fields.model_name);if(name)model=string_at(name); }
            }
            char message[512]{};
            std::snprintf(message,sizeof(message),"Glove renderer: paint reads=%u model=%s.",state.glove_reads.load(),model.c_str());
            overlay_log(message);
        }
    } catch (...) { overlay_log("Cannot inspect glove renderer state."); }
}

}
