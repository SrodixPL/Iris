#pragma once
#include "core/features/world/esp.hpp"
#include "util/latest_worker.hpp"

namespace iris::visuals::esp {
struct Prepared {
    std::shared_ptr<const Snapshot> players;
    std::shared_ptr<const world::esp::Snapshot> world;
    float width{},height{};
    std::vector<std::optional<ScreenBounds>> bounds;
    std::vector<std::optional<Vector>> labels;
};
struct Prepare {Prepared operator()(Prepared input) const;};
using Worker=LatestWorker<Prepared,Prepared,Prepare>;
void viewport(float width,float height) noexcept;
// Called once after native readers on the owning game thread.
void publish();
std::shared_ptr<const Prepared> prepared() noexcept;
}
