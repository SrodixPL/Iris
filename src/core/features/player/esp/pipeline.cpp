#include "core/features/player/esp/pipeline.hpp"

namespace iris::visuals::esp {
namespace {
std::atomic<std::uint64_t> dimensions{};
std::atomic<Worker*> active{};
Worker& worker(){static Worker value{Prepare{}};active.store(&value);return value;}
}
Prepared Prepare::operator()(Prepared input) const {
    if(input.players){input.bounds.reserve(input.players->players.size());for(const auto& player:input.players->players)input.bounds.push_back(player_bounds(input.players->matrix,player,input.width,input.height));}
    if(input.world){input.labels.reserve(input.world->labels.size());for(const auto& label:input.world->labels)input.labels.push_back(project(input.world->matrix,label.origin,input.width,input.height));}
    return input;
}
void viewport(float width,float height) noexcept {
    if(!std::isfinite(width)||!std::isfinite(height)||width<1||height<1||width>32768||height>32768)return;
    dimensions=static_cast<std::uint64_t>(static_cast<unsigned>(width))<<32|static_cast<unsigned>(height);
}
void publish(){
    auto players=snapshot.load();auto world=world::esp::snapshot.load();const auto size=dimensions.load();
    if(!players&&!world){worker().cancel();return;}
    worker().submit({std::move(players),std::move(world),static_cast<float>(size>>32),static_cast<float>(size&0xffffffff)});
}
std::shared_ptr<const Prepared> prepared() noexcept {const auto* value=active.load();return value?value->result():nullptr;}
}
