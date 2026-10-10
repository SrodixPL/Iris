#include "core/crash.hpp"
#include "core/module.hpp"
#include "core/log.hpp"
#include <Windows.h>
#include <dbghelp.h>
#include <cstdio>

namespace iris::crash {
namespace {
wchar_t dump_path[32768]{};
volatile LONG captured{};
LONG CALLBACK report(EXCEPTION_POINTERS* exception) noexcept {
    const auto code = exception->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
    bool associated = module::owns(exception->ExceptionRecord->ExceptionAddress);
    if (!associated) {
        void* frames[32]{};
        const auto count = CaptureStackBackTrace(0, 32, frames, nullptr);
        unsigned owned{};
        for (USHORT i = 0; i < count; ++i) if (module::owns(frames[i])) ++owned;
        associated = owned > 1; // Exclude this exception handler's own frame.
    }
    if (!associated || InterlockedCompareExchange(&captured, 1, 0) != 0) return EXCEPTION_CONTINUE_SEARCH;
    char message[256]{};
    std::snprintf(message, sizeof(message), "DLL fault: module=%p code=0x%08lX instruction=%p access=%llu address=%p; writing crash dump.",
                  module::base(), code, exception->ExceptionRecord->ExceptionAddress,
                  code == EXCEPTION_ACCESS_VIOLATION ? exception->ExceptionRecord->ExceptionInformation[0] : 0,
                  code == EXCEPTION_ACCESS_VIOLATION ? reinterpret_cast<void*>(exception->ExceptionRecord->ExceptionInformation[1]) : nullptr);
    overlay_log(message);
    HANDLE file = CreateFileW(dump_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                         static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory), &info, nullptr, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
}
void initialize() noexcept {
    try {
        const auto folder = module::folder() / L"logs";
        std::filesystem::create_directories(folder);
        const auto path = folder / (L"crash-" + std::to_wstring(GetCurrentProcessId()) + L".dmp");
        if (wcscpy_s(dump_path, path.c_str()) == 0 && module::pin()) AddVectoredExceptionHandler(1, report);
    } catch (...) {}
}
}
