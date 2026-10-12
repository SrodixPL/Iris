#pragma once
#include <memory>

namespace iris::hooks {
struct State;
// Runtimes share a serialized target registry; hooks retain their runtime state.
class Runtime {
public:
    Runtime();
private:
    std::shared_ptr<State> state_;
    friend class Hook;
};
class Hook {
public:
    // Creates a disabled hook. Publish original() before calling enable().
    Hook(const Runtime& runtime, void* target, void* detour);
    ~Hook();
    Hook(const Hook&) = delete;
    Hook& operator=(const Hook&) = delete;
    void enable();
    void disable();
    // Quiesce callers before close/destruction. Trampoline becomes invalid.
    void close();
    template<class Function> [[nodiscard]] Function original() const noexcept {
        return reinterpret_cast<Function>(original_);
    }
private:
    // Keep SafetyHook's C++23 types out of the C++20 feature headers.
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void* original_{};
};
}
