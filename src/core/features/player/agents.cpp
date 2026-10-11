#include "core/sdk/binding_values.hpp"
#include "core/features/runtime.hpp"
#include "core/features/inventory/patch.hpp"

namespace iris::cosmetics::detail {
void restore_agent(Runtime& state) {
    if(!state.agent)return;
    auto& saved=*state.agent;
    if(entity(state,saved.handle)==saved.entity && !saved.original_model.empty()) {
        state.set_model(reinterpret_cast<void*>(saved.entity),saved.original_model.c_str());
        if(!memory::write(saved.entity+state.fields.glove_reapply,true))throw std::runtime_error("Cannot refresh restored agent gloves.");
    }
    state.agent.reset();
    state.agent_resource.clear();
    state.agent_next_refresh = 0;
}
void update_agent(Runtime& state,Address pawn,std::uint32_t handle,bool alive,int team) {
    const bool active=alive && team>=2 && team<=3 && state.requested.enabled && !state.stopping;
    const int definition=active ? state.requested.agents[team-2] : 0;
    const auto* agent=find_agent(definition);
    const std::string desired=active && !state.requested.custom_models[team-2].empty() ? state.requested.custom_models[team-2] : agent ? agent->model : "";
    if(state.agent && (desired.empty() || state.agent->entity!=pawn || state.agent->handle!=handle || !models::same_resource_path(state.agent_resource,desired)))restore_agent(state);
    if(desired.empty())return;
    const auto scene=get<Address>(pawn+state.fields.scene);
    if(!scene)return;
    const auto name=get<Address>(scene+state.fields.model+state.fields.model_name);
    if(!name)return;
    const auto current=string_at(name);
    if(current.empty())return;
    if(!state.agent) {
        Selection value;value.definition=definition;
        state.agent.emplace(ChangedWeapon{pawn,handle,value,{}});
        state.agent->original_model=current;
        state.agent_resource=desired;
    }
    if(!models::same_resource_path(current,desired) && GetTickCount64() >= state.agent_next_refresh) {
        state.agent_next_refresh = GetTickCount64() + 500;
        if(!load_resource(state,desired.c_str(),0x6c646d76)){
            state.agent_next_refresh=GetTickCount64()+3000;
            overlay_log(("Agent model resource is not loaded; preserving current model: "+desired).c_str());
            return;
        }
        state.set_model(reinterpret_cast<void*>(pawn),desired.c_str());
        const auto applied_name=get<Address>(scene+state.fields.model+state.fields.model_name);
        const auto applied=applied_name?string_at(applied_name):std::string{};
        if(!models::same_resource_path(applied,desired)){
            const auto original=state.agent->original_model;
            state.set_model(reinterpret_cast<void*>(pawn),original.c_str());
            state.agent_next_refresh=GetTickCount64()+3000;
            overlay_log(("Agent model load failed: requested="+desired+" resolved="+applied+"; original restored.").c_str());
            return;
        }
        if(!memory::write(pawn+state.fields.glove_reapply,true))throw std::runtime_error("Cannot refresh agent gloves.");
    }
}

}
