#include "Utils/Guard.h"

#include <Windows.h>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial {
namespace {

constexpr int kMaxReportsPerSite = 3;

void reportFault(const char* what, unsigned long code, void* address) {
    static std::mutex mutex;
    static std::unordered_map<std::string, int> counts;

    std::lock_guard lock(mutex);
    int& seen = counts[what];
    if (seen >= kMaxReportsPerSite)
        return;
    ++seen;

    const auto base = memory::base();
    const auto rva = reinterpret_cast<uintptr_t>(address);
    const bool inGame = base && rva >= base && rva < base + memory::imageSize();

    LOG_ERROR("Guard", "{} faulted: code {:#x} at {} ({})", what, code, address,
              inGame ? std::format("Minecraft.Windows.exe+{:#x}", rva - base) : "outside the game image");

    if (seen == kMaxReportsPerSite)
        LOG_ERROR("Guard", "{}: further faults will not be reported", what);
}

struct FaultInfo {
    unsigned long code = 0;
    void* address = nullptr;
};

int captureFault(EXCEPTION_POINTERS* info, FaultInfo& out) {
    out.code = info->ExceptionRecord->ExceptionCode;
    out.address = info->ExceptionRecord->ExceptionAddress;
    return EXCEPTION_EXECUTE_HANDLER;
}

}

bool runGuarded(const char* what, GuardedFn fn, void* data) {
    FaultInfo fault;

    __try {
        fn(data);
        return true;
    } __except (captureFault(GetExceptionInformation(), fault)) {
        reportFault(what, fault.code, fault.address);
        return false;
    }
}

}
