#pragma once
#include "core/hooks/inline_hook.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

namespace iris::cosmetics::detail {
struct PreviewTarget {
    std::uintptr_t texture{};
    int width{},height{};
    bool operator==(const PreviewTarget&) const = default;
};
struct PreviewCameraFrame {
    std::uintptr_t map{};
    std::array<float,3> origin{},angles{};
    float fov{};
    PreviewTarget target;
    bool operator==(const PreviewCameraFrame&) const = default;
};
struct PreviewFrameLedger {
    std::uint64_t generation{},applied_generation{},submitted_generation{},completed_generation{};
    std::optional<PreviewCameraFrame> applied,submitted,completed;
    void reset() noexcept {
        ++generation;applied.reset();submitted.reset();completed.reset();
        applied_generation=submitted_generation=completed_generation=0;
    }
    void apply(std::uint64_t epoch,const std::optional<PreviewCameraFrame>& before,const std::optional<PreviewCameraFrame>& after) {
        if(epoch!=generation)return;
        const bool unchanged=before && after && applied && applied_generation==epoch && *after==*applied;
        if(before && after && before->map==after->map) {applied=after;applied_generation=epoch;}
        else {applied.reset();applied_generation=0;}
        if(!unchanged) {submitted.reset();completed.reset();submitted_generation=completed_generation=0;}
    }
    void submit(std::uint64_t epoch,bool accepted,const std::optional<PreviewCameraFrame>& before,const std::optional<PreviewCameraFrame>& after) {
        if(epoch!=generation)return;
        if(accepted && before && after && applied && applied_generation==epoch &&
           *before==*applied && *after==*applied && after->target.texture) {submitted=after;submitted_generation=epoch;}
        else {submitted.reset();submitted_generation=0;}
        completed.reset();completed_generation=0;
    }
    bool complete(std::uint64_t epoch,bool ready,const std::optional<PreviewCameraFrame>& before,const std::optional<PreviewCameraFrame>& after) {
        if(epoch!=generation)return false;
        if(ready && before && after && submitted && submitted_generation==epoch && *before==*submitted && *after==*submitted) {
            completed=after;completed_generation=epoch;return true;
        }
        completed.reset();completed_generation=0;return false;
    }
    bool accepts(const std::optional<PreviewCameraFrame>& current) const noexcept {
        return current && completed && completed_generation==generation && *current==*completed;
    }
};
// All receipts belong to the established game-frame thread. Off-thread
// callbacks forward their original without reading this state or native data.
struct PreviewReceipts : PreviewFrameLedger {
    using CameraApply=void(__fastcall*)(void*);
    using Submit=bool(__fastcall*)(void*);
    using Complete=void(__fastcall*)(void*);
    std::unique_ptr<hooks::Hook> apply_hook,submit_hook,complete_hook;
    CameraApply original_apply{};
    Submit original_submit{};
    Complete original_complete{};
    std::atomic<unsigned> thread{};
    std::atomic<unsigned> off_thread{};
    std::uintptr_t client{},generator{},item{};
    std::uint64_t job{};
    std::array<float,3> expected_angles{};
    float expected_distance{};
    unsigned observed{};
    std::uint64_t logged{};
    void invalidate() noexcept;
};
struct Runtime;
PreviewTarget preview_target(std::uintptr_t generator);
void bind_preview_receipts(Runtime&);
void enable_preview_receipts(Runtime&);
void expect_preview_frame(Runtime&);
bool completed_preview_frame(Runtime&);
}
