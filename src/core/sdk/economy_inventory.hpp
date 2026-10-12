#pragma once
#include "core/sdk/types.hpp"
#include <array>
#include <cstddef>
#include <vector>

namespace iris::sdk::economy {
// Copied, identity-bound data. Native object addresses never enter menu state.
struct Context {
    Address client{}, inventory{}, shared_cache{}, type_cache{};
    std::array<std::byte,16> owner{};
    std::uint32_t account{};
    bool operator==(const Context&) const = default;
};
struct Slot {
    std::uint16_t team{}, slot{};
    bool operator==(const Slot&) const = default;
};
struct EquippedItem {
    std::uint64_t id{};
    std::uint16_t definition{};
    bool operator==(const EquippedItem&) const = default;
};
struct ItemIdentity {
    Address object{};
    std::uint64_t id{};
    std::uint32_t account{}, position{};
    std::uint16_t definition{};
};
struct Attribute {
    std::uint16_t definition{};
    // Default-schema attributes use uint32 storage, including float bit patterns.
    std::uint32_t bits{};
    bool operator==(const Attribute&) const = default;
};
struct ItemSpec {
    std::uint64_t id{};
    std::uint32_t position{};
    std::uint16_t definition{};
    std::uint16_t packed_metadata{};
    std::array<char,161> name{};
    std::vector<Attribute> attributes;
};

// The game-thread coordinator must serialize every mutation and own rollback.
// A factory allocation is caller-owned until cache insertion succeeds. Native
// cache destruction owns inserted items; stale contexts must never free them.
Context inspect(Address client);
bool current(const Context& context);
std::vector<ItemIdentity> items(const Context& context);
bool owns(const Context& context, Address object, std::uint64_t id);
EquippedItem equipped(const Context& context, Slot slot);
std::vector<EquippedItem> loadout_snapshot(const Context& context);
Address item_view(const Context& context, std::uint64_t id);
Address loadout_view(const Context& context, Slot slot);
Address allocate(Address client);
void initialize(const Context& context, Address object, const ItemSpec& spec);
bool insert(const Context& context, Address object);
void notify_created(const Context& context, Address object);
Address detach(const Context& context, Address object);
void notify_destroyed(const Context& context, Address object);
void destroy(Address client, Address object);
void equip_local(const Context& context, Slot slot, EquippedItem item);
}
