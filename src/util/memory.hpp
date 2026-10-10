#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace iris::memory {
class Allocation {
public:
    explicit Allocation(std::size_t size);
    ~Allocation();
    Allocation(Allocation&& other) noexcept;
    Allocation& operator=(Allocation&& other) noexcept;
    Allocation(const Allocation&) = delete;
    Allocation& operator=(const Allocation&) = delete;
    [[nodiscard]] std::span<std::byte> bytes() const noexcept { return {data_, size_}; }
private:
    std::byte* data_{};
    std::size_t size_{};
};
// A single committed region only; preserves its original page protection.
class Protection {
public:
    Protection(void* address, std::size_t size, DWORD protection);
    ~Protection();
    Protection(const Protection&) = delete;
    Protection& operator=(const Protection&) = delete;
    void restore();
private:
    void* address_{};
    std::size_t size_{};
    DWORD previous_{};
};
// OS-mediated copies within this process. Caller must synchronize lifetimes.
[[nodiscard]] bool read(std::uintptr_t address, std::span<std::byte> destination) noexcept;
[[nodiscard]] bool write(std::uintptr_t address, std::span<const std::byte> source) noexcept;
// For synchronized game-thread callbacks only. SEH contains stale pointers;
// direct writes obey page protections without WriteProcessMemory's bypass.
[[nodiscard]] bool read_local(std::uintptr_t address, std::span<std::byte> destination) noexcept;
[[nodiscard]] bool write_local(std::uintptr_t address, std::span<const std::byte> source) noexcept;
template<class T> [[nodiscard]] std::optional<T> read(std::uintptr_t address) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    T value{};
    if (!read(address, std::as_writable_bytes(std::span{&value, std::size_t{1}}))) return {};
    return value;
}
template<class T> [[nodiscard]] bool write(std::uintptr_t address, const T& value) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    return write(address, std::as_bytes(std::span{&value, std::size_t{1}}));
}
template<class T> [[nodiscard]] std::optional<T> read_field(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) return {};
    T result{};
    std::memcpy(&result, bytes.data() + offset, sizeof(T));
    return result;
}
// Holds a loader reference until destruction; name identifies a loaded DLL.
class Module {
public:
    explicit Module(const wchar_t* name);
    ~Module();
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    [[nodiscard]] std::uintptr_t base() const noexcept { return base_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] void* symbol(const char* name) const;
private:
    HMODULE handle_{};
    std::uintptr_t base_{};
    std::size_t size_{};
};
class Pattern {
public:
    // Hex bytes separated by whitespace; ? or ?? matches one byte.
    explicit Pattern(std::string_view signature);
    [[nodiscard]] std::optional<std::size_t> find(std::span<const std::byte> bytes) const noexcept;
private:
    std::vector<std::optional<std::byte>> bytes_;
    std::optional<std::size_t> anchor_;
};
}
