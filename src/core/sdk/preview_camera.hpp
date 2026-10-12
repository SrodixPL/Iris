#pragma once
#include "core/sdk/types.hpp"
#include <array>
#include <cstddef>

namespace iris::sdk::preview {
struct CameraControl {
    float yaw{},pitch{},zoom{};
    bool operator==(const CameraControl&) const = default;
};
bool valid_control(const CameraControl&) noexcept;
// Game-thread-owned native KV3. The external preview queue copies its contents;
// asset defaults and borrowed child pointers never leave this adapter.
class CameraSettings {
    alignas(8) std::array<std::byte,16> root_{};
    Address client_{};
    unsigned thread_{};
    bool constructed_{},uncertain_{};
    std::array<float,3> angles_{};
    double distance_{};
    std::array<float,3> applied_angles_{};
    float applied_distance_{};
    void require_thread() const;
public:
    CameraSettings()=default;
    CameraSettings(const CameraSettings&)=delete;
    CameraSettings& operator=(const CameraSettings&)=delete;
    bool prepare(Address client,Address scratch_item);
    void apply(const CameraControl&);
    void clear();
    bool ready() const noexcept {return constructed_&&!uncertain_&&distance_>0;}
    void* data() noexcept {return ready()?root_.data():nullptr;}
    const std::array<float,3>& applied_angles() const noexcept {return applied_angles_;}
    float applied_distance() const noexcept {return applied_distance_;}
};
}
