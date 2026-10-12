#pragma once
#include "core/features/inventory/inventory.hpp"
#include "core/features/inventory/owned_items.hpp"

namespace iris::cosmetics::inventory {
// Build from copied menu/profile and roster state. No native calls or pointers
// cross this boundary; the game-thread owner performs one reconciliation.
std::vector<Request> requests(const Configuration& configuration,const EquippedLoadout& roster);
}
