#pragma once
#include <Windows.h>
#include <cstdint>
namespace iris::overlay {
DWORD start() noexcept;
void request_stop() noexcept;
std::uint64_t frames() noexcept;
}
