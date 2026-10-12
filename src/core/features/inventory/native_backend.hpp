#pragma once
#include "core/features/inventory/owned_items.hpp"

namespace iris::cosmetics::inventory {
// Runtime owns this adapter and OwnedItems together on the game thread. There
// is no second inventory cache or item ownership ledger in the adapter.
class NativeBackend final : public Backend {
    iris::sdk::Address client_{};
    void require_current(const native::Context& context) const;
public:
    explicit NativeBackend(iris::sdk::Address client):client_(client) {}
    native::Context inspect() override;
    bool current(const native::Context&) override;
    std::vector<native::ItemIdentity> items(const native::Context&) override;
    std::vector<native::EquippedItem> loadout(const native::Context&) override;
    native::EquippedItem equipped(const native::Context&, native::Slot) override;
    iris::sdk::Address view(const native::Context&, std::uint64_t) override;
    iris::sdk::Address allocate(const native::Context&) override;
    void initialize(const native::Context&, iris::sdk::Address, const native::ItemSpec&) override;
    bool insert(const native::Context&, iris::sdk::Address) override;
    void created(const native::Context&, iris::sdk::Address) override;
    iris::sdk::Address detach(const native::Context&, iris::sdk::Address) override;
    void destroyed(const native::Context&, iris::sdk::Address) override;
    void destroy(const native::Context&, iris::sdk::Address) override;
    void equip(const native::Context&, native::Slot, native::EquippedItem) override;
};
}
