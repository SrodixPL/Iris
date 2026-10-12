#include "core/hooks/inline_hook.hpp"
#include <safetyhook.hpp>
#include <Windows.h>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include "core/log.hpp"

namespace iris::hooks {
namespace {
std::mutex runtime_mutex;
std::weak_ptr<State> active_runtime;

const char* error_name(const safetyhook::InlineHook::Error& error) noexcept {
    using Error = safetyhook::InlineHook::Error;
    switch (error.type) {
    case Error::BAD_ALLOCATION:
        return error.allocator_error == safetyhook::Allocator::Error::NO_MEMORY_IN_RANGE
            ? "no trampoline memory in range" : "trampoline allocation failed";
    case Error::FAILED_TO_DECODE_INSTRUCTION: return "instruction decoding failed";
    case Error::SHORT_JUMP_IN_TRAMPOLINE: return "short jump in trampoline";
    case Error::IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE: return "IP-relative instruction out of range";
    case Error::UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE: return "unsupported trampoline instruction";
    case Error::FAILED_TO_UNPROTECT: return "memory protection change failed";
    case Error::NOT_ENOUGH_SPACE: return "insufficient space for hook";
    default: return "unknown error";
    }
}
[[noreturn]] void fail(const safetyhook::InlineHook::Error& error, const char* operation, void* target) {
    char message[256]{};
    std::snprintf(message, sizeof(message), "%s target=%p: %s", operation, target, error_name(error));
    overlay_log(message);
    throw std::runtime_error(message);
}
bool executable(void* address) noexcept {
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(address, &info, sizeof(info)) == sizeof(info) && info.State == MEM_COMMIT &&
        !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
        (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}
}
struct State {
    std::mutex mutex;
    std::unordered_set<void*> targets;
    std::shared_ptr<safetyhook::Allocator> allocator = safetyhook::Allocator::create();
};
struct Hook::Impl {
    explicit Impl(std::shared_ptr<State> shared) : state(std::move(shared)) {}
    std::shared_ptr<State> state;
    safetyhook::InlineHook hook;
    void* target{};
};
Runtime::Runtime() {
    std::lock_guard lock(runtime_mutex);
    state_ = active_runtime.lock();
    if (!state_) { state_ = std::make_shared<State>(); active_runtime = state_; }
}
Hook::Hook(const Runtime& runtime, void* target, void* detour) {
    if (!target || !detour || !runtime.state_)
        throw std::invalid_argument("Hook requires a runtime, target and detour");
    if (!executable(target) || !executable(detour))
        throw std::invalid_argument("Hook target and detour must be executable");
    impl_ = std::make_unique<Impl>(runtime.state_);
    std::lock_guard lock(impl_->state->mutex);
    if (!impl_->state->targets.insert(target).second)
        throw std::runtime_error("Hook target is already registered");
    try {
        auto result = safetyhook::InlineHook::create(impl_->state->allocator, target, detour,
            safetyhook::InlineHook::StartDisabled);
        if (!result) fail(result.error(), "SafetyHook create", target);
        impl_->hook = std::move(*result);
        impl_->target = target;
        original_ = impl_->hook.original<void*>();
    } catch (...) {
        impl_->state->targets.erase(target);
        throw;
    }
#ifdef IRIS_DEBUG
    char message[180]{};
    std::snprintf(message, sizeof(message), "SafetyHook target=%p detour=%p trampoline=%p", target, detour, original_);
    overlay_log(message);
#endif
}
Hook::~Hook() {
    // Never release a trampoline or callback owner after cleanup failure.
    try { close(); } catch (...) { std::terminate(); }
}
void Hook::enable() {
    std::lock_guard lock(impl_->state->mutex);
    if (!impl_->target) throw std::logic_error("Hook is closed");
    if (auto result = impl_->hook.enable(); !result)
        fail(result.error(), "SafetyHook enable", impl_->target);
}
void Hook::disable() {
    std::lock_guard lock(impl_->state->mutex);
    if (!impl_->target) return;
    if (auto result = impl_->hook.disable(); !result)
        fail(result.error(), "SafetyHook disable", impl_->target);
}
void Hook::close() {
    std::lock_guard lock(impl_->state->mutex);
    if (!impl_->target) return;
    if (auto result = impl_->hook.disable(); !result)
        fail(result.error(), "SafetyHook disable", impl_->target);
    impl_->hook.reset();
    impl_->state->targets.erase(impl_->target);
    impl_->target = nullptr;
    original_ = nullptr;
}
}
