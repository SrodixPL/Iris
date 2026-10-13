#pragma once
#include "core/features/combat/aim_math.hpp"
#include "util/latest_worker.hpp"

namespace iris::combat::workers {
struct Identity {
    std::shared_ptr<const Settings> generation;
    std::uint32_t pawn{},weapon{};
    std::int64_t command{};
    double sampled{};
};
inline bool current(const Identity& job,const Identity& now) noexcept {
    return job.generation==now.generation&&job.pawn==now.pawn&&job.weapon==now.weapon&&
        now.command>=job.command&&now.command-job.command<=8&&now.sampled>=job.sampled&&now.sampled-job.sampled<=50;
}
struct Candidate {
    std::uint32_t handle{};
    unsigned ordinal{};
    float angle{},score{};
    int rank{};
    bool locked{};
};
struct AimInput {Identity identity;std::vector<Candidate> candidates;int selection{};std::uint32_t locked{};};
struct AimResult {Identity identity;std::vector<Candidate> candidates;std::uint32_t locked{};};
struct RankAim {AimResult operator()(AimInput input) const;};
using Aim=LatestWorker<AimInput,AimResult,RankAim>;
struct TriggerInput {Identity identity;std::uint64_t epoch{};std::uint32_t target{};float delay{};};
struct TriggerResult {Identity identity;std::uint64_t epoch{};std::uint32_t target{};bool ready{};};
struct ConfirmTrigger {
    std::uint64_t epoch{};
    ShotConfirmation confirmation;
    TriggerResult operator()(TriggerInput input);
};
using Trigger=LatestWorker<TriggerInput,TriggerResult,ConfirmTrigger>;
// Treat a delayed callback as a stall, not a request to catch up camera motion.
inline float frame_step(float elapsed) noexcept {
    return std::isfinite(elapsed)&&elapsed>0?std::min(elapsed,1.f/64.f):0;
}
}
