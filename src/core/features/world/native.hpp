#pragma once
#include "core/features/inventory/inventory.hpp"
#include "core/hooks/minhook.hpp"
#include "core/sdk/world_bindings.hpp"
namespace iris::world {
void bind(std::uintptr_t client,hooks::Runtime& hooks);
void publish(const cosmetics::Environment& settings);
void stop() noexcept;
bool scene_verified() noexcept;
}
