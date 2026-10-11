#include "core/features/inventory/music.hpp"
#include "core/features/runtime.hpp"
#include "core/sdk/binding_values.hpp"
#include "core/sdk/entities.hpp"
#include <limits>

namespace iris::cosmetics::detail {
namespace {
Address inventory_services(Address controller) {
    return get<Address>(controller + sdk::binding_values::layout_music_inventory_services);
}
bool current_owner(Runtime& state,const ChangedMusic& saved) {
    return entity(state,saved.controller_handle)==saved.controller &&
        inventory_services(saved.controller)==saved.services;
}
std::uint32_t controller_handle(Runtime& state,Address controller) {
    const auto chunks=get<Address>(state.client+bindings::entity_chunks);
    const auto chunk=chunks?get<Address>(chunks):0;
    if(!chunk)return 0xffffffff;
    // Controllers occupy the reviewed first entity chunk; retain the full serial.
    for(unsigned index=1;index<=64;++index) {
        const auto identity=get<sdk::EntityIdentity>(chunk+index*sizeof(sdk::EntityIdentity));
        if(identity.entity==controller && (identity.handle&0x7fff)==index &&
            entity(state,identity.handle)==controller)return identity.handle;
    }
    return 0xffffffff;
}
}
void bind_music(Runtime& state) {
    if(schema_field(state.schema_scope,"CCSPlayerController","m_pInventoryServices")!=
            sdk::binding_values::layout_music_inventory_services ||
        schema_field(state.schema_scope,"CCSPlayerController_InventoryServices","m_unMusicID")!=
            sdk::binding_values::layout_music_service_id)
        throw std::runtime_error("Music inventory layout does not match the reviewed build.");
}
void restore_music(Runtime& state) {
    if(!state.music)return;
    const auto& saved=*state.music;
    if(current_owner(state,saved)) {
        const auto address=saved.services+sdk::binding_values::layout_music_service_id;
        if(get<std::uint16_t>(address)==saved.written &&
            !memory::write(address,saved.original))
            throw std::runtime_error("Cannot restore the local music kit.");
    }
    state.music.reset();
}
void update_music(Runtime& state) {
    const int desired=state.stopping?0:state.requested.music_kit;
    if(!desired){restore_music(state);return;}
    if(!find_music_kit(desired) || desired>std::numeric_limits<std::uint16_t>::max())
        throw std::runtime_error("Music kit is outside the current catalog.");
    const auto controller=get<Address>(state.client+bindings::local_controller);
    const auto handle=controller?controller_handle(state,controller):0xffffffff;
    const auto services=handle!=0xffffffff?inventory_services(controller):0;
    if(state.music && (state.music->controller!=controller ||
        state.music->controller_handle!=handle || state.music->services!=services))restore_music(state);
    if(!services)return;
    const auto address=services+sdk::binding_values::layout_music_service_id;
    const auto current=get<std::uint16_t>(address);
    const auto value=static_cast<std::uint16_t>(desired);
    if(!state.music) {
        if(current==value)return;
        state.music=ChangedMusic{controller,services,handle,current,current};
    }
    auto& saved=*state.music;
    if(!current_owner(state,saved))return;
    // A network update becomes the restoration baseline before reapplying.
    if(current!=saved.written)saved.original=current;
    if(current==value){saved.written=value;return;}
    if(!memory::write(address,value))
        throw std::runtime_error("Cannot apply the local music kit.");
    saved.written=value;
}
}
