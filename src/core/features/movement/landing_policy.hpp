#pragma once
#include "core/features/movement/planner.hpp"
#include "core/features/movement/settings.hpp"
namespace iris::movement {
struct JumpState {
    std::uint32_t pawn{},opportunity{};
    bool grounded{},decided{},attempt{};
    unsigned count{};
};
inline bool bunny_hop(JumpState& state,std::uint32_t pawn,bool held,bool supported,float chance,unsigned limit,std::uint32_t decision_seed) noexcept {
    if(state.pawn!=pawn||!held){state={};state.pawn=pawn;}
    if(!held)return false;
    if(!supported){state.grounded=false;state.decided=false;return false;}
    if(!state.grounded){++state.opportunity;state.grounded=true;}
    if(!state.decided){
        state.decided=true;
        // Sample only on first support for this jump. Command identity varies the
        // sample after releasing jump, while the decided state preserves retries.
        state.attempt=(!limit||state.count<limit)&&std::isfinite(chance)&&chance>0&&(chance>=100||
            static_cast<double>(random_word(pawn^state.opportunity^decision_seed))*100/4294967296.0<chance);
        if(state.attempt)++state.count;
    }
    return state.attempt;
}
enum class LandingOwner {none,bhop};
struct ButtonAction {std::uint64_t button{};bool pressed{};float when{};};
struct LandingInput {
    std::uint32_t pawn{},decision_seed{};
    bool held{},grounded{},bhop{};
    float vertical_speed{},horizontal_speed{};
    std::optional<float> contact;
    Options options;
};
struct LandingDecision {
    LandingOwner owner{};
    bool owns_jump{},owns_duck{},jump{},retry{};
    std::array<ButtonAction,4> actions{};
    unsigned count{};
};
inline LandingDecision plan_landing(JumpState& state,const LandingInput& input) noexcept {
    if(state.pawn!=input.pawn||!input.bhop||!input.held){state={};state.pawn=input.pawn;}
    LandingDecision result;
    const bool contact=input.contact&&std::isfinite(*input.contact)&&*input.contact>0&&*input.contact<1;
    const float when=contact?*input.contact:1.f/64.f;
    const auto press=[&]{result.actions[0]={2,false,std::max(0.f,when-1.f/64.f)};result.actions[1]={2,true,when};result.count=2;};
    result.retry=state.decided&&state.attempt&&input.grounded;
    const bool attempt=bunny_hop(state,input.pawn,input.bhop&&input.held,input.grounded||contact,input.options.chance,static_cast<unsigned>(input.options.attempt_limit),input.decision_seed);
    if(input.bhop&&input.held){
        result.owner=LandingOwner::bhop;result.owns_jump=true;result.jump=input.grounded&&attempt;
        if(attempt)press();
    }
    return result;
}
}
