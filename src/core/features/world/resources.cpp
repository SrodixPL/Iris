#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
#include "core/features/world/native.hpp"
#include "core/sdk/world_bindings.hpp"
#include <fstream>

namespace iris::cosmetics::detail {

int environment_offset(Runtime& state,const char* type,const char* field) {
    const auto key=std::string(type)+"::"+field;
    const auto found=state.environment_offsets.find(key);
    if(found!=state.environment_offsets.end())return found->second;
    int offset=-1;
    try {offset=schema_field(state.schema_scope,type,field);} catch(const std::exception& error){overlay_log(error.what());}
    state.environment_offsets.emplace(key,offset);return offset;
}
void assign_resource(Address* target,const Address* source) {
    using Assign=Address*(__fastcall*)(Address*,const Address*);
    reinterpret_cast<Assign>(runtime().client+bindings::resource_assign)(target,source);
}
Address load_resource(Runtime& state,const char* path,std::uint32_t type) {
    auto& held=state.resource_cache[path];
    if(!held){
        const auto system=get<Address>(state.client+bindings::resource_system);
        if(!system)return 0;
        alignas(8) std::array<std::byte,0xe0> name{};
        const std::uint32_t storage=0xc00000c8;std::memcpy(name.data()+4,&storage,4);
        using Name=bool(__fastcall*)(void*,const char*);
        using Destroy=void(__fastcall*)(void*,int);
        const auto destroy=reinterpret_cast<Destroy>(get<Address>(state.client+bindings::resource_name_destroy));
        const bool named=reinterpret_cast<Name>(state.client+bindings::resource_name)(name.data(),path);
        std::uint64_t resolved_type{};std::memcpy(&resolved_type,name.data()+iris::sdk::binding_values::layout_resource_resolved_type,sizeof(resolved_type));
        if(!named || resolved_type!=type){destroy(name.data(),0);return 0;}
        using State=int(__fastcall*)(void*,const void*);
        const auto loaded=reinterpret_cast<State>(method(system,static_cast<int>(iris::sdk::binding_values::slots_resource_state)))(reinterpret_cast<void*>(system),name.data());
        using Load=Address(__fastcall*)(void*,const void*,const char*);
        const auto binding=reinterpret_cast<Load>(method(system,loaded?static_cast<int>(iris::sdk::binding_values::slots_resource_existing):static_cast<int>(iris::sdk::binding_values::slots_resource_load)))(reinterpret_cast<void*>(system),name.data(),loaded?nullptr:"");
        destroy(name.data(),0);
        // On this ResourceSystem interface, virtual 7 is a version query
        // returning 2, not a binding FourCC. ResourceName supplies the type.
        // Retain the returned binding; its data may complete asynchronously.
        if(!binding)return 0;
        assign_resource(&held,&binding);
    }
    return get<Address>(held)?held:0;
}
void environment_resource(EnvironmentEntity& saved,int offset,Address value) {
    if(offset<0 || !value)return;
    const auto current=memory::read<Address>(saved.address+offset);if(!current)return;
    auto& field=saved.fields[offset];
    if(field.original.empty() || (field.written.size()==8 && std::memcmp(&*current,field.written.data(),8)!=0)){
        if(field.resource && field.original.size()==8){Address previous{};std::memcpy(&previous,field.original.data(),8);const Address empty{};assign_resource(&previous,&empty);}
        Address baseline{};assign_resource(&baseline,&*current);
        field.original.resize(8);std::memcpy(field.original.data(),&baseline,8);field.resource=true;
    }
    if(*current==value)return;
    assign_resource(reinterpret_cast<Address*>(saved.address+offset),&value);
    field.written.resize(8);std::memcpy(field.written.data(),&value,8);saved.dirty=true;
}
void restore_environment_field(EnvironmentEntity& saved,int offset,bool entity_valid) {
    auto found=saved.fields.find(offset);if(found==saved.fields.end())return;
    auto& field=found->second;
    if(entity_valid){
        std::vector<std::byte> current(field.written.size());
        if(!current.empty() && memory::read(saved.address+offset,current) && current==field.written){
            if(field.resource){Address original{};std::memcpy(&original,field.original.data(),8);assign_resource(reinterpret_cast<Address*>(saved.address+offset),&original);}
            else (void)memory::write(saved.address+offset,std::span<const std::byte>(field.original));
            saved.dirty=true;
        }
    }
    if(field.resource && field.original.size()==8){Address original{};std::memcpy(&original,field.original.data(),8);const Address empty{};assign_resource(&original,&empty);}
    saved.fields.erase(found);
}
void restore_environment_entity(EnvironmentEntity& saved,bool entity_valid) {
    saved.dirty=false;
    while(!saved.fields.empty())restore_environment_field(saved,saved.fields.begin()->first,entity_valid);
    if(entity_valid && saved.dirty && saved.kind=="env_sky")reinterpret_cast<SubclassChanged>(runtime().client+bindings::sky_state_changed)(reinterpret_cast<void*>(saved.address));
}

}
