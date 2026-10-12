#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
#include "core/features/world/native.hpp"
#include "core/sdk/world_bindings.hpp"
#include <fstream>
#include "core/features/world/environment.hpp"

namespace iris::cosmetics::detail {
namespace {
void __fastcall flash_effect(Address self,Address target,float* duration){
    auto& state=runtime();state.original_flash_effect(self,target,duration);
    if(state.stopping||!state.environment.remove_flash)return;
    const auto controller=get<Address>(state.client+bindings::local_controller);
    const auto handle=controller?get<std::uint32_t>(controller+state.fields.pawn):0xffffffff;
    if(!target||entity(state,handle)!=target)return;
    auto& saved=state.environment_entities[handle];
    if(saved.address!=target){restore_environment_entity(saved,false);saved=EnvironmentEntity{target,"cs_player_pawn",{}};}
    update_flash(state,saved);
}
}
void bind_flash(Runtime& state){
    state.flash_effect_hook=std::make_unique<hooks::Hook>(*state.hooks,reinterpret_cast<void*>(state.client+sdk::binding_values::client_flash_effect),reinterpret_cast<void*>(&flash_effect));
    state.original_flash_effect=state.flash_effect_hook->original<Runtime::FlashEffect>();state.flash_effect_hook->enable();
}
void update_flash(Runtime& state,EnvironmentEntity& saved) {
    const auto& settings=state.environment;
    const auto offset=[&](const char* type,const char* name){return environment_offset(state,type,name);};

            const auto controller=get<Address>(state.client+bindings::local_controller);
            const auto pawn=controller?entity(state,get<std::uint32_t>(controller+state.fields.pawn)):0;
            const int alpha=offset("C_CSPlayerPawnBase","m_flFlashMaxAlpha");
            if(settings.remove_flash && saved.address==pawn)environment_write(saved,alpha,0.f);
            else restore_environment_field(saved,alpha);
            for(const auto* name:{"m_flFlashOverlayAlpha","m_flFlashScreenshotAlpha"}){
                const int field=offset("C_CSPlayerPawnBase",name);
                if(settings.remove_flash&&saved.address==pawn)environment_write(saved,field,0.f);else restore_environment_field(saved,field);
            }
            const int spotted=offset("C_CSPlayerPawn","m_entitySpottedState"),flag=offset("EntitySpottedState_t","m_bSpotted"),mask=offset("EntitySpottedState_t","m_bSpottedByMask");
            const int team=offset("C_BaseEntity","m_iTeamNum");
            const bool radar=state.requested.player_visuals.radar&&pawn&&saved.address!=pawn&&team>=0&&get<std::uint8_t>(saved.address+team)!=get<std::uint8_t>(pawn+team);
            if(spotted>=0&&flag>=0&&mask>=0){
                if(radar){environment_write(saved,spotted+flag,true);environment_write(saved,spotted+mask,std::array<std::uint32_t,2>{0xffffffff,0xffffffff});}
                else {restore_environment_field(saved,spotted+flag);restore_environment_field(saved,spotted+mask);}
            }
        }

}
