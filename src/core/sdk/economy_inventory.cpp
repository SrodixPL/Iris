#include "core/sdk/economy_inventory.hpp"
#include "core/sdk/binding_values.hpp"
#include "core/sdk/client.hpp"
#include "core/sdk/offsets/build.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace iris::sdk::economy {
namespace {
namespace b = binding_values;
enum class Operation { LocalInventory, TypeCache, ItemView, LoadoutView, Allocate,
    Attribute, Name, Insert, Created, Detach, Destroyed, Destroy, Equip };
struct Call {
    Operation operation{};
    Address client{}, object{}, argument{};
    const void* owner{};
    std::uint64_t id{};
    std::uint32_t bits{};
    int index{}, team{}, slot{}, definition{};
    Address result{};
    bool accepted{};
};
// Keep the SEH boundary free of C++ ownership. The caller retains its ledger
// across faults, including native operations that completed only partly.
bool invoke(Call* call) {
    __try {
        void* object = reinterpret_cast<void*>(call->object);
        switch (call->operation) {
        case Operation::LocalInventory:
            call->result = reinterpret_cast<Address(__fastcall*)(void*)>(call->client+b::client_local_inventory)(object); break;
        case Operation::TypeCache:
            call->result = reinterpret_cast<Address(__fastcall*)(void*,int)>(call->client+b::client_inventory_type_cache)(object,1); break;
        case Operation::ItemView:
            call->result = reinterpret_cast<Address(__fastcall*)(void*,std::uint64_t)>(call->client+b::client_inventory_item_view)(object,call->id); break;
        case Operation::LoadoutView:
            call->result = reinterpret_cast<Address(__fastcall*)(void*,int,int)>(call->client+b::client_loadout_item)(object,call->team,call->slot); break;
        case Operation::Allocate:
            call->result = reinterpret_cast<Address(__fastcall*)()>(call->client+b::client_econ_allocate)(); break;
        case Operation::Attribute: {
            const auto schema = reinterpret_cast<void*(__fastcall*)()>(call->client+b::client_item_schema)();
            const auto definition = reinterpret_cast<Address(__fastcall*)(void*,int)>(call->client+b::client_attribute_definition)(schema,call->index);
            if (!definition) return false;
            const auto type = *reinterpret_cast<const Address*>(definition+b::layout_attribute_type);
            if (!type || *reinterpret_cast<const Address*>(type)!=call->client+b::client_econ_default_attribute_vtable) return false;
            reinterpret_cast<void(__fastcall*)(void*,void*,const std::uint32_t*)>(call->client+b::client_econ_set_attribute)(object,reinterpret_cast<void*>(definition),&call->bits);
            break;
        }
        case Operation::Name:
            reinterpret_cast<void(__fastcall*)(void*,const char*,int)>(call->client+b::client_econ_set_name)(object,reinterpret_cast<const char*>(call->argument),-1); break;
        case Operation::Insert:
            call->accepted = reinterpret_cast<bool(__fastcall*)(void*,void*)>(call->client+b::client_inventory_cache_add)(object,reinterpret_cast<void*>(call->argument)); break;
        case Operation::Created:
            reinterpret_cast<void(__fastcall*)(void*,const void*,void*,int)>(call->client+b::client_inventory_created)(object,call->owner,reinterpret_cast<void*>(call->argument),static_cast<int>(b::layout_econ_notification_event)); break;
        case Operation::Detach:
            call->result = reinterpret_cast<Address(__fastcall*)(void*,void*)>(call->client+b::client_inventory_cache_remove)(object,reinterpret_cast<void*>(call->argument)); break;
        case Operation::Destroyed:
            reinterpret_cast<void(__fastcall*)(void*,const void*,void*,int)>(call->client+b::client_inventory_destroyed)(object,call->owner,reinterpret_cast<void*>(call->argument),static_cast<int>(b::layout_econ_notification_event)); break;
        case Operation::Destroy:
            reinterpret_cast<void*(__fastcall*)(void*,unsigned)>(call->client+b::client_econ_destroy)(object,1); break;
        case Operation::Equip:
            reinterpret_cast<void(__fastcall*)(void*,std::uint16_t,std::uint16_t,std::uint64_t,std::uint16_t,bool)>(call->client+b::client_inventory_local_equip)(object,static_cast<std::uint16_t>(call->team),static_cast<std::uint16_t>(call->slot),call->id,static_cast<std::uint16_t>(call->definition),true); break;
        }
        return true;
    } __except (GetExceptionCode()==0xe06d7363 ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void run(Call& call) {
    if (!invoke(&call)) throw std::runtime_error("Native inventory operation failed; ownership recovery is required.");
}
void require_slot(Slot slot) {
    if (slot.team>=b::layout_econ_team_count || slot.slot>=b::layout_econ_slot_count)
        throw std::runtime_error("Inventory slot is outside the reviewed table.");
}
void require_context(const Context& context) {
    if (!current(context)) throw std::runtime_error("Inventory identity changed; borrowed items are unavailable.");
}
void require_item(const Context& context, Address object) {
    if (!object || get<Address>(object)!=context.client+b::client_econ_vtable ||
        get<std::uint32_t>(object+b::layout_econ_account)!=context.account)
        throw std::runtime_error("Economy item identity differs from its owning inventory.");
}
template<class T> void write(Address address, const T& value) {
    if (!memory::write(address,value)) throw std::runtime_error("Cannot initialize the owned economy item.");
}
}

Context inspect(Address client) {
    if (client!=reinterpret_cast<Address>(GetModuleHandleW(L"client.dll")) ||
        sha256_file(reinterpret_cast<HMODULE>(client))!=reviewed_build::client_sha256)
        throw std::runtime_error("Inventory client differs from the reviewed build.");
    const auto manager=client+b::client_inventory_manager;
    if (get<Address>(manager)!=client+b::client_inventory_vtable ||
        method(manager,static_cast<int>(b::slots_inventory_local))!=client+b::client_local_inventory)
        throw std::runtime_error("Inventory manager binding changed.");
    Call local{Operation::LocalInventory,client,manager};run(local);
    Context context;context.client=client;context.inventory=local.result;
    if (!context.inventory) throw std::runtime_error("Waiting for the local inventory.");
    if (get<Address>(context.inventory)!=client+b::client_player_inventory_vtable ||
        method(context.inventory,0)!=client+b::client_inventory_created ||
        method(context.inventory,2)!=client+b::client_inventory_destroyed ||
        method(context.inventory,static_cast<int>(b::slots_inventory_loadout))!=client+b::client_loadout_item)
        throw std::runtime_error("Inventory callback identity changed.");
    if (!memory::read(context.inventory+b::layout_econ_inventory_owner,std::span<std::byte>{context.owner}))
        throw std::runtime_error("Cannot copy the inventory owner identity.");
    std::memcpy(&context.account,context.owner.data(),sizeof(context.account));
    if (!context.account) throw std::runtime_error("The local inventory has no account identity.");
    context.shared_cache=get<Address>(context.inventory+b::layout_econ_inventory_cache);
    if (!context.shared_cache || get<Address>(context.shared_cache)!=client+b::client_inventory_shared_cache_vtable)
        throw std::runtime_error("Shared inventory cache identity changed.");
    Call cache{Operation::TypeCache,client,context.shared_cache};run(cache);context.type_cache=cache.result;
    if (!context.type_cache || get<Address>(context.type_cache)!=client+b::client_inventory_type_cache_vtable ||
        get<int>(context.type_cache+b::layout_econ_cache_type)!=1 ||
        method(context.type_cache,1)!=client+b::client_inventory_cache_add ||
        method(context.type_cache,3)!=client+b::client_inventory_cache_remove)
        throw std::runtime_error("Economy type cache identity changed.");
    return context;
}
bool current(const Context& context) {
    if (!context.client || !context.inventory || !context.type_cache) return false;
    const auto manager=context.client+b::client_inventory_manager;
    if (get<Address>(manager)!=context.client+b::client_inventory_vtable) return false;
    Call local{Operation::LocalInventory,context.client,manager};run(local);
    if (local.result!=context.inventory || get<Address>(context.inventory)!=context.client+b::client_player_inventory_vtable ||
        get<Address>(context.inventory+b::layout_econ_inventory_cache)!=context.shared_cache ||
        get<Address>(context.shared_cache)!=context.client+b::client_inventory_shared_cache_vtable) return false;
    std::array<std::byte,16> owner{};
    if (!memory::read(context.inventory+b::layout_econ_inventory_owner,std::span<std::byte>{owner}) || owner!=context.owner) return false;
    Call cache{Operation::TypeCache,context.client,context.shared_cache};run(cache);
    return cache.result==context.type_cache && get<Address>(context.type_cache)==context.client+b::client_inventory_type_cache_vtable;
}
std::vector<ItemIdentity> items(const Context& context) {
    require_context(context);
    const auto count=get<int>(context.type_cache+b::layout_econ_cache_count);
    const auto data=get<Address>(context.type_cache+b::layout_econ_cache_data);
    if (count<0 || count>4096 || (count && !data)) throw std::runtime_error("Economy item enumeration is outside its bounds.");
    std::vector<ItemIdentity> result;result.reserve(static_cast<std::size_t>(count));
    for (int index=0;index<count;++index) {
        const auto object=get<Address>(data+sizeof(Address)*index);require_item(context,object);
        result.push_back({object,get<std::uint64_t>(object+b::layout_econ_id),get<std::uint32_t>(object+b::layout_econ_account),
            get<std::uint32_t>(object+b::layout_econ_position),get<std::uint16_t>(object+b::layout_econ_definition)});
    }
    return result;
}
bool owns(const Context& context, Address object, std::uint64_t id) {
    // Membership is checked before dereferencing a possibly native-retired object.
    require_context(context);
    const auto count=get<int>(context.type_cache+b::layout_econ_cache_count);
    const auto data=get<Address>(context.type_cache+b::layout_econ_cache_data);
    if (count<0 || count>4096 || (count && !data)) throw std::runtime_error("Economy ownership enumeration is outside its bounds.");
    for (int index=0;index<count;++index) if (get<Address>(data+sizeof(Address)*index)==object) {
        require_item(context,object);return get<std::uint64_t>(object+b::layout_econ_id)==id;
    }
    return false;
}
EquippedItem equipped(const Context& context, Slot slot) {
    require_context(context);require_slot(slot);
    const auto entry=context.inventory+b::layout_econ_loadout+b::layout_econ_slot_stride*(slot.team*b::layout_econ_slot_count+slot.slot);
    return {get<std::uint64_t>(entry),get<std::uint16_t>(entry+sizeof(std::uint64_t))};
}
Address item_view(const Context& context, std::uint64_t id) {
    require_context(context);Call call{Operation::ItemView,context.client,context.inventory};call.id=id;run(call);return call.result;
}
std::vector<EquippedItem> loadout_snapshot(const Context& context) {
    require_context(context);
    const auto count=b::layout_econ_team_count*b::layout_econ_slot_count;
    std::vector<std::byte> bytes(count*b::layout_econ_slot_stride);
    if (!memory::read(context.inventory+b::layout_econ_loadout,std::span<std::byte>{bytes}))
        throw std::runtime_error("Cannot copy the local inventory loadout.");
    std::vector<EquippedItem> result;result.reserve(count);
    for (std::size_t index=0;index<count;++index) {
        EquippedItem item;
        std::memcpy(&item.id,bytes.data()+index*b::layout_econ_slot_stride,sizeof(item.id));
        std::memcpy(&item.definition,bytes.data()+index*b::layout_econ_slot_stride+sizeof(item.id),sizeof(item.definition));
        result.push_back(item);
    }
    return result;
}
Address loadout_view(const Context& context, Slot slot) {
    require_context(context);require_slot(slot);Call call{Operation::LoadoutView,context.client,context.inventory};call.team=slot.team;call.slot=slot.slot;run(call);return call.result;
}
Address allocate(Address client) {
    if (client!=reinterpret_cast<Address>(GetModuleHandleW(L"client.dll"))) throw std::runtime_error("Inventory client is unavailable.");
    Call call{Operation::Allocate,client};run(call);
    if (!call.result || get<Address>(call.result)!=client+b::client_econ_vtable) throw std::runtime_error("Economy factory did not return the reviewed item type.");
    return call.result;
}
void initialize(const Context& context, Address object, const ItemSpec& spec) {
    require_context(context);
    if (!object || get<Address>(object)!=context.client+b::client_econ_vtable || !spec.id ||
        spec.id>=b::layout_econ_builtin_id || !spec.definition || !spec.position ||
        spec.position>b::layout_econ_position_index_mask || spec.name.back()!=0 || spec.attributes.size()>16)
        throw std::runtime_error("Invalid owned economy item specification.");
    for (const auto& existing:items(context)) if (existing.id==spec.id || existing.object==object)
        throw std::runtime_error("Economy item identity collides with an existing cache item.");
    write(object+b::layout_econ_id,spec.id);write(object+b::layout_econ_account,context.account);
    write(object+b::layout_econ_position,spec.position);write(object+b::layout_econ_definition,spec.definition);
    write(object+b::layout_econ_packed,spec.packed_metadata);
    for (const auto& attribute:spec.attributes) {
        Call call{Operation::Attribute,context.client,object};call.index=attribute.definition;call.bits=attribute.bits;run(call);
    }
    if (!spec.attributes.empty()) {
        const auto block=get<Address>(object+b::layout_econ_attributes);
        if (!block) throw std::runtime_error("Native attribute storage was not created.");
        const auto count=get<std::uint8_t>(block+b::layout_econ_attribute_count);
        if (count<spec.attributes.size() || count>32) throw std::runtime_error("Native attribute storage has an unexpected count.");
        for (const auto& attribute:spec.attributes) {
            bool matched=false;
            for (unsigned index=0;index<count;++index) {
                const auto entry=block+b::layout_econ_attribute_data+index*b::layout_econ_attribute_stride;
                if (get<std::uint16_t>(entry)==attribute.definition) {
                    matched=get<std::uint32_t>(entry+b::layout_econ_attribute_payload)==attribute.bits;
                    break;
                }
            }
            if (!matched) throw std::runtime_error("Native attribute readback rejected the requested value.");
        }
    }
    if (spec.name[0]) {Call call{Operation::Name,context.client,object,reinterpret_cast<Address>(spec.name.data())};run(call);}
}
bool insert(const Context& context, Address object) {
    require_context(context);require_item(context,object);
    const auto id=get<std::uint64_t>(object+b::layout_econ_id);
    for (const auto& item:items(context)) if (item.id==id || item.object==object) return false;
    Call call{Operation::Insert,context.client,context.type_cache,object};run(call);return call.accepted;
}
void notify_created(const Context& context, Address object) {
    require_context(context);require_item(context,object);
    Call call{Operation::Created,context.client,context.inventory,object};call.owner=context.owner.data();run(call);
}
Address detach(const Context& context, Address object) {
    require_context(context);require_item(context,object);
    const auto id=get<std::uint64_t>(object+b::layout_econ_id);
    const auto all=items(context);
    if (std::count_if(all.begin(),all.end(),[&](const auto& item){return item.id==id;})!=1 ||
        std::none_of(all.begin(),all.end(),[&](const auto& item){return item.id==id && item.object==object;}))
        throw std::runtime_error("Economy removal identity is ambiguous; the item remains untouched.");
    Call call{Operation::Detach,context.client,context.type_cache,object};run(call);return call.result;
}
void notify_destroyed(const Context& context, Address object) {
    require_context(context);require_item(context,object);
    Call call{Operation::Destroyed,context.client,context.inventory,object};call.owner=context.owner.data();run(call);
}
void destroy(Address client, Address object) {
    if (!object || get<Address>(object)!=client+b::client_econ_vtable) throw std::runtime_error("Refusing to destroy an unknown economy object.");
    Call call{Operation::Destroy,client,object};run(call);
}
void equip_local(const Context& context, Slot slot, EquippedItem item) {
    require_context(context);require_slot(slot);
    if (item.id && item.id<b::layout_econ_builtin_id && !item_view(context,item.id))
        throw std::runtime_error("Refusing to equip an economy ID absent from the inventory.");
    Call call{Operation::Equip,context.client,context.inventory};call.team=slot.team;call.slot=slot.slot;call.id=item.id;call.definition=item.definition;run(call);
    if (equipped(context,slot)!=item) throw std::runtime_error("Native local loadout readback rejected the item.");
}
}
