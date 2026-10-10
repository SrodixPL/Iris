#pragma once
#include "core/features/runtime.hpp"
namespace iris::cosmetics::detail {
template<class T> void patch(ChangedWeapon& weapon, Address address, const T& value) {
    SavedField saved{address, std::vector<std::byte>(sizeof(T))};
    if (!memory::read(address, saved.bytes)) throw std::runtime_error("Cannot snapshot cosmetic field.");
    weapon.fields.push_back(std::move(saved));
    if (!memory::write(address, value)) throw std::runtime_error("Cannot update cosmetic field.");
}
struct RosterSample {
    std::array<std::array<EquippedSlot,58>,2> teams{};
    std::array<std::size_t,2> counts{};
};
}
