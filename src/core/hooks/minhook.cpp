#include "core/hooks/minhook.hpp"
#include <stdexcept>
#include <string>
#include "core/log.hpp"
#include <cstdio>

namespace iris::hooks {
namespace {
void check(MH_STATUS result, const char* operation) {
#ifdef IRIS_DEBUG
    char message[160]{};
    std::snprintf(message, sizeof(message), "%s: %s", operation, MH_StatusToString(result));
    overlay_log(message);
#endif
    if (result != MH_OK) throw std::runtime_error(std::string(operation) + ": " + MH_StatusToString(result));
}
std::weak_ptr<State> active_runtime;
}
struct State {
    State() { check(MH_Initialize(), "MH_Initialize"); }
    ~State() { MH_Uninitialize(); }
};
Runtime::Runtime() {
    state_ = active_runtime.lock();
    if (!state_) { state_ = std::make_shared<State>(); active_runtime = state_; }
}
Hook::Hook(const Runtime& runtime, void* target, void* detour) : state_(runtime.state_), target_(target) {
    if (!target || !detour || !state_) throw std::invalid_argument("Hook requires a runtime, target and detour");
    check(MH_CreateHook(target, detour, &original_), "MH_CreateHook");
#ifdef IRIS_DEBUG
    char message[180]{};
    std::snprintf(message, sizeof(message), "Hook target=%p detour=%p trampoline=%p", target, detour, original_);
    overlay_log(message);
#endif
}
Hook::~Hook() {
    // Never leave a detour pointing at unloaded code after cleanup failure.
    try { close(); } catch (...) { std::terminate(); }
}
void Hook::enable() {
    if (!target_) throw std::logic_error("Hook is closed");
    if (!enabled_) { check(MH_EnableHook(target_), "MH_EnableHook"); enabled_ = true; }
}
void Hook::disable() {
    if (enabled_) { check(MH_DisableHook(target_), "MH_DisableHook"); enabled_ = false; }
}
void Hook::close() {
    if (!target_) return;
    disable();
    check(MH_RemoveHook(target_), "MH_RemoveHook");
    target_ = nullptr;
    original_ = nullptr;
}
}
