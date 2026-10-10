#pragma once
#include <Windows.h>
#include <filesystem>
namespace iris::module {
void attach(HMODULE instance) noexcept;
bool owns(const void* address) noexcept;
bool pin() noexcept;
HMODULE base() noexcept;
std::filesystem::path folder();
}
