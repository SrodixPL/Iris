#pragma once
#include <Windows.h>
#include <cstdio>
#include <string>

namespace iris::startup {
inline std::wstring object_name(const wchar_t* kind, DWORD pid) {
    return std::wstring(L"Local\\Iris.Startup.") + kind + L"." + std::to_wstring(pid);
}
struct Report { DWORD result; char message[2048]; };
inline bool report(DWORD result, const char* message) noexcept {
    const auto name = object_name(L"Report", GetCurrentProcessId());
    HANDLE mapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE, name.c_str());
    if (!mapping) return false;
    auto* view = static_cast<Report*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(Report)));
    if (view) {
        std::snprintf(view->message, sizeof(view->message), "%s", message);
        view->result = result;
        const auto event_name = object_name(L"Ready", GetCurrentProcessId());
        HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name.c_str());
        if (event) { SetEvent(event); CloseHandle(event); }
        UnmapViewOfFile(view);
    }
    CloseHandle(mapping);
    return view != nullptr;
}
inline bool is_game() noexcept {
    wchar_t path[32768]{};
    const auto length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length >= 32768) return false;
    const auto* name = wcsrchr(path, L'\\');
    return _wcsicmp(name ? name + 1 : path, L"cs2.exe") == 0;
}
inline void fail(const char* message) noexcept {
    if (report(1, message)) ExitProcess(1);
    MessageBoxA(nullptr, message, "Iris startup failed", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    ExitProcess(1);
}
}
