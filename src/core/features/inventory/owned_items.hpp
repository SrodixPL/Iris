#pragma once
#include "core/sdk/economy_inventory.hpp"
#include <functional>
#include <optional>

namespace iris::cosmetics::inventory {
namespace native = iris::sdk::economy;
struct Request { native::Slot slot; native::ItemSpec item; };
// Injection point for meaningful transaction/failure tests. Native publication
// stays in the SDK; this class owns only reconciliation and Iris ownership.
class Backend {
public:
    virtual ~Backend() = default;
    virtual native::Context inspect() = 0;
    virtual bool current(const native::Context&) = 0;
    virtual std::vector<native::ItemIdentity> items(const native::Context&) = 0;
    virtual std::vector<native::EquippedItem> loadout(const native::Context&) = 0;
    virtual native::EquippedItem equipped(const native::Context&, native::Slot) = 0;
    virtual iris::sdk::Address view(const native::Context&, std::uint64_t) = 0;
    virtual iris::sdk::Address allocate(const native::Context&) = 0;
    virtual void initialize(const native::Context&, iris::sdk::Address, const native::ItemSpec&) = 0;
    virtual bool insert(const native::Context&, iris::sdk::Address) = 0;
    virtual void created(const native::Context&, iris::sdk::Address) = 0;
    virtual iris::sdk::Address detach(const native::Context&, iris::sdk::Address) = 0;
    virtual void destroyed(const native::Context&, iris::sdk::Address) = 0;
    virtual void destroy(const native::Context&, iris::sdk::Address) = 0;
    virtual void equip(const native::Context&, native::Slot, native::EquippedItem) = 0;
};
class OwnedItems {
    enum class Phase { Allocated, Inserted, Published, Detaching, Detached, ViewRetired, Destroying };
    struct Item {
        iris::sdk::Address address{};
        std::uint64_t id{};
        native::ItemSpec signature;
        Phase phase{Phase::Allocated};
    };
    struct Binding { native::Slot slot; std::uint64_t id{}; };
    Backend& backend_;
    std::optional<native::Context> context_;
    std::vector<Item> items_;
    std::vector<Binding> bindings_;
    std::vector<native::EquippedItem> originals_;
    // Iris policy, not a Valve-reserved namespace. Every allocation checks
    // collisions, and deletion always requires this ledger's exact pointer/ID.
    std::uint64_t next_id_{0x7000000000000000ull};
    void retire(Item& item);
    void cleanup_unused();
public:
    explicit OwnedItems(Backend& backend):backend_(backend) {}
    // Restore inline borrowed item references before any old economy item can
    // be retired. The callback runs on the same game-thread coordinator.
    void reconcile(const std::vector<Request>& desired, const std::function<void()>& before_equip = {});
    void restore(const std::function<void()>& before_equip = {});
    std::uint64_t item_for(native::Slot slot) const;
    // Native equip interception uses ledger/context identity, never an ID range.
    bool guard_native_equip(std::uint64_t id);
    bool currently_equipped(native::Slot slot,std::uint64_t id);
    void prepare_genuine_equip(native::Slot slot);
    std::size_t owned_count() const { return items_.size(); }
    bool recovery_pending() const { return !items_.empty(); }
};
}
