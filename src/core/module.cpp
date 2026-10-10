#include "core/module.hpp"
#include <stdexcept>
namespace iris::module {
namespace { HMODULE image{}; }
void attach(HMODULE instance) noexcept { image = instance; }
HMODULE base() noexcept { return image; }
bool owns(const void* address) noexcept {
    if (!image) return false;
    const auto* bytes = reinterpret_cast<const BYTE*>(image);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(bytes + dos->e_lfanew);
    const auto pointer = reinterpret_cast<ULONG_PTR>(address);
    const auto start = reinterpret_cast<ULONG_PTR>(image);
    return pointer >= start && pointer - start < nt->OptionalHeader.SizeOfImage;
}
bool pin() noexcept {
    HMODULE owner{};
    const auto address = reinterpret_cast<LPCWSTR>(&pin);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           address, &owner)) return true;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                             address, &owner) != FALSE;
}
std::filesystem::path folder() {
    wchar_t path[32768]{};
    HMODULE owner = image;
    if (!owner) GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR>(&folder), &owner);
    if (owner) {
        const auto length = GetModuleFileNameW(owner, path, static_cast<DWORD>(std::size(path)));
        if (length && length < std::size(path)) return std::filesystem::path(path).parent_path();
    }
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", path, static_cast<DWORD>(std::size(path)));
    if (!length || length >= std::size(path)) throw std::runtime_error("Cannot resolve Iris data directory");
    return std::filesystem::path(path) / L"Iris";
}
}
