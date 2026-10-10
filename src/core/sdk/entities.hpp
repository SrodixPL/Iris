#pragma once
#include "core/sdk/types.hpp"
#include <cstddef>
namespace iris::sdk {
struct EntityIdentity {
    Address entity;
    std::byte metadata[8];
    std::uint32_t handle;
    std::byte tail[0x5c];
};
static_assert(sizeof(EntityIdentity) == 0x70);
static_assert(offsetof(EntityIdentity, handle) == 0x10);
}
