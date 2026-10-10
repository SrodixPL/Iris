#pragma once
#include <cstdint>
namespace iris::sdk::binding_values {
#define IRIS_BINDING(group, name, value) inline std::uintptr_t group##_##name=value;
#define IRIS_LAYOUT(group, name, value) inline constexpr std::uintptr_t group##_##name=value;
#include "core/sdk/offsets/offsets.inc"
#undef IRIS_LAYOUT
#undef IRIS_BINDING
}
