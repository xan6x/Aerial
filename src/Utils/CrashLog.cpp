#include "Utils/CrashLog.h"

#include <Windows.h>

#include <filesystem>

#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial::crash {
namespace {

PVOID g_handler = nullptr;

// Only the codes that mean "something is genuinely broken". The game raises
// plenty of C++ and debugger exceptions in normal operation, and reporting those
// would bury the one that matters.
bool isFatal(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return true;
    default:
        return false;
    }
}

const char* describe(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION:      return "privileged instruction";
    case EXCEPTION_IN_PAGE_ERROR:         return "in-page error";
    case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "misaligned access";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
    default:                              return "exception";
    }
}

// Module owning `address`, plus the offset into it. An address that belongs to
// no module at all is the interesting case: that is freed memory, and after an
// eject it usually means a stale hook still pointing at where this DLL used to
// be.
struct Owner {
    char name[64] = "<unmapped>";
    uintptr_t offset = 0;
};

Owner ownerOf(const void* address) {
    Owner owner;
    owner.offset = reinterpret_cast<uintptr_t>(address);

    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module) ||
        !module)
        return owner;

    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(module, path, MAX_PATH)) {
        const std::string file = std::filesystem::path(path).filename().string();
        std::snprintf(owner.name, sizeof(owner.name), "%s", file.c_str());
    } else {
        std::snprintf(owner.name, sizeof(owner.name), "%p", static_cast<void*>(module));
    }

    owner.offset = reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module);
    return owner;
}

LONG CALLBACK onException(EXCEPTION_POINTERS* info) {
    if (!info || !info->ExceptionRecord || !isFatal(info->ExceptionRecord->ExceptionCode))
        return EXCEPTION_CONTINUE_SEARCH;

    // Report once. A fault often repeats as the process unwinds, and a hundred
    // identical blocks would push the useful first one out of view.
    static LONG reported = 0;
    if (InterlockedExchange(&reported, 1) != 0)
        return EXCEPTION_CONTINUE_SEARCH;

    const auto* record = info->ExceptionRecord;
    const Owner owner = ownerOf(record->ExceptionAddress);

    LOG_ERROR("Crash", "{} at {} ({}+{:#x}) on thread {}", describe(record->ExceptionCode),
              record->ExceptionAddress, owner.name, owner.offset, GetCurrentThreadId());

    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        static const char* const kOperation[] = {"reading", "writing", "executing"};
        const ULONG_PTR operation = record->ExceptionInformation[0];
        LOG_ERROR("Crash", "  {} address {:#x}",
                  operation <= 8 ? kOperation[operation == 8 ? 2 : operation] : "accessing",
                  static_cast<uintptr_t>(record->ExceptionInformation[1]));
    }

    // Where the fault came from matters as much as where it landed: a stale
    // detour shows up as a game-module return address into an unmapped caller.
    void* frames[12]{};
    const USHORT captured = CaptureStackBackTrace(0, 12, frames, nullptr);
    for (USHORT i = 0; i < captured; ++i) {
        const Owner frame = ownerOf(frames[i]);
        LOG_ERROR("Crash", "  #{} {} ({}+{:#x})", i, frames[i], frame.name, frame.offset);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void install() {
    if (g_handler)
        return;
    // First in the chain, so the game cannot swallow the fault before we see it.
    g_handler = AddVectoredExceptionHandler(1, onException);
}

void remove() {
    if (!g_handler)
        return;
    RemoveVectoredExceptionHandler(g_handler);
    g_handler = nullptr;
}

} // namespace aerial::crash
