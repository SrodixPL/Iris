#pragma once
#include <MinHook.h>
#include <memory>

namespace iris::hooks {
struct State;
// Lifecycle calls must be serialized; instances share one linked MinHook copy.
class Runtime {
public:
    Runtime();
private:
    std::shared_ptr<State> state_;
    friend class Hook;
};
class Hook {
public:
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
    std::shared_ptr<State> state_;
    void* target_{};
    void* original_{};
    bool enabled_{};
};
}
