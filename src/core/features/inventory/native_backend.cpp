#include "core/features/inventory/native_backend.hpp"
#include <stdexcept>

namespace iris::cosmetics::inventory {
void NativeBackend::require_current(const native::Context& context) const {
    if (context.client!=client_ || !native::current(context))
        throw std::runtime_error("Inventory adapter no longer owns the current native context.");
}
native::Context NativeBackend::inspect() { return native::inspect(client_); }
bool NativeBackend::current(const native::Context& context) { return context.client==client_ && native::current(context); }
std::vector<native::ItemIdentity> NativeBackend::items(const native::Context& context) { require_current(context);return native::items(context); }
std::vector<native::EquippedItem> NativeBackend::loadout(const native::Context& context) { require_current(context);return native::loadout_snapshot(context); }
native::EquippedItem NativeBackend::equipped(const native::Context& context,native::Slot slot) { require_current(context);return native::equipped(context,slot); }
iris::sdk::Address NativeBackend::view(const native::Context& context,std::uint64_t id) { require_current(context);return native::item_view(context,id); }
iris::sdk::Address NativeBackend::allocate(const native::Context& context) { require_current(context);return native::allocate(client_); }
void NativeBackend::initialize(const native::Context& context,iris::sdk::Address object,const native::ItemSpec& spec) { require_current(context);native::initialize(context,object,spec); }
bool NativeBackend::insert(const native::Context& context,iris::sdk::Address object) { require_current(context);return native::insert(context,object); }
void NativeBackend::created(const native::Context& context,iris::sdk::Address object) { require_current(context);native::notify_created(context,object); }
iris::sdk::Address NativeBackend::detach(const native::Context& context,iris::sdk::Address object) { require_current(context);return native::detach(context,object); }
void NativeBackend::destroyed(const native::Context& context,iris::sdk::Address object) { require_current(context);native::notify_destroyed(context,object); }
void NativeBackend::destroy(const native::Context& context,iris::sdk::Address object) { require_current(context);native::destroy(client_,object); }
void NativeBackend::equip(const native::Context& context,native::Slot slot,native::EquippedItem item) { require_current(context);native::equip_local(context,slot,item); }
}
