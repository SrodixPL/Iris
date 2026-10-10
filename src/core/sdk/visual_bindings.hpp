#pragma once
#include "core/sdk/binding_values.hpp"
#include "core/sdk/offsets/build.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace iris::sdk::visual_bindings {
inline constexpr const char* tier_sha256=iris::sdk::reviewed_build::tier0_sha256;
inline constexpr const char* material_sha256=iris::sdk::reviewed_build::materialsystem2_sha256;
struct Primitive {
    std::array<std::byte,0x18> prefix{};
    std::uintptr_t object{},material{},material_copy{};
    std::array<std::byte,0x20> middle{};
    std::uint32_t color{};
    float opacity{};
    std::int32_t order{};
    std::array<std::byte,0x14> tail{};
};
static_assert(sizeof(Primitive)==0x70 && offsetof(Primitive,color)==0x50 && offsetof(Primitive,material)==0x20);
struct Output {
    std::uintptr_t fixed{};
    std::int32_t capacity{},count{},overflow_count{},reserved{};
    std::uintptr_t overflow{};
    std::int32_t overflow_capacity{},allocation_flags{};
    int size() const noexcept {
        if(capacity<0||count<0||overflow_count<0||overflow_capacity<0||count>capacity||overflow_count>overflow_capacity||
            count>1048576||overflow_count>1048576||(!fixed&&count)||(!overflow&&overflow_count))return -1;
        return count+overflow_count;
    }
    std::uintptr_t at(int index) const noexcept {
        if(index<0||index>=size())return 0;
        return index<count ? fixed+sizeof(Primitive)*index : overflow+sizeof(Primitive)*(index-count);
    }
};
static_assert(sizeof(Output)==0x28 && offsetof(Output,overflow)==0x18);
}
