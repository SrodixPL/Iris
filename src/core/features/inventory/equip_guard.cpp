#include "core/features/runtime.hpp"
#include "core/sdk/binding_values.hpp"

namespace iris::cosmetics::detail {
bool __fastcall inventory_equip_hook(void* manager,int team,int slot,std::uint64_t id) {
    auto& state=runtime();
    try {
        std::lock_guard lock(state.mutex);
        if(reinterpret_cast<Address>(manager)==state.client+bindings::inventory_manager && state.inventory_items) {
            const bool valid=team>=0 && team<static_cast<int>(iris::sdk::binding_values::layout_econ_team_count) &&
                slot>=0 && slot<static_cast<int>(iris::sdk::binding_values::layout_econ_slot_count);
            if(state.inventory_items->guard_native_equip(id)) {
                if(!valid)return false;
                return state.inventory_items->currently_equipped(
                    {static_cast<std::uint16_t>(team),static_cast<std::uint16_t>(slot)},id);
            }
            if(valid)state.inventory_items->prepare_genuine_equip(
                {static_cast<std::uint16_t>(team),static_cast<std::uint16_t>(slot)});
        }
    } catch(const std::exception& error) {
        // Uncertain ownership must not fall through to a GC-producing call.
        overlay_log(error.what());return false;
    }
    // Native equip can call unrelated subsystems. Forward outside our lock.
    return state.original_inventory_equip(manager,team,slot,id);
}
}
