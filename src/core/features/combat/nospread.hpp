#pragma once
#include "core/hooks/minhook.hpp"
#include "core/features/combat/settings.hpp"
#include "core/features/combat/aim_settings.hpp"
#include "core/features/movement/steering.hpp"
#include <memory>
#include <array>
#include <atomic>
#include <cstdint>
namespace iris::nospread {
inline std::atomic<bool> enabled{},round_frozen{},rage_allowed{};
inline bool rage_active() noexcept {return enabled.load()&&rage_allowed.load();}
inline void safe_mode(int mode) noexcept {rage_allowed=mode==2;}
inline std::atomic<const char*> movement_status{"Disabled"};
inline std::atomic<std::shared_ptr<const Settings>> settings;
inline std::atomic<std::shared_ptr<const combat::Settings>> aim_settings;
inline std::atomic<std::shared_ptr<const movement::Settings>> movement_settings;
inline movement::NativeInput native_input;
inline std::atomic<bool> stopped{};
inline std::atomic<const char*> legit_status{"Waiting for game runtime"};
// Copied command-thread identity lets the overlay resolve the same profile safely.
inline std::atomic<int> legit_weapon_definition{};
inline std::atomic<float> camera_fov{90};
inline std::atomic<bool> player_alive{};
struct HudSnapshot {bool scoped{};float accuracy{},fov{90};std::uint64_t sampled{};};
inline std::atomic<bool> hud_enabled{};
inline std::atomic<std::shared_ptr<const HudSnapshot>> hud_snapshot;
inline std::atomic<const char*> status{"Waiting for game runtime"};
bool camera_view(std::array<float,3>& view) noexcept;
void bind(std::uintptr_t client,std::uintptr_t scope,hooks::Runtime& hooks);
}
