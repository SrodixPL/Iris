#pragma once
#include "core/features/world/esp_settings.hpp"
#include "core/features/player/visuals.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace iris::cosmetics::detail {
struct Runtime;
void update_world_visuals(Runtime& state);
// Game-thread lifecycle reset; stop() only clears the published render snapshot.
void clear_world_visuals(Runtime& state);
bool guarded_world_visuals(Runtime* state);
}
namespace iris::world::esp {
enum class Kind {Weapon,Hostage,Grenade,Bomb};
struct Label {visuals::Vector origin{};Kind kind{};std::string text;int definition{};};
struct Snapshot {visuals::Matrix matrix{};Settings settings;std::uint64_t sampled{};std::vector<Label> labels;};
inline std::atomic<std::shared_ptr<const Snapshot>> snapshot;
// Render consumes copied labels only. Native reads stay on the game thread.
bool draw(visuals::Icon (*lookup)(void*,int)=nullptr,void* owner=nullptr);
void stop() noexcept;
}
