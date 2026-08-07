#include "Utils/Memory.h"

#include <Windows.h>
#include <Psapi.h>
#include <vector>

#include "Utils/Logger.h"

namespace aerial::memory {
namespace {

struct ModuleInfo {
    uintptr_t base = 0;
    size_t size = 0;
};

const ModuleInfo& moduleInfo() {
    static ModuleInfo info = [] {
        ModuleInfo result{};
        const HMODULE module = GetModuleHandleW(nullptr);
        MODULEINFO mi{};
        if (module && GetModuleInformation(GetCurrentProcess(), module, &mi, sizeof(mi))) {
            result.base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
            result.size = mi.SizeOfImage;
        }
        return result;
    }();
    return info;
}

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
};

Pattern parsePattern(std::string_view text) {
    Pattern pattern;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ') {
            ++i;
            continue;
        }
        if (text[i] == '?') {
            pattern.bytes.push_back(0);
            pattern.mask.push_back(false);
            while (i < text.size() && text[i] == '?')
                ++i;
            continue;
        }
        uint8_t value = 0;
        int digits = 0;
        while (i < text.size() && digits < 2) {
            const char c = text[i];
            uint8_t nibble;
            if (c >= '0' && c <= '9') nibble = static_cast<uint8_t>(c - '0');
            else if (c >= 'a' && c <= 'f') nibble = static_cast<uint8_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') nibble = static_cast<uint8_t>(c - 'A' + 10);
            else break;
            value = static_cast<uint8_t>((value << 4) | nibble);
            ++i;
            ++digits;
        }
        if (digits == 0) {
            ++i;
            continue;
        }
        pattern.bytes.push_back(value);
        pattern.mask.push_back(true);
    }
    return pattern;
}

struct Section {
    uintptr_t start;
    size_t size;
};

const std::vector<Section>& executableSections() {
    static std::vector<Section> sections = [] {
        std::vector<Section> result;
        const auto& info = moduleInfo();
        if (!info.base)
            return result;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(info.base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return result;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(info.base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return result;

        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE))
                continue;
            result.push_back({info.base + section->VirtualAddress, section->Misc.VirtualSize});
        }
        return result;
    }();
    return sections;
}

}

uintptr_t base() { return moduleInfo().base; }

size_t imageSize() { return moduleInfo().size; }

uintptr_t findPattern(std::string_view text) {
    const Pattern pattern = parsePattern(text);
    if (pattern.bytes.empty())
        return 0;

    for (const auto& section : executableSections()) {
        if (section.size < pattern.bytes.size())
            continue;
        const auto* data = reinterpret_cast<const uint8_t*>(section.start);
        const size_t last = section.size - pattern.bytes.size();
        for (size_t i = 0; i <= last; ++i) {
            bool matched = true;
            for (size_t j = 0; j < pattern.bytes.size(); ++j) {
                if (pattern.mask[j] && data[i + j] != pattern.bytes[j]) {
                    matched = false;
                    break;
                }
            }
            if (matched)
                return section.start + i;
        }
    }

    LOG_WARN("Memory", "pattern not found: {}", text);
    return 0;
}

uintptr_t findPatternRva(std::string_view text) {
    const uintptr_t address = findPattern(text);
    return address ? address - base() : 0;
}

uintptr_t resolveRip(uintptr_t address, int offset, int instructionSize) {
    if (!isReadable(reinterpret_cast<const void*>(address), instructionSize))
        return 0;
    const auto displacement = *reinterpret_cast<const int32_t*>(address + offset);
    return address + instructionSize + displacement;
}

uintptr_t followCall(uintptr_t address) {
    if (!isReadable(reinterpret_cast<const void*>(address), 5))
        return 0;
    const auto opcode = *reinterpret_cast<const uint8_t*>(address);
    if (opcode != 0xE8 && opcode != 0xE9) {
        LOG_WARN("Memory", "followCall: {:#x} is not a near call/jmp (opcode {:#04x})", address, opcode);
        return 0;
    }
    return resolveRip(address, 1, 5);
}

void* chain(void* start, std::initializer_list<ptrdiff_t> offsets) {
    auto* current = static_cast<uint8_t*>(start);
    for (const ptrdiff_t offset : offsets) {
        if (!isReadable(current, sizeof(void*)))
            return nullptr;
        current = *reinterpret_cast<uint8_t**>(current + offset);
    }
    return current;
}

bool isReadable(const void* address, size_t size) {
    if (!address)
        return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(address, &mbi, sizeof(mbi)))
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;

    constexpr DWORD kNoRead = PAGE_NOACCESS | PAGE_GUARD;
    if (mbi.Protect & kNoRead)
        return false;

    const auto start = reinterpret_cast<uintptr_t>(address);
    const auto regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return start + size <= regionEnd;
}

bool patch(uintptr_t address, const void* bytes, size_t size) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    std::memcpy(reinterpret_cast<void*>(address), bytes, size);
    VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
    return true;
}

bool nop(uintptr_t address, size_t size) {
    const std::vector<uint8_t> nops(size, 0x90);
    return patch(address, nops.data(), size);
}

}
