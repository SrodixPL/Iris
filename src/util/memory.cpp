#include "util/memory.hpp"
#include <Psapi.h>
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace iris::memory {
namespace {
[[noreturn]] void win_error(const char* operation) {
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
}
bool valid_range(std::uintptr_t address, std::size_t size) noexcept {
    return address != 0 && size <= (std::numeric_limits<std::uintptr_t>::max)() - address;
}
}
Allocation::Allocation(std::size_t size) : size_(size) {
    if (!size) throw std::invalid_argument("Allocation size must be nonzero");
    data_ = static_cast<std::byte*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!data_) win_error("VirtualAlloc");
}
Allocation::~Allocation() { if (data_) VirtualFree(data_, 0, MEM_RELEASE); }
Allocation::Allocation(Allocation&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)) {}
Allocation& Allocation::operator=(Allocation&& other) noexcept {
    if (this != &other) {
        if (data_) VirtualFree(data_, 0, MEM_RELEASE);
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}
Protection::Protection(void* address, std::size_t size, DWORD protection) : address_(address), size_(size) {
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    MEMORY_BASIC_INFORMATION info{};
    if (!size || !valid_range(start, size) || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT)
        throw std::invalid_argument("Protection requires a committed, nonempty region");
    const auto offset = start - reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    if (offset > info.RegionSize || size > info.RegionSize - offset)
        throw std::invalid_argument("Protection range spans multiple memory regions");
    if (!VirtualProtect(address, size, protection, &previous_)) win_error("VirtualProtect");
}
Protection::~Protection() {
    if (address_) { DWORD ignored{}; VirtualProtect(address_, size_, previous_, &ignored); }
}
void Protection::restore() {
    if (!address_) return;
    DWORD ignored{};
    if (!VirtualProtect(address_, size_, previous_, &ignored)) win_error("VirtualProtect restore");
    address_ = nullptr;
}
bool read(std::uintptr_t address, std::span<std::byte> destination) noexcept {
    if (destination.empty()) return true;
    if (!valid_range(address, destination.size())) return false;
    SIZE_T copied{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), destination.data(),
                             destination.size(), &copied) && copied == destination.size();
}
bool read_local(std::uintptr_t address, std::span<std::byte> destination) noexcept {
    if(destination.empty())return true;
    if(!valid_range(address,destination.size()))return false;
    __try {std::memcpy(destination.data(),reinterpret_cast<const void*>(address),destination.size());return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool write_local(std::uintptr_t address, std::span<const std::byte> source) noexcept {
    if(source.empty())return true;
    if(!valid_range(address,source.size()))return false;
    __try {std::memcpy(reinterpret_cast<void*>(address),source.data(),source.size());return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool write(std::uintptr_t address, std::span<const std::byte> source) noexcept {
    if (source.empty()) return true;
    if (!valid_range(address, source.size())) return false;
    // WriteProcessMemory may bypass PAGE_READONLY; enforce writable pages first.
    auto cursor = address;
    const auto end = address + source.size();
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD access = info.Protect & 0xff;
        if (access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
            access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) return false;
        const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if (!valid_range(base, info.RegionSize) || base + info.RegionSize <= cursor) return false;
        cursor = (std::min)(end, base + info.RegionSize);
    }
    SIZE_T copied{};
    return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), source.data(),
                              source.size(), &copied) && copied == source.size();
}
Module::Module(const wchar_t* name) {
    if (!name) throw std::invalid_argument("Module name must be nonnull");
    if (!GetModuleHandleExW(0, name, &handle_)) win_error("GetModuleHandleExW");
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), handle_, &info, sizeof(info))) {
        const auto error = GetLastError();
        FreeLibrary(handle_);
        throw std::system_error(static_cast<int>(error), std::system_category(), "GetModuleInformation");
    }
    base_ = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
    size_ = info.SizeOfImage;
}
Module::~Module() { FreeLibrary(handle_); }
void* Module::symbol(const char* name) const {
    if (!name) throw std::invalid_argument("Symbol name must be nonnull");
    const auto result = GetProcAddress(handle_, name);
    if (!result) win_error("GetProcAddress");
    return reinterpret_cast<void*>(result);
}
}
