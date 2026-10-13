#include "core/features/runtime.hpp"
#include "core/features/inventory/weapon_mesh.hpp"

namespace iris::cosmetics::detail {
namespace {
void inventory_fields(Configuration& to,const Configuration& from) {
    to.enabled=from.enabled;to.loadouts=from.loadouts;to.gloves=from.gloves;
    to.agents=from.agents;to.custom_models=from.custom_models;to.music_kit=from.music_kit;
}
void acknowledge(Runtime& state,InventoryAcceptance result,const std::string& detail={}) {
    // Periodic recovery of the previous selections must not turn a rejected
    // request into acceptance of that request's different selection values.
    if(result==InventoryAcceptance::Accepted && state.inventory_receipt.revision==state.requested.revision &&
        state.inventory_receipt.acceptance==InventoryAcceptance::Rejected)return;
    state.inventory_receipt={state.requested.revision,result,detail};
    state.inventory_ready.notify_all();
}
}
void restore_inventory_views(Runtime& state) {
    // Restore the old renderer's entity fields before a loadout change or item
    // retirement. Native stock views keep ownership of their private copies;
    // no SOCache pointer is installed in an entity or menu texture callback.
    restore_hud_weapon_meshes(state);
    restore_glove(state);
    for(auto it=state.changed.begin();it!=state.changed.end();) {
        restore(state,it->second);it=state.changed.erase(it);
    }
    publish_finishes(state);
    state.hud_knife.store(nullptr);
}
void restore_owned_inventory(Runtime& state) {
    if(!state.inventory_items)return;
    state.inventory_items->restore([&]{restore_inventory_views(state);});
}
void update_owned_inventory(Runtime& state) {
    const auto now=GetTickCount64();
    if(state.stopping) {
        if(now<state.inventory_poll)return;
        state.inventory_poll=now+10000;
        restore_owned_inventory(state);
        acknowledge(state,InventoryAcceptance::Rejected,"Inventory updates stopped; original loadout restored.");
        return;
    }
    if(!state.inventory_fault.empty()) {
        if(state.inventory_receipt.acceptance==InventoryAcceptance::Pending)
            acknowledge(state,InventoryAcceptance::Rejected,state.inventory_fault);
        return;
    }
    const bool new_revision=state.inventory_revision!=state.requested.revision;
    if(!new_revision && now<state.inventory_poll)return;
    state.inventory_poll=now+1000;
    try {
        if(!state.inventory_backend) {
            state.inventory_backend=std::make_unique<inventory::NativeBackend>(state.client);
            state.inventory_items=std::make_unique<inventory::OwnedItems>(*state.inventory_backend);
        }
        // Flexible firearms need the actual roster; empty and special-slot-only
        // selections can be reconciled while a map/pawn is still unavailable.
        bool needs_roster=false;
        if(state.requested.enabled)for(const auto& team:state.requested.loadouts)
            for(const auto& selected:team)
                needs_roster|=selected.enabled && !is_knife(selected.definition) && selected.definition!=31;
        if(needs_roster)read_roster(state);
        const auto desired=inventory::requests(state.requested,state.roster);
        state.inventory_items->reconcile(desired,[&]{restore_inventory_views(state);});
        state.inventory_accepted=state.requested;
        state.inventory_revision=state.requested.revision;
        acknowledge(state,InventoryAcceptance::Accepted);
        if(new_revision) {
            char message[160]{};
            std::snprintf(message,sizeof(message),"Owned inventory accepted revision %llu: %zu Iris items; loadout readback verified.",
                state.requested.revision,state.inventory_items->owned_count());
            overlay_log(message);
        }
    } catch(const std::exception& error) {
        const std::string failure=error.what();
        // Reconciliation performs precommit rollback itself. Cleanup can fail
        // after commit; recover to genuine baselines before retrying selections.
        try {restore_owned_inventory(state);}
        catch(const std::exception& recovery) {
            state.inventory_fault=failure+" Restoration pending: "+recovery.what();
            acknowledge(state,InventoryAcceptance::Rejected,state.inventory_fault);
            overlay_log(state.inventory_receipt.detail.c_str());
            state.inventory_poll=now+10000;
            return;
        }
        inventory_fields(state.requested,state.inventory_accepted);
        state.inventory_revision=state.requested.revision;
        acknowledge(state,InventoryAcceptance::Rejected,failure);
        overlay_log(failure.c_str());
    }
}
}
