#pragma once
#include "core/sdk/types.hpp"
#include <cstdint>

namespace iris::cosmetics::detail {
struct Runtime;
struct ChangedMusic {
    sdk::Address controller{}, services{};
    std::uint32_t controller_handle{0xffffffff};
    std::uint16_t original{}, written{};
};
void bind_music(Runtime& state);
void update_music(Runtime& state);
void restore_music(Runtime& state);
}
