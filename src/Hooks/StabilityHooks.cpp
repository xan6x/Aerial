#include <Windows.h>

#include "Hooks/HookRegistry.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Memory.h"

namespace aerial::hooks {
namespace {

namespace func = offsets::func;

Detour<void(__fastcall*)(void*, void*, char)> g_appendTextRuns;

int accessViolationFilter(unsigned int code) {
    return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

void __fastcall onAppendTextRuns(void* self, void* element, char flag) {
    __try {
        g_appendTextRuns.call(self, element, flag);
    } __except (accessViolationFilter(GetExceptionCode())) {
    }
}

bool install() {
    g_appendTextRuns.attach("TextRunBuilder::appendElement",
                            memory::rva(func::TextRunBuilder_appendElement), &onAppendTextRuns);
    return true;
}

const Installer g_installer{"Stability", &install};

}
}
