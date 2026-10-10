#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
#include "core/features/world/native.hpp"
#include "core/sdk/world_bindings.hpp"
#include <fstream>

namespace iris::cosmetics::detail {
void update_native_variables(Runtime& state) {
    std::vector<Address> active;
    const auto set=[&]<class T>(Address binding,const T& value){
        const auto data=memory::read<Address>(state.client+binding+8).value_or(0);
        if(!data)return;
        active.push_back(data);
        auto& saved=state.native_variables[data];saved.address=data;
        // The reviewed native viewmodel readers consume ConVarData +58.
        environment_write(saved,0x58,value);
    };
    const auto controller=get<Address>(state.client+bindings::local_controller);
    const auto pawn=controller?entity(state,get<std::uint32_t>(controller+state.fields.pawn)):0;
    if(!state.stopping && pawn && get<int>(pawn+state.fields.health)>0 && state.requested.viewmodel_override){
        const auto& f=state.requested;
        set(bindings::viewmodel_x,f.viewmodel_x);set(bindings::viewmodel_y,f.viewmodel_y);
        set(bindings::viewmodel_z,f.viewmodel_z);set(bindings::viewmodel_fov,f.viewmodel_fov);
    }
    if(!state.stopping && state.environment.enabled){
        const auto& w=state.environment;
        if(w.remove_3d_sky)set(sdk::world_bindings::draw_3d_sky,false);
        if(w.remove_ragdolls)set(sdk::world_bindings::ragdoll_limit,0);
        if(w.remove_fog){set(bindings::fog_enable,false);set(bindings::cubemap_fog_enable,false);}
        if(w.remove_shadows && world::scene_verified()){
            const auto module=reinterpret_cast<Address>(GetModuleHandleW(L"scenesystem.dll"));
            const auto data=memory::read<Address>(module+sdk::world_bindings::shadows+8).value_or(0);
            if(data){active.push_back(data);auto& saved=state.native_variables[data];saved.address=data;environment_write(saved,0x58,false);}
        }
    }
    for(auto it=state.native_variables.begin();it!=state.native_variables.end();){
        if(std::find(active.begin(),active.end(),it->first)==active.end()){
            restore_environment_entity(it->second);it=state.native_variables.erase(it);
        }else ++it;
    }
}

}
