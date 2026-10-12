#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
#include "core/features/world/native.hpp"
#include "core/sdk/world_bindings.hpp"
#include <fstream>
#include "core/features/world/environment.hpp"

namespace iris::cosmetics::detail {
void update_environment(Runtime& state) {
    auto& settings=state.environment;
    const auto now=GetTickCount64();
    for(auto it=state.smoke_volumes.begin();it!=state.smoke_volumes.end();){
        const auto owner=entity(state,it->first);
        const bool valid=owner && memory::read<Address>(owner+sdk::world_bindings::smoke_state)==it->second.address;
        if(!valid || !settings.enabled || !settings.smoke_override || state.stopping){
            restore_environment_entity(it->second,valid);it=state.smoke_volumes.erase(it);
        }else ++it;
    }
    if((!settings.enabled&&!state.requested.player_visuals.radar) || state.stopping) {
        for(auto& [handle,saved]:state.environment_entities)
            restore_environment_entity(saved,entity(state,handle)==saved.address);
        state.environment_entities.clear();state.environment_scan=0;return;
    }
    if(now<state.environment_tick)return;
    state.environment_tick=now+50;
    if(now>=state.environment_scan){
        state.environment_scan=now+1000;
        const auto chunks=get<Address>(state.client+bindings::entity_chunks);
        if(chunks)for(unsigned group=0;group<64;++group){
            const auto chunk=memory::read<Address>(chunks+group*8).value_or(0);if(!chunk)continue;
            std::array<std::byte,512*0x70> identities{};
            if(!memory::read(chunk,identities))continue;
            for(unsigned index=0;index<512;++index){
                const auto row=std::span<const std::byte>(identities).subspan(index*0x70,0x70);
                const auto address=memory::read_field<Address>(row,0).value_or(0);
                const auto name=memory::read_field<Address>(row,0x20).value_or(0);
                if(!address||!name)continue;
                std::array<char,64> text{};
                if(!memory::read(name,std::as_writable_bytes(std::span(text))))continue;
                text.back()=0;const std::string kind=text.data();
                if(kind!="env_sky" && kind!="env_fog_controller" && kind!="fog_controller" &&
                   kind!="env_volumetric_fog_controller" && kind!="env_cubemap_fog" && kind!="env_cubemap" && kind!="env_cubemap_box" &&
                   kind!="env_wind" && kind!="env_wind_clientside" && kind!="smokegrenade_projectile" && kind!="cs_player_pawn" && kind!="post_processing_volume")continue;
                const auto handle=memory::read_field<std::uint32_t>(row,0x10).value_or(0xffffffff);
                auto& saved=state.environment_entities[handle];
                if(saved.address!=address){restore_environment_entity(saved,false);saved=EnvironmentEntity{address,kind,{}};}
            }
        }
    }
    for(auto it=state.environment_entities.begin();it!=state.environment_entities.end();){
        auto& [handle,saved]=*it;
        if(entity(state,handle)!=saved.address){restore_environment_entity(saved,false);it=state.environment_entities.erase(it);continue;}++it;
        saved.dirty=false;
        if(saved.kind=="env_sky") {
            update_skyboxes(state,saved);
        } else if(saved.kind=="post_processing_volume"){
            update_postprocessing(state,saved);
        } else if(saved.kind=="smokegrenade_projectile"){
            update_smoke(state,saved,handle);
        } else if(saved.kind=="cs_player_pawn"){
            update_flash(state,saved);
        } else if(saved.kind=="env_cubemap" || saved.kind=="env_cubemap_box"){
            restore_environment_entity(saved);
        } else if(saved.kind=="env_wind" || saved.kind=="env_wind_clientside"){
            update_wind(state,saved);
        } else if(saved.kind=="env_cubemap_fog"){
            restore_environment_entity(saved);
        } else if(saved.kind=="env_volumetric_fog_controller"){
            // Do not force expensive map volumetric lighting on. Independent
            // colored fog now uses scene depth in the existing grading pass.
            restore_environment_entity(saved);
        } else {
            restore_environment_entity(saved);
        }
    }
}
int environment_exception(Runtime* state,int phase,EXCEPTION_POINTERS* exception){
    if(exception->ExceptionRecord->ExceptionCode==0xe06d7363)return EXCEPTION_CONTINUE_SEARCH;
    if(GetTickCount64()>=state->environment_fault_at[phase]){
        state->environment_fault_at[phase]=GetTickCount64()+10000;
        char message[180]{};
        std::snprintf(message,sizeof(message),"Native update phase %d fault: code=%lx instruction=%p clientRVA=%llx.",
            phase,exception->ExceptionRecord->ExceptionCode,exception->ExceptionRecord->ExceptionAddress,
            reinterpret_cast<Address>(exception->ExceptionRecord->ExceptionAddress)-state->client);
        overlay_log(message);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
bool guarded_environment(Runtime* state,int phase){
    __try {
        switch(phase){case 0:update_native_variables(*state);break;case 1:update_environment(*state);break;
          case 2:update_camera_fields(*state);break;case 3:update_weather(*state);break;}
        return true;
    }
    __except(environment_exception(state,phase,GetExceptionInformation())){return false;}
}

}
