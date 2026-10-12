#include "core/features/inventory/owned_items.hpp"
#include "core/sdk/binding_values.hpp"
#include <algorithm>
#include <array>
#include <exception>
#include <stdexcept>

namespace iris::cosmetics::inventory {
namespace {
constexpr std::size_t slots_per_team=58, teams=4, max_items=64;
std::size_t index(native::Slot slot) {
    if (slot.team>=teams || slot.slot>=slots_per_team) throw std::runtime_error("Invalid native loadout route.");
    return slot.team*slots_per_team+slot.slot;
}
bool same(const native::ItemSpec& a,const native::ItemSpec& b) {
    return a.definition==b.definition && a.packed_metadata==b.packed_metadata && a.name==b.name && a.attributes==b.attributes;
}
}
void OwnedItems::retire(Item& item) {
    if (!item.address) return;
    // A destructor can fault after releasing the allocation. Never probe or
    // free that address again; retain the unresolved ledger until host exit.
    if (item.phase==Phase::Destroying)
        throw std::runtime_error("Economy destruction faulted with unknown allocation ownership; restart the host.");
    if (!context_ || !backend_.current(*context_)) throw std::runtime_error("Inventory context changed during item recovery.");
    auto present=backend_.items(*context_);
    if (std::any_of(present.begin(),present.end(),[&](const auto& value){return value.id==item.id && value.object!=item.address;}))
        throw std::runtime_error("Owned economy identity collides with another native object; retirement is suspended.");
    const bool member=std::any_of(present.begin(),present.end(),[&](const auto& value){return value.object==item.address && value.id==item.id && value.account==context_->account;});
    if (item.phase==Phase::Inserted || item.phase==Phase::Published) {
        if (!member) { item.address=0;return; } // Native refresh already retired it.
        const auto slots=backend_.loadout(*context_);
        if (std::any_of(slots.begin(),slots.end(),[&](const auto& value){return value.id==item.id;}))
            throw std::runtime_error("Cannot retire an Iris item still referenced by a loadout slot.");
        item.phase=Phase::Detaching;
    } else if (item.phase==Phase::Allocated && member) {
        // The native insertion may have completed before a fault was reported.
        item.phase=Phase::Inserted;retire(item);return;
    }
    if (item.phase==Phase::Detaching) {
        // Detachment can complete before the adapter reports a fault. Unlike
        // native retirement of a Published item, this recorded transition
        // retains ownership of the detached allocation for recovery.
        if (member) {
            const auto detached=backend_.detach(*context_,item.address);
            if (detached!=item.address) throw std::runtime_error("Native cache detached a different economy item.");
        }
        item.phase=Phase::Detached;
    }
    if (item.phase==Phase::Detached) {
        if (backend_.view(*context_,item.id)) backend_.destroyed(*context_,item.address);
        if (backend_.view(*context_,item.id)) throw std::runtime_error("Native item view has not retired.");
        item.phase=Phase::ViewRetired;
    }
    item.phase=Phase::Destroying;
    backend_.destroy(*context_,item.address);
    item.address=0;
}
void OwnedItems::cleanup_unused() {
    for (auto& item:items_) {
        if (!item.address) continue;
        const auto slots=backend_.loadout(*context_);
        const bool referenced=std::any_of(slots.begin(),slots.end(),[&](const auto& value){return value.id==item.id;});
        if (!referenced) retire(item);
    }
    std::erase_if(items_,[](const auto& item){return !item.address;});
}
void OwnedItems::reconcile(const std::vector<Request>& desired,const std::function<void()>& before_equip) {
    if (desired.size()>max_items) throw std::runtime_error("Inventory selection exceeds its owned-item bound.");
    for (std::size_t i=0;i<desired.size();++i) {
        index(desired[i].slot);
        if (!desired[i].item.definition || desired[i].item.id || desired[i].item.position)
            throw std::runtime_error("Inventory requests must use a definition and unassigned identity.");
        for (std::size_t j=0;j<i;++j) if (desired[i].slot==desired[j].slot)
            throw std::runtime_error("Two selections address the same native loadout slot.");
    }
    if (context_ && !backend_.current(*context_)) {
        // Inserted items belong to the retired native cache. An interrupted,
        // detached allocation still needs recovery and cannot be discarded.
        if (std::any_of(items_.begin(),items_.end(),[](const auto& item){return item.address && item.phase!=Phase::Published && item.phase!=Phase::Inserted;}))
            throw std::runtime_error("Inventory changed with detached cleanup pending; restart the host.");
        items_.clear();bindings_.clear();originals_.clear();context_.reset();
    }
    if (!context_) {
        if (desired.empty()) return;
        context_=backend_.inspect();originals_=backend_.loadout(*context_);
        if (originals_.size()!=teams*slots_per_team) throw std::runtime_error("Native loadout snapshot has an unexpected size.");
    }
    const auto live=backend_.items(*context_);
    std::vector<std::uint64_t> known_owned_ids;
    for (const auto& item:items_) known_owned_ids.push_back(item.id);
    for (const auto& binding:bindings_)
        if (std::find(known_owned_ids.begin(),known_owned_ids.end(),binding.id)==known_owned_ids.end()) known_owned_ids.push_back(binding.id);
    for (auto& item:items_) if (item.phase==Phase::Published) {
        if (std::count_if(live.begin(),live.end(),[&](const auto& value){return value.id==item.id;})>1)
            throw std::runtime_error("An Iris item ID collides with native inventory data; mutation is suspended.");
        if (std::none_of(live.begin(),live.end(),[&](const auto& value){return value.object==item.address && value.id==item.id && value.account==context_->account;})) item.address=0;
    }
    std::erase_if(items_,[](const auto& item){return !item.address;});
    // A native refresh may replace an old Iris ID with a different genuine
    // object. ID equality alone must not hide that genuine restoration baseline.
    std::erase_if(known_owned_ids,[&](std::uint64_t id){
        const auto entry=std::find_if(live.begin(),live.end(),[&](const auto& value){return value.id==id;});
        return entry!=live.end() && std::none_of(items_.begin(),items_.end(),[&](const auto& item){
            return item.id==id && item.address==entry->object && entry->account==context_->account;
        });
    });
    const auto snapshot=backend_.loadout(*context_);
    if (snapshot.size()!=originals_.size()) throw std::runtime_error("Native loadout snapshot changed shape.");
    // Legitimate intervening equips become the restoration baseline. Never
    // classify ownership from the synthetic ID policy range alone.
    for (std::size_t i=0;i<snapshot.size();++i)
        if (std::find(known_owned_ids.begin(),known_owned_ids.end(),snapshot[i].id)==known_owned_ids.end()) originals_[i]=snapshot[i];
    const auto old_size=items_.size();
    if (old_size+desired.size()>2*max_items) throw std::runtime_error("Inventory recovery ledger is full.");
    items_.reserve(old_size+desired.size());
    std::vector<Binding> next;next.reserve(desired.size());
    auto target=snapshot;
    auto rollback=snapshot;
    for (std::size_t i=0;i<snapshot.size();++i)
        if (std::find(known_owned_ids.begin(),known_owned_ids.end(),snapshot[i].id)!=known_owned_ids.end()) {
            target[i]=originals_[i];
            if (std::none_of(items_.begin(),items_.end(),[&](const auto& item){return item.id==snapshot[i].id;})) rollback[i]=originals_[i];
        }
    std::vector<std::size_t> written;
    try {
        for (const auto& request:desired) {
            auto found=std::find_if(items_.begin(),items_.end(),[&](const auto& item){return item.phase==Phase::Published && same(item.signature,request.item);});
            if (found==items_.end()) {
                native::ItemSpec spec=request.item;
                const auto all=backend_.items(*context_);
                do {
                    if (++next_id_>=0xf000000000000000ull) throw std::runtime_error("Iris item identity policy is exhausted.");
                } while (std::any_of(all.begin(),all.end(),[&](const auto& value){return value.id==next_id_;}));
                constexpr auto position_mask=iris::sdk::binding_values::layout_econ_position_index_mask;
                constexpr auto unacknowledged_mask=iris::sdk::binding_values::layout_econ_position_unacknowledged_mask;
                std::array<bool,position_mask+1> occupied{};
                for (const auto& value:all)
                    if(value.position && !(value.position & unacknowledged_mask))
                        occupied[value.position & position_mask]=true;
                std::uint32_t position=1;
                while(position<=position_mask && occupied[position])++position;
                if(position>position_mask)throw std::runtime_error("Inventory position is exhausted.");
                items_.push_back({0,next_id_,request.item,Phase::Allocated});
                found=std::prev(items_.end());
                found->address=backend_.allocate(*context_);
                if (!found->address) throw std::runtime_error("Economy item allocation failed.");
                spec.id=found->id;spec.position=position;
                backend_.initialize(*context_,found->address,spec);
                if (!backend_.insert(*context_,found->address)) throw std::runtime_error("Native inventory rejected the replacement item.");
                found->phase=Phase::Inserted;
                backend_.created(*context_,found->address);
                if (!backend_.view(*context_,found->id)) throw std::runtime_error("Native inventory did not publish the replacement item view.");
                found->phase=Phase::Published;
            }
            target[index(request.slot)]={found->id,request.item.definition};
            next.push_back({request.slot,found->id});
        }
        const bool retires_item=std::any_of(items_.begin(),items_.end(),[&](const auto& item){
            return item.address && std::none_of(target.begin(),target.end(),[&](const auto& slot){return slot.id==item.id;});
        });
        if (before_equip && (target!=snapshot || retires_item)) before_equip();
        for (std::size_t i=0;i<target.size();++i) if (target[i]!=snapshot[i]) {
            // Record before invoking native code, which may fault after writing.
            written.push_back(i);
            const native::Slot slot{static_cast<std::uint16_t>(i/slots_per_team),static_cast<std::uint16_t>(i%slots_per_team)};
            backend_.equip(*context_,slot,target[i]);
            if (backend_.equipped(*context_,slot)!=target[i]) throw std::runtime_error("Inventory equip readback failed.");
        }
        if (backend_.loadout(*context_)!=target) throw std::runtime_error("Inventory transaction readback changed before acceptance.");
    } catch (...) {
        const auto failure=std::current_exception();
        bool recovered=true;
        if (backend_.current(*context_)) {
            for (std::size_t i=0;i<snapshot.size();++i)
                if (rollback[i]!=snapshot[i] && std::find(written.begin(),written.end(),i)==written.end()) written.push_back(i);
            for (auto it=written.rbegin();it!=written.rend();++it) try {
                const native::Slot slot{static_cast<std::uint16_t>(*it/slots_per_team),static_cast<std::uint16_t>(*it%slots_per_team)};
                backend_.equip(*context_,slot,rollback[*it]);
                if (backend_.equipped(*context_,slot)!=rollback[*it]) recovered=false;
            } catch (...) { recovered=false; }
            for (std::size_t i=old_size;i<items_.size();++i) try {retire(items_[i]);} catch (...) {recovered=false;}
            std::erase_if(items_,[](const auto& item){return !item.address;});
        } else recovered=false;
        if (!recovered) throw std::runtime_error("Inventory replacement failed and ownership recovery is incomplete; restart the host.");
        std::rethrow_exception(failure);
    }
    bindings_=std::move(next);
    // Beyond this point the new slots are committed. Cleanup faults retain the
    // ledger and must not claim a successful complete replacement or restore
    // already-retired old IDs. The coordinator restores genuine baselines.
    cleanup_unused();
}
void OwnedItems::restore(const std::function<void()>& before_equip) { reconcile({},before_equip); }
std::uint64_t OwnedItems::item_for(native::Slot slot) const {
    for (const auto& binding:bindings_) if (binding.slot==slot)
        for (const auto& item:items_)
            if (item.address && item.phase==Phase::Published && item.id==binding.id) return binding.id;
    return 0;
}
bool OwnedItems::guard_native_equip(std::uint64_t id) {
    if(!context_ || std::none_of(items_.begin(),items_.end(),[&](const auto& item){return item.address&&item.id==id;}))return false;
    if(!backend_.current(*context_))return false;
    const auto live=backend_.items(*context_);
    const auto matching=std::count_if(live.begin(),live.end(),[&](const auto& item){return item.id==id;});
    if(matching>1)return true; // Ambiguous key: suspend this native request.
    for(const auto& value:live)if(value.id==id)
        return std::any_of(items_.begin(),items_.end(),[&](const auto& item){
            return item.address==value.object && item.id==id && value.account==context_->account;
        });
    // An interrupted detach retains the Iris ledger while its view retires.
    // Native refresh can also leave a stale UI request; neither may enter GC.
    return true;
}
bool OwnedItems::currently_equipped(native::Slot slot,std::uint64_t id) {
    return context_ && backend_.current(*context_) && item_for(slot)==id && backend_.equipped(*context_,slot).id==id;
}
void OwnedItems::prepare_genuine_equip(native::Slot slot) {
    const auto at=index(slot);
    if(!context_ || !backend_.current(*context_))return;
    const auto equipped=backend_.equipped(*context_,slot);
    const bool ours=std::any_of(items_.begin(),items_.end(),[&](const auto& item){return item.address&&item.id==equipped.id;}) ||
        std::any_of(bindings_.begin(),bindings_.end(),[&](const auto& binding){return binding.id==equipped.id;});
    if(!ours || !guard_native_equip(equipped.id))return;
    // Keep the owned allocation alive until ordinary reconciliation retires it.
    // The native genuine equip then sees genuine old/new IDs in its GC queue.
    backend_.equip(*context_,slot,originals_.at(at));
    if(backend_.equipped(*context_,slot)!=originals_[at])throw std::runtime_error("Cannot restore a genuine baseline before native equip.");
}
}
