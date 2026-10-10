#pragma once
#include <Windows.h>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace iris {
template<class T> class ThreadState {
    static void NTAPI destroy(void* value) noexcept { delete static_cast<T*>(value); }
    DWORD slot_{FlsAlloc(destroy)};
public:
    ThreadState() { if (slot_ == FLS_OUT_OF_INDEXES) throw std::runtime_error("Cannot allocate thread state"); }
    ~ThreadState() { FlsFree(slot_); }
    ThreadState(const ThreadState&) = delete;
    ThreadState& operator=(const ThreadState&) = delete;
    T& get() {
        auto* value = static_cast<T*>(FlsGetValue(slot_));
        if (!value) {
            auto owned = std::make_unique<T>();
            if (!FlsSetValue(slot_, owned.get())) throw std::runtime_error("Cannot store thread state");
            value = owned.release();
        }
        return *value;
    }
};
class ThreadValue {
    DWORD slot_{TlsAlloc()};
public:
    ThreadValue() { if (slot_ == TLS_OUT_OF_INDEXES) throw std::runtime_error("Cannot allocate thread value"); }
    ~ThreadValue() { TlsFree(slot_); }
    ThreadValue(const ThreadValue&) = delete;
    ThreadValue& operator=(const ThreadValue&) = delete;
    operator std::uintptr_t() const noexcept { return reinterpret_cast<std::uintptr_t>(TlsGetValue(slot_)); }
    ThreadValue& operator=(std::uintptr_t value) noexcept {
        if (!TlsSetValue(slot_, reinterpret_cast<void*>(value))) std::terminate();
        return *this;
    }
};
}
