#pragma once
#include "core/hooks/inline_hook.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace iris::visuals::native {
struct Target {
    std::uintptr_t pawn{},identity{},glow{};
    std::uint32_t handle{};
    std::array<float,4> color{};
    std::uint32_t scene_handle{};
    bool glow_enabled{},chams{},visible_chams{},hidden_chams{},original_material{};
    std::array<float,4> visible_color{},hidden_color{};
};
struct Frame {std::uint64_t sampled{};std::vector<Target> targets;std::array<std::uintptr_t,2> materials{};};
inline std::atomic<std::shared_ptr<const Frame>> frame;
void bind(std::uintptr_t client,hooks::Runtime& runtime);
void bind_glow(std::uintptr_t client,hooks::Runtime& runtime);
void bind_chams(hooks::Runtime& runtime);
void stop() noexcept;
std::array<std::uintptr_t,2> prepare_materials(int style);
void report();
}
