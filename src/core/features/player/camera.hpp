#pragma once
#include <cstdint>
namespace iris::cosmetics {
// Resolve the local presentation selection without modifying native camera
// transition state. A supplied service identity limits a render callback.
float camera_fov(float native_fov,std::uintptr_t camera_services=0) noexcept;
}
