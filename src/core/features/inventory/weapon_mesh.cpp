#include "core/features/inventory/weapon_mesh.hpp"
#include "core/features/runtime.hpp"
#include "core/sdk/binding_values.hpp"
#include "core/sdk/entities.hpp"

namespace iris::cosmetics::detail {
std::uint64_t weapon_mesh_mask(const ChangedWeapon& weapon) {
    const auto* finish = find_finish(weapon.selection.definition, weapon.selection.paint);
    return finish && finish->legacy ? 2u : 1u;
}

void synchronize_weapon_mesh(Runtime& state, ChangedWeapon& weapon) {
    if (entity(state, weapon.handle) != weapon.entity) return;
    const auto node = get<Address>(weapon.entity + state.fields.scene);
    if (!node) return;
    std::erase_if(weapon.fields, [node](const SavedField& field) { return field.scene && field.scene != node; });
    const auto address = node + state.fields.model + state.fields.mesh;
    const auto mask = weapon_mesh_mask(weapon);
    const auto saved = std::find_if(weapon.fields.begin(), weapon.fields.end(),
        [address, node](const SavedField& field) { return field.address == address && field.scene == node; });
    if (saved == weapon.fields.end()) {
        SavedField original{address, std::vector<std::byte>(sizeof(mask)), node};
        if (node == weapon.scene) std::memcpy(original.bytes.data(), &weapon.original_mesh, sizeof(mask));
        else if (!memory::read(address, original.bytes)) throw std::runtime_error("Cannot snapshot cosmetic model mesh.");
        weapon.fields.push_back(std::move(original));
    }
    // Prewriting the field would suppress the native renderer notification.
    state.set_mesh_group_mask(reinterpret_cast<void*>(node), mask);
}

namespace {
bool current_hud_model(Runtime& state, const ChangedHudWeaponMesh& saved) {
    return entity(state, saved.handle) == saved.entity &&
        get<Address>(saved.entity) == state.client + sdk::binding_values::client_hud_weapon_vtable;
}
void restore_hud_model(Runtime& state, const ChangedHudWeaponMesh& saved) {
    if (current_hud_model(state, saved) && get<Address>(saved.entity + state.fields.scene) == saved.scene)
        state.set_mesh_group_mask(reinterpret_cast<void*>(saved.scene), saved.original_mesh);
}
const ChangedWeapon* selected_owner(Runtime& state, Address hud_model) {
    const auto owner = get<std::uint32_t>(hud_model + state.fields.owner);
    const auto found = state.changed.find(owner);
    if (state.stopping || !state.requested.enabled || found == state.changed.end() ||
        !found->second.finish_override || found->second.menu_weapon ||
        entity(state, owner) != found->second.entity) return nullptr;
    return &found->second;
}
}

void restore_hud_weapon_meshes(Runtime& state) {
    // Erase only after the native restore succeeds so recovery can retry.
    for (auto it = state.hud_weapon_meshes.begin(); it != state.hud_weapon_meshes.end();) {
        restore_hud_model(state, it->second);
        it = state.hud_weapon_meshes.erase(it);
    }
    state.hud_weapon_mesh_tick = 0;
    state.hud_material_refresh_pending = false;
}

bool synchronize_hud_weapon_meshes(Runtime& state) {
    bool materials_changed = false;
    for (auto it = state.hud_weapon_meshes.begin(); it != state.hud_weapon_meshes.end();) {
        auto& saved = it->second;
        const auto* owner = current_hud_model(state, saved) ? selected_owner(state, saved.entity) : nullptr;
        if (!owner || owner->handle != saved.owner_handle || owner->entity != saved.owner_entity) {
            restore_hud_model(state, saved);
            it = state.hud_weapon_meshes.erase(it);
            materials_changed = true;
            continue;
        }
        const auto scene = get<Address>(saved.entity + state.fields.scene);
        if (scene != saved.scene) {
            // Retired scenes must never be restored or used for a new snapshot.
            it = state.hud_weapon_meshes.erase(it);
            state.hud_weapon_mesh_tick = 0;
            materials_changed = true;
            continue;
        }
        const auto mask = weapon_mesh_mask(*owner);
        // A native reset needs renderer propagation, but must not schedule a
        // material rebuild every frame. Rebuild for selection/lifetime changes.
        materials_changed |= saved.applied_mask != mask;
        state.set_mesh_group_mask(reinterpret_cast<void*>(scene), mask);
        saved.applied_mask = mask;
        ++it;
    }
    if (state.stopping || !state.requested.enabled || state.changed.empty()) {
        state.hud_weapon_mesh_tick = 0;
        return materials_changed;
    }
    const auto now = GetTickCount64();
    if (now < state.hud_weapon_mesh_tick) return materials_changed;
    state.hud_weapon_mesh_tick = now + 250;
    const auto chunks = get<Address>(state.client + bindings::entity_chunks);
    if (!chunks) return materials_changed;
    // HUD models occupy the client-only entity range, including indices above
    // 0x4000. Discover them in bounded blocks; enforce known models each frame.
    for (unsigned group = 0; group < 64; ++group) {
        const auto chunk = memory::read<Address>(chunks + group * sizeof(Address)).value_or(0);
        if (!chunk) continue;
        std::array<EntityIdentity, 512> identities{};
        if (!memory::read(chunk, std::as_writable_bytes(std::span{identities}))) continue;
        for (unsigned index = 0; index < identities.size(); ++index) {
            const auto& identity = identities[index];
            if (!identity.entity || (identity.handle & 0x7fff) != group * 512 + index ||
                state.hud_weapon_meshes.contains(identity.handle) ||
                memory::read<Address>(identity.entity).value_or(0) != state.client + sdk::binding_values::client_hud_weapon_vtable ||
                entity(state, identity.handle) != identity.entity) continue;
            const auto* owner = selected_owner(state, identity.entity);
            if (!owner) continue;
            const auto scene = get<Address>(identity.entity + state.fields.scene);
            if (!scene) continue;
            const auto original = get<std::uint64_t>(scene + state.fields.model + state.fields.mesh);
            const auto mask = weapon_mesh_mask(*owner);
            state.hud_weapon_meshes.emplace(identity.handle, ChangedHudWeaponMesh{
                identity.entity, scene, owner->entity, identity.handle, owner->handle, original, mask});
            state.set_mesh_group_mask(reinterpret_cast<void*>(scene), mask);
            materials_changed = true;
        }
    }
    return materials_changed;
}

void refresh_weapon_materials(Runtime& state, bool refreshed, Address pawn) {
    for (auto& [handle, weapon] : state.changed) {
        (void)handle;
        synchronize_weapon_mesh(state, weapon);
    }
    // HUD models can arrive after purchase with an unchanged selection. Their
    // geometry must be selected before regenerating either material owner.
    state.hud_material_refresh_pending |= synchronize_hud_weapon_meshes(state);
    refreshed |= state.hud_material_refresh_pending;
    if (refreshed && pawn) {
        state.regenerate();
        state.hud_material_refresh_pending = false;
        for (auto& [handle, weapon] : state.changed) {
            (void)handle;
            synchronize_weapon_mesh(state, weapon);
        }
    }
    // A scene created by regeneration gets one subsequent update rather than
    // recursive regeneration. Native mask resets alone do not trigger rebuilds.
    state.hud_material_refresh_pending |= synchronize_hud_weapon_meshes(state);
}
}
