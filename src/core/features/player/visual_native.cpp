#include "core/features/player/visual_native.hpp"
namespace iris::visuals::native {
void bind(std::uintptr_t client,hooks::Runtime& runtime) {
    bind_glow(client,runtime);
    bind_chams(runtime);
}
void stop() noexcept {frame.store(nullptr);}

}
