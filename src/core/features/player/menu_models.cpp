#include "core/features/runtime.hpp"
#include "core/sdk/entities.hpp"
#include "core/sdk/player_bindings.hpp"
namespace iris::cosmetics::detail {
std::vector<std::pair<Address,int>> weapon_owners(Runtime& state,Address pawn,bool alive,int team) {
    std::vector<std::pair<Address,int>> owners;
    if(alive)owners.emplace_back(pawn,team);
    for(const auto& [handle,preview]:state.menu_preview_players){
        if(entity(state,handle)!=preview || preview==pawn)continue;
        const auto preview_team=get<std::uint8_t>(preview+state.fields.team);
        owners.emplace_back(preview,preview_team==2?2:3);
    }
    return owners;
}
void update_menu_models(Runtime& state) {
    const auto now=GetTickCount64();
    const bool active=!state.stopping;
    if(active && now<state.menu_model_tick)return;
    state.menu_model_tick=now+500;
    // Primary PreviewPlayer vtable, reviewed against the gated build's RTTI
    // and native constructor. Other menu entities must never be changed.
    const auto preview_vtable=state.client+player_bindings::preview_player_vtable;
    const auto alias_vtable=state.client+player_bindings::preview_player_alias_vtable;
    const auto preview=[&](Address address){
        const auto table=memory::read<Address>(address).value_or(0);
        return table==preview_vtable || table==alias_vtable;
    };
    const auto valid=[&](std::uint32_t handle,Address address){
        return entity(state,handle)==address && preview(address);
    };
    for(auto it=state.menu_players.begin();it!=state.menu_players.end();){
        if(!active || !valid(it->first,it->second.entity)){
            if(valid(it->first,it->second.entity) && !it->second.original_model.empty())
                state.set_model(reinterpret_cast<void*>(it->second.entity),it->second.original_model.c_str());
            it=state.menu_players.erase(it);
        }else ++it;
    }
    state.menu_preview_players.clear();
    if(!active)return;
    const auto chunks=get<Address>(state.client+bindings::entity_chunks);
    if(!chunks)return;
    bool refreshed=false;
    for(unsigned group=0;group<64;++group){
        const auto chunk=memory::read<Address>(chunks+group*8).value_or(0);
        if(!chunk)continue;
        std::array<std::byte,512*sizeof(EntityIdentity)> identities{};
        if(!memory::read(chunk,identities))continue;
        for(unsigned index=0;index<512;++index){
            EntityIdentity identity{};
            std::memcpy(&identity,identities.data()+index*sizeof(identity),sizeof(identity));
            const auto address=identity.entity;
            if(!address || !preview(address))continue;
            if(!valid(identity.handle,address))continue;
            state.menu_preview_players.emplace(identity.handle,address);
            const auto team=get<std::uint8_t>(address+state.fields.team);
            const auto side=team==2?0:1;
            // Catalog agents are equipped through the local owned inventory.
            // The game publishes that selection to the local lobby preview;
            // applying it to every PreviewPlayer also changes party members
            // and lets their default CT team overwrite a selected T agent.
            const std::string desired=state.requested.enabled ? state.requested.custom_models[side] : "";
            auto saved=state.menu_players.find(identity.handle);
            if(desired.empty()){
                if(saved!=state.menu_players.end()){
                    state.set_model(reinterpret_cast<void*>(address),saved->second.original_model.c_str());
                    state.menu_players.erase(saved);
                }
                continue;
            }
            const auto scene=get<Address>(address+state.fields.scene);
            const auto name=scene?get<Address>(scene+state.fields.model+state.fields.model_name):0;
            if(!name)continue;
            const auto current=string_at(name);
            if(current.empty())continue;
            if(models::same_resource_path(current,desired) && !state.menu_model_refresh)continue;
            if(!load_resource(state,desired.c_str(),0x6c646d76)){
                if(saved!=state.menu_players.end())state.set_model(reinterpret_cast<void*>(address),saved->second.original_model.c_str());
                continue;
            }
            if(saved==state.menu_players.end()){
                ChangedWeapon original;original.entity=address;original.handle=identity.handle;original.original_model=current;
                saved=state.menu_players.emplace(identity.handle,std::move(original)).first;
            }
            state.set_model(reinterpret_cast<void*>(address),desired.c_str());
            const auto applied_name=get<Address>(scene+state.fields.model+state.fields.model_name);
            if(!applied_name || !models::same_resource_path(string_at(applied_name),desired)){
                state.set_model(reinterpret_cast<void*>(address),saved->second.original_model.c_str());
                continue;
            }
            overlay_log(("Main-menu custom model applied: "+desired).c_str());
            refreshed=true;
        }
    }
    if(refreshed)state.menu_model_refresh=false;
}
bool guarded_menu_models(Runtime* state) {
    __try {update_menu_models(*state);return true;}
    __except(GetExceptionCode()==0xe06d7363?EXCEPTION_CONTINUE_SEARCH:EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
