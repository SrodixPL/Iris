#include "core/features/combat/workers.hpp"
#include <algorithm>

namespace iris::combat::workers {
AimResult RankAim::operator()(AimInput input) const {
    std::erase_if(input.candidates,[](const auto& c){return !c.handle||!std::isfinite(c.angle)||!std::isfinite(c.score);});
    std::stable_sort(input.candidates.begin(),input.candidates.end(),[&](const auto& a,const auto& b){
        if(a.locked!=b.locked)return a.locked>b.locked;
        if(a.rank!=b.rank)return a.rank>b.rank;
        if(input.selection==1&&a.score!=b.score)return a.score>b.score;
        return a.angle<b.angle;
    });
    return {std::move(input.identity),std::move(input.candidates),input.locked};
}
TriggerResult ConfirmTrigger::operator()(TriggerInput input){
    if(epoch!=input.epoch){epoch=input.epoch;confirmation.reset();}
    const bool ready=confirmation.ready(input.target,input.identity.sampled,input.delay);
    return {std::move(input.identity),input.epoch,input.target,ready};
}
}
