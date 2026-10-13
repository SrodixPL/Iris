#include "core/log.hpp"
#include "core/module.hpp"
#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <cstdio>
namespace iris {
namespace {
#ifdef IRIS_DEBUG
HANDLE console = INVALID_HANDLE_VALUE;
#endif
std::mutex log_mutex;
}
void initialize_logging() noexcept {
#ifdef IRIS_DEBUG
    if (AllocConsole()) SetConsoleTitleW(L"Iris Debug");
    console = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr, OPEN_EXISTING, 0, nullptr);
#endif
}
void overlay_log(const char* message) noexcept {
    try {
        std::lock_guard lock(log_mutex);
        SYSTEMTIME time{};
        GetLocalTime(&time);
        char line[2048]{};
        const int length = std::snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u] [pid:%lu tid:%lu] %s\r\n",
            time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
            GetCurrentProcessId(), GetCurrentThreadId(), message);
        if (length < 0) return;
        const auto count = static_cast<DWORD>((std::min)(length, static_cast<int>(sizeof(line) - 1)));
        OutputDebugStringA(line);
        DWORD written{};
#ifdef IRIS_DEBUG
        if (console != INVALID_HANDLE_VALUE) WriteFile(console, line, count, &written, nullptr);
#endif
        const auto directory = module::data_folder() / L"logs";
        std::filesystem::create_directories(directory);
        const auto path = directory / (L"iris-" + std::to_wstring(GetCurrentProcessId()) + L".log");
        const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        WriteFile(file, line, count, &written, nullptr);
        CloseHandle(file);
    } catch (...) { OutputDebugStringA(message); }
}
}
