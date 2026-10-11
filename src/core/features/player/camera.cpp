#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
namespace iris::cosmetics::detail {
void __fastcall viewmodel_hook(std::array<float,3>* offsets,float* fov,bool clamp) {
    auto& state=runtime();state.original_viewmodel(offsets,fov,clamp);
    if(const auto value=state.viewmodel_override.load();value){
        if(value->offsets_override && offsets)*offsets=value->offsets;
        if(value->fov_override && fov)*fov=value->fov;
    }
}
void __fastcall pawn_viewmodel_hook(void* pawn,std::array<float,3>* offsets,float* fov) {
    auto& state=runtime();state.original_pawn_viewmodel(pawn,offsets,fov);
    if(const auto value=state.viewmodel_override.load();value && value->pawn==reinterpret_cast<Address>(pawn)){
        if(value->offsets_override && offsets)*offsets=value->offsets;
        if(value->fov_override && fov)*fov=value->fov;
    }
}
void __fastcall camera_hook(void* self,int slot) {
    auto& state=runtime();
    state.original_camera_think(self,slot);
    if(slot!=0 || !state.camera_enabled.load())return;
    const auto camera=reinterpret_cast<Address>(self);
    // Preserve the native collision result when it remains active. In a normal
    // practice session CAM_Think clears the command flag; restore only the local
    // requested view here, without changing global console permissions.
    if(memory::read<bool>(camera+bindings::camera_flag).value_or(false))return;
    auto angles=memory::read<std::array<float,3>>(camera+bindings::view_angles);
    const int count=memory::read<int>(camera+iris::sdk::binding_values::layout_camera_history_count).value_or(0);
    const auto history=memory::read<Address>(camera+iris::sdk::binding_values::layout_camera_history).value_or(0);
    if(count>0 && count<256 && history)
        angles=memory::read<std::array<float,3>>(history+(count-1)*iris::sdk::binding_values::layout_camera_history_stride+iris::sdk::binding_values::layout_camera_history_angles);
    if(!angles || !std::isfinite((*angles)[0]) || !std::isfinite((*angles)[1]))return;
    const float distance=state.camera_distance.load();
    if(!std::isfinite(distance))return;
    (*angles)[2]=std::clamp(distance,0.f,500.f);
    if(!memory::write(camera+bindings::camera_offset,*angles))return;
    if(!memory::write(camera+bindings::camera_flag,true))return;
}

void restore_camera_fields(Runtime& state){
    state.viewmodel_override.store(nullptr);
    if(entity(state,state.camera_pawn_handle)==state.camera_pawn && state.camera_pawn){
        restore_environment_entity(state.viewmodel_fields);
        const int services=environment_offset(state,"C_BasePlayerPawn","m_pCameraServices");
        if(services>=0 && get<Address>(state.camera_pawn+services)==state.camera_fields.address)restore_environment_entity(state.camera_fields);
    }
    state.viewmodel_fields={};state.camera_fields={};state.camera_pawn=0;
}
void update_camera_fields(Runtime& state){
    const auto controller=get<Address>(state.client+bindings::local_controller);
    const auto handle=controller?get<std::uint32_t>(controller+state.fields.pawn):0xffffffff;
    const auto pawn=entity(state,handle);
    const int services=environment_offset(state,"C_BasePlayerPawn","m_pCameraServices");
    const auto camera=pawn && services>=0?get<Address>(pawn+services):0;
    const auto& f=state.requested;
    if(state.camera_pawn && (pawn!=state.camera_pawn || handle!=state.camera_pawn_handle || camera!=state.camera_fields.address))restore_camera_fields(state);
    if(state.stopping || !pawn || get<int>(pawn+state.fields.health)<=0){restore_camera_fields(state);return;}
    state.camera_pawn=pawn;state.camera_pawn_handle=handle;
    state.viewmodel_fields.address=pawn;state.camera_fields.address=camera;
    if(!f.viewmodel_override && !f.viewmodel_fov_override)restore_environment_entity(state.viewmodel_fields);
    else {
        state.viewmodel_override.store(std::make_shared<const ViewmodelOverride>(ViewmodelOverride{pawn,{f.viewmodel_x,f.viewmodel_y,f.viewmodel_z},f.viewmodel_fov,f.viewmodel_fov_override,f.viewmodel_override}));
        const int x=environment_offset(state,"C_CSPlayerPawn","m_flViewmodelOffsetX");
        const int y=environment_offset(state,"C_CSPlayerPawn","m_flViewmodelOffsetY");
        const int z=environment_offset(state,"C_CSPlayerPawn","m_flViewmodelOffsetZ");
        if(f.viewmodel_override){
            environment_write(state.viewmodel_fields,x,f.viewmodel_x);
            environment_write(state.viewmodel_fields,y,f.viewmodel_y);
            environment_write(state.viewmodel_fields,z,f.viewmodel_z);
        }else{
            restore_environment_field(state.viewmodel_fields,x);
            restore_environment_field(state.viewmodel_fields,y);
            restore_environment_field(state.viewmodel_fields,z);
        }
        const int fov_offset=environment_offset(state,"C_CSPlayerPawn","m_flViewmodelFOV");
        if(f.viewmodel_fov_override)environment_write(state.viewmodel_fields,fov_offset,f.viewmodel_fov);
        else restore_environment_field(state.viewmodel_fields,fov_offset);
    }
    if(!f.viewmodel_override && !f.viewmodel_fov_override)state.viewmodel_override.store(nullptr);
    if(!camera)return;
    const int scoped=environment_offset(state,"C_CSPlayerPawn","m_bIsScoped");
    const auto is_scoped=scoped>=0 ? memory::read<bool>(pawn+scoped) : std::nullopt;
    if(is_scoped && (*is_scoped ? f.scope_fov_override : f.custom_fov)){
        const float fov=*is_scoped ? f.scope_fov : f.fov;
        environment_write(state.camera_fields,environment_offset(state,"CCSPlayerBase_CameraServices","m_iFOV"),static_cast<std::uint32_t>(fov));
        environment_write(state.camera_fields,environment_offset(state,"CCSPlayerBase_CameraServices","m_iFOVStart"),static_cast<std::uint32_t>(fov));
    }else restore_environment_entity(state.camera_fields);
}
}
