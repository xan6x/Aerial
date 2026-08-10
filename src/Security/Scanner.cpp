#include "Security/Scanner.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <intrin.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Utils/Hook.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial::security {
namespace {

constexpr ULONG kThreadStartAddress = 9;

std::set<uintptr_t> g_baselineExecPriv;
std::set<uintptr_t> g_reportedRegions;
std::set<uintptr_t> g_reportedThreads;
std::set<std::wstring> g_reportedModules;
std::set<DWORD> g_suspendedThreads;

std::vector<uint8_t> g_cleanSelfText;
uintptr_t g_cleanSelfTextVa = 0;
size_t g_cleanSelfTextSize = 0;
bool g_cleanSelfTried = false;

std::vector<uint8_t> g_cleanText;
uintptr_t g_cleanTextVa = 0;
size_t g_cleanTextSize = 0;
bool g_cleanTextTried = false;

constexpr int kHookKillThreshold = 3;
constexpr uint64_t kHeavyEvery = 5;

uint64_t g_xorKey = 0;

inline uint8_t keyByte(size_t i) {
    return reinterpret_cast<const uint8_t*>(&g_xorKey)[i & 7];
}

void encryptCache(std::vector<uint8_t>& data) {
    for (size_t i = 0; i < data.size(); ++i)
        data[i] ^= keyByte(i);
}

struct ModuleRange {
    uintptr_t base = 0;
    uintptr_t end = 0;
    std::wstring path;
};

struct Range {
    uintptr_t base = 0;
    uintptr_t end = 0;
};

bool inRanges(uintptr_t address, const std::vector<Range>& ranges) {
    for (const Range& range : ranges)
        if (address >= range.base && address < range.end)
            return true;
    return false;
}

[[maybe_unused]] std::string narrow(const std::wstring& wide) {
    if (wide.empty())
        return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), bytes,
                        nullptr, nullptr);
    return out;
}

std::wstring toLower(std::wstring value) {
    for (wchar_t& c : value)
        c = static_cast<wchar_t>(::towlower(c));
    return value;
}

std::vector<ModuleRange> collectModules() {
    std::vector<ModuleRange> out;

    const HANDLE snapshot =
        CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
        return out;

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            ModuleRange range;
            range.base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
            range.end = range.base + entry.modBaseSize;
            range.path = entry.szExePath;
            out.push_back(std::move(range));
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return out;
}

bool inAnyModule(uintptr_t address, const std::vector<ModuleRange>& modules) {
    for (const ModuleRange& module : modules)
        if (address >= module.base && address < module.end)
            return true;
    return false;
}

bool fileExists(const std::wstring& path) {
    if (path.empty())
        return false;
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool suspiciousPath(const std::wstring& path) {
    if (path.empty())
        return true;
    const std::wstring lower = toLower(path);
    return lower.find(L"\\temp\\") != std::wstring::npos ||
           lower.find(L"\\downloads\\") != std::wstring::npos ||
           lower.find(L"\\desktop\\") != std::wstring::npos;
}

uint64_t fnv1a(const uint8_t* data, size_t length) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < length; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

using NtQueryInformationThread_t = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

NtQueryInformationThread_t queryThread() {
    static const auto fn = reinterpret_cast<NtQueryInformationThread_t>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    return fn;
}

std::vector<uint8_t> readFile(const wchar_t* path) {
    std::vector<uint8_t> out;

    const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return out;

    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (1LL << 30)) {
        out.resize(static_cast<size_t>(size.QuadPart));
        size_t offset = 0;
        while (offset < out.size()) {
            const DWORD want =
                static_cast<DWORD>(std::min<size_t>(1u << 20, out.size() - offset));
            DWORD got = 0;
            if (!ReadFile(file, out.data() + offset, want, &got, nullptr) || got == 0) {
                out.clear();
                break;
            }
            offset += got;
        }
    }
    CloseHandle(file);
    return out;
}

uint32_t rvaToRaw(const IMAGE_NT_HEADERS64* nt, uint32_t rva) {
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const uint32_t start = section[i].VirtualAddress;
        const uint32_t raw = section[i].SizeOfRawData;
        const uint32_t virt = section[i].Misc.VirtualSize;
        const uint32_t span = raw > virt ? raw : virt;
        if (rva >= start && rva < start + span)
            return section[i].PointerToRawData + (rva - start);
    }
    return 0;
}

bool loadRelocatedText(HMODULE mod, uintptr_t loadedBase, std::vector<uint8_t>& outText,
                       uintptr_t& outVa, size_t& outSize) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(mod, path, MAX_PATH))
        return false;

    const std::vector<uint8_t> bytes = readFile(path);
    if (bytes.size() < sizeof(IMAGE_DOS_HEADER))
        return false;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    const auto* section = IMAGE_FIRST_SECTION(nt);
    const IMAGE_SECTION_HEADER* text = nullptr;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::memcmp(section[i].Name, ".text", 5) == 0) {
            text = &section[i];
            break;
        }
    }
    if (!text)
        return false;

    const size_t raw = text->SizeOfRawData;
    const size_t virt = text->Misc.VirtualSize;
    const size_t size = raw < virt ? raw : virt;
    if (size == 0 || text->PointerToRawData + size > bytes.size())
        return false;

    std::vector<uint8_t> buffer(bytes.begin() + text->PointerToRawData,
                                bytes.begin() + text->PointerToRawData + size);

    const int64_t delta =
        static_cast<int64_t>(loadedBase) - static_cast<int64_t>(nt->OptionalHeader.ImageBase);
    const auto& reloc = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (delta != 0 && reloc.VirtualAddress && reloc.Size) {
        const uint32_t rawOffset = rvaToRaw(nt, reloc.VirtualAddress);
        if (rawOffset && rawOffset + reloc.Size <= bytes.size()) {
            const uint8_t* cursor = bytes.data() + rawOffset;
            const uint8_t* stop = cursor + reloc.Size;
            const uint32_t textStart = text->VirtualAddress;
            const uint32_t textEnd = textStart + static_cast<uint32_t>(size);
            while (cursor + sizeof(IMAGE_BASE_RELOCATION) <= stop) {
                const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(cursor);
                if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION))
                    break;
                const uint32_t count =
                    (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                const auto* entries =
                    reinterpret_cast<const WORD*>(cursor + sizeof(IMAGE_BASE_RELOCATION));
                for (uint32_t i = 0; i < count; ++i) {
                    if ((entries[i] >> 12) != IMAGE_REL_BASED_DIR64)
                        continue;
                    const uint32_t targetRva = block->VirtualAddress + (entries[i] & 0x0FFF);
                    if (targetRva < textStart || targetRva + sizeof(uint64_t) > textEnd)
                        continue;
                    const size_t index = targetRva - textStart;
                    uint64_t value = 0;
                    std::memcpy(&value, buffer.data() + index, sizeof(value));
                    value += static_cast<uint64_t>(delta);
                    std::memcpy(buffer.data() + index, &value, sizeof(value));
                }
                cursor += block->SizeOfBlock;
            }
        }
    }

    outVa = text->VirtualAddress;
    outSize = size;
    outText = std::move(buffer);
    return true;
}

bool ensureCleanSelfText(uintptr_t selfBase) {
    if (g_cleanSelfTried)
        return !g_cleanSelfText.empty();
    g_cleanSelfTried = true;
    if (!loadRelocatedText(reinterpret_cast<HMODULE>(selfBase), selfBase, g_cleanSelfText,
                           g_cleanSelfTextVa, g_cleanSelfTextSize))
        return false;
    encryptCache(g_cleanSelfText);
    return true;
}

bool ensureCleanText(uintptr_t gameBase) {
    if (g_cleanTextTried)
        return !g_cleanText.empty();
    g_cleanTextTried = true;
    if (!loadRelocatedText(GetModuleHandleW(nullptr), gameBase, g_cleanText, g_cleanTextVa,
                           g_cleanTextSize))
        return false;
    encryptCache(g_cleanText);
    return true;
}

uintptr_t decodeJump(uintptr_t address, const uint8_t* p) {
    if (p[0] == 0xE9 || p[0] == 0xE8) {
        int32_t rel;
        std::memcpy(&rel, p + 1, sizeof(rel));
        return address + 5 + static_cast<intptr_t>(rel);
    }
    if (p[0] == 0xFF && p[1] == 0x25) {
        int32_t disp;
        std::memcpy(&disp, p + 2, sizeof(disp));
        const uintptr_t slot = address + 6 + static_cast<intptr_t>(disp);
        if (!memory::isReadable(reinterpret_cast<void*>(slot), sizeof(uintptr_t)))
            return 0;
        uintptr_t target;
        std::memcpy(&target, reinterpret_cast<void*>(slot), sizeof(target));
        return target;
    }
    if (p[0] == 0x48 && p[1] == 0xB8 && p[10] == 0xFF && p[11] == 0xE0) {
        uintptr_t target;
        std::memcpy(&target, p + 2, sizeof(target));
        return target;
    }
    return 0;
}

int countForeignHooks(const std::vector<ModuleRange>& modules,
                      const std::vector<uintptr_t>& ownTargets, uintptr_t gameBase) {
    if (!ensureCleanText(gameBase))
        return 0;

    const uint8_t* disk = g_cleanText.data();
    const auto* mem = reinterpret_cast<const uint8_t*>(gameBase + g_cleanTextVa);
    const size_t size = g_cleanTextSize;

    int found = 0;
    for (size_t i = 0; i + 16 < size;) {
        if (mem[i] == static_cast<uint8_t>(disk[i] ^ keyByte(i))) {
            ++i;
            continue;
        }

        const uintptr_t addr = reinterpret_cast<uintptr_t>(mem + i);

        bool own = false;
        for (const uintptr_t target : ownTargets) {
            if (addr >= target && addr < target + 16) {
                own = true;
                break;
            }
        }
        if (own) {
            i += 16;
            continue;
        }

        const uintptr_t target = decodeJump(addr, mem + i);
        if (target && !inAnyModule(target, modules)) {
            ++found;
            i += 16;
        } else {
            ++i;
        }
    }
    return found;
}

int syscallNumber(const char* name) {
    wchar_t dir[MAX_PATH]{};
    if (!GetSystemDirectoryW(dir, MAX_PATH))
        return -1;
    std::wstring path = std::wstring(dir) + L"\\ntdll.dll";

    const std::vector<uint8_t> bytes = readFile(path.c_str());
    if (bytes.size() < sizeof(IMAGE_DOS_HEADER))
        return -1;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return -1;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return -1;

    const auto& dir32 = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const uint32_t expOff = rvaToRaw(nt, dir32.VirtualAddress);
    if (!expOff)
        return -1;
    const auto* ed = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(bytes.data() + expOff);

    const uint32_t namesOff = rvaToRaw(nt, ed->AddressOfNames);
    const uint32_t ordsOff = rvaToRaw(nt, ed->AddressOfNameOrdinals);
    const uint32_t funcsOff = rvaToRaw(nt, ed->AddressOfFunctions);
    if (!namesOff || !ordsOff || !funcsOff)
        return -1;
    const auto* names = reinterpret_cast<const uint32_t*>(bytes.data() + namesOff);
    const auto* ords = reinterpret_cast<const uint16_t*>(bytes.data() + ordsOff);
    const auto* funcs = reinterpret_cast<const uint32_t*>(bytes.data() + funcsOff);

    for (uint32_t i = 0; i < ed->NumberOfNames; ++i) {
        const uint32_t nameOff = rvaToRaw(nt, names[i]);
        if (!nameOff)
            continue;
        if (std::strcmp(reinterpret_cast<const char*>(bytes.data() + nameOff), name) != 0)
            continue;

        const uint32_t funcOff = rvaToRaw(nt, funcs[ords[i]]);
        if (!funcOff || funcOff + 8 > bytes.size())
            return -1;
        const uint8_t* stub = bytes.data() + funcOff;
        if (stub[0] == 0x4C && stub[1] == 0x8B && stub[2] == 0xD1 && stub[3] == 0xB8) {
            int ssn;
            std::memcpy(&ssn, stub + 4, sizeof(ssn));
            return ssn;
        }
        return -1;
    }
    return -1;
}

void* buildSyscallStub(int ssn) {
    uint8_t code[] = {0x4C, 0x8B, 0xD1, 0xB8, 0, 0, 0, 0, 0x0F, 0x05, 0xC3};
    std::memcpy(code + 4, &ssn, sizeof(ssn));
    void* mem = VirtualAlloc(nullptr, sizeof(code), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem)
        return nullptr;
    std::memcpy(mem, code, sizeof(code));
    return mem;
}

[[noreturn]] void terminateSelf() {
    const LONG code = static_cast<LONG>(0xA1E17ACC);

    static void* const stub = [] {
        const int ssn = syscallNumber("NtTerminateProcess");
        return ssn >= 0 ? buildSyscallStub(ssn) : nullptr;
    }();
    if (stub) {
        auto fn = reinterpret_cast<LONG(NTAPI*)(HANDLE, LONG)>(stub);
        fn(reinterpret_cast<HANDLE>(-1), code);
    }

    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
    __fastfail(7);
}

bool debuggerPresent() {
    const auto* peb = reinterpret_cast<const uint8_t*>(__readgsqword(0x60));
    if (peb && peb[0x02] != 0)
        return true;

    using NtQIP_t = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static const auto query = reinterpret_cast<NtQIP_t>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    if (query) {
        const HANDLE self = reinterpret_cast<HANDLE>(-1);
        HANDLE port = nullptr;
        if (query(self, 7, &port, sizeof(port), nullptr) == 0 && port)
            return true;
        HANDLE object = nullptr;
        if (query(self, 30, &object, sizeof(object), nullptr) == 0 && object)
            return true;
    }

    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(GetCurrentThread(), &ctx) &&
        (ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3))
        return true;

    return false;
}

bool moduleLinked(uintptr_t base) {
    const auto* peb = reinterpret_cast<const uint8_t*>(__readgsqword(0x60));
    if (!peb)
        return true;
    const auto* ldr = *reinterpret_cast<const uint8_t* const*>(peb + 0x18);
    if (!ldr)
        return true;

    const auto* head = reinterpret_cast<const LIST_ENTRY*>(ldr + 0x10);
    const LIST_ENTRY* node = head->Flink;
    for (int guard = 0; node && node != head && guard < 4096; ++guard, node = node->Flink) {
        const auto* entry = reinterpret_cast<const uint8_t*>(node);
        const auto dllBase = *reinterpret_cast<const uintptr_t*>(entry + 0x30);
        if (dllBase == base)
            return true;
    }
    return false;
}

bool iatHooked(uintptr_t base, const std::vector<ModuleRange>& modules) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return false;

    for (auto* imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
         imp->Name; ++imp) {
        auto* thunk = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + imp->FirstThunk);
        for (; thunk->u1.Function; ++thunk) {
            const uintptr_t target = thunk->u1.Function;
            if (inAnyModule(target, modules))
                continue;

            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<void*>(target), &mbi, sizeof(mbi)) == sizeof(mbi) &&
                mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE)
                return true;
        }
    }
    return false;
}

bool looksLikePatch(const uint8_t* run, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        const uint8_t b = run[i];
        if (b == 0xE9 || b == 0xE8 || b == 0xEB || b == 0xC3 || b == 0xC2 || b == 0xCC ||
            b == 0x90 || b == 0xFF || (b >= 0x70 && b <= 0x7F))
            return true;
        if (b == 0x0F && i + 1 < length && run[i + 1] >= 0x80 && run[i + 1] <= 0x8F)
            return true;
    }
    return false;
}

void freezeOtherThreads(std::vector<HANDLE>& out) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return;

    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();

    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self)
                continue;
            const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
            if (!thread)
                continue;
            if (SuspendThread(thread) != static_cast<DWORD>(-1))
                out.push_back(thread);
            else
                CloseHandle(thread);
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

void resumeThreads(std::vector<HANDLE>& handles) {
    for (const HANDLE thread : handles) {
        ResumeThread(thread);
        CloseHandle(thread);
    }
    handles.clear();
}

int applyReverts(uint8_t* mem, const uint8_t* clean,
                 const std::vector<std::pair<size_t, size_t>>& runs) {
    if (runs.empty())
        return 0;

    std::vector<HANDLE> frozen;
    freezeOtherThreads(frozen);
    for (const auto& run : runs)
        memory::patch(reinterpret_cast<uintptr_t>(mem + run.first), clean + run.first, run.second);
    resumeThreads(frozen);
    return static_cast<int>(runs.size());
}

int healSelfText(uintptr_t selfBase, uintptr_t textBase, size_t textSize) {
    if (!textBase || !textSize || !ensureCleanSelfText(selfBase))
        return 0;

    auto* mem = reinterpret_cast<uint8_t*>(textBase);
    const size_t size = std::min(textSize, g_cleanSelfTextSize);

    std::vector<uint8_t> plain(size);
    for (size_t i = 0; i < size; ++i)
        plain[i] = static_cast<uint8_t>(g_cleanSelfText[i] ^ keyByte(i));
    const uint8_t* clean = plain.data();

    std::vector<std::pair<size_t, size_t>> runs;
    for (size_t i = 0; i < size;) {
        if (mem[i] == clean[i]) {
            ++i;
            continue;
        }

        size_t j = i;
        while (j < size && mem[j] != clean[j] && (j - i) < 64)
            ++j;

        if (looksLikePatch(mem + i, j - i))
            runs.emplace_back(i, j - i);
        i = j;
    }
    return applyReverts(mem, clean, runs);
}

int neutralizeThreads(const std::vector<Range>& cheat, const std::vector<ModuleRange>& modules,
                      uintptr_t gameBase, uintptr_t gameEnd, uintptr_t selfBase, uintptr_t selfEnd) {
    const auto ntQuery = queryThread();

    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    int suspended = 0;

    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self)
                continue;
            if (g_suspendedThreads.count(entry.th32ThreadID))
                continue;

            const HANDLE thread = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
            if (!thread)
                continue;

            uintptr_t start = 0;
            if (ntQuery)
                ntQuery(thread, kThreadStartAddress, &start, sizeof(start), nullptr);

            const bool startTrusted =
                (start >= gameBase && start < gameEnd) || (start >= selfBase && start < selfEnd);
            const bool startUnbacked = start && !startTrusted && !inAnyModule(start, modules);
            const bool startInCheat = inRanges(start, cheat);

            if (startUnbacked && g_reportedThreads.insert(start).second)
                LOG_DEBUG("Security",
                          "thread {} starts in unbacked memory {:#x} (possible injected code)",
                          entry.th32ThreadID, start);

            if (!startUnbacked && !startInCheat) {
                CloseHandle(thread);
                continue;
            }

            const char* reason = startUnbacked ? "unbacked start" : "start in cheat memory";
            if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
                g_suspendedThreads.insert(entry.th32ThreadID);
                ++suspended;
                LOG_DEBUG("Security", "neutralised cheat thread {} ({}, start {:#x})",
                          entry.th32ThreadID, reason, start);
            }
            CloseHandle(thread);
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return suspended;
}

}

Scanner& Scanner::get() {
    static Scanner instance;
    return instance;
}

void Scanner::init(void* selfModule) {
    m_selfBase = reinterpret_cast<uintptr_t>(selfModule);
    m_gameBase = memory::base();
    m_gameEnd = m_gameBase + memory::imageSize();

    if (m_selfBase) {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(m_selfBase);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            const auto* nt =
                reinterpret_cast<const IMAGE_NT_HEADERS64*>(m_selfBase + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE) {
                m_selfEnd = m_selfBase + nt->OptionalHeader.SizeOfImage;

                const auto* section = IMAGE_FIRST_SECTION(nt);
                for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
                    if (std::memcmp(section[i].Name, ".text", 5) == 0) {
                        m_textBase = m_selfBase + section[i].VirtualAddress;
                        m_textSize = section[i].Misc.VirtualSize;
                        break;
                    }
                }
            }
        }
    }

    if (m_textBase && m_textSize)
        m_textHash = fnv1a(reinterpret_cast<const uint8_t*>(m_textBase), m_textSize);

    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t address = 0;
    while (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        const uintptr_t region = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const bool exec =
            mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                            PAGE_EXECUTE_WRITECOPY)) != 0;
        if (exec && mbi.Type == MEM_PRIVATE)
            g_baselineExecPriv.insert(region);

        const uintptr_t next = region + mbi.RegionSize;
        if (next <= address)
            break;
        address = next;
    }

    g_xorKey = (static_cast<uint64_t>(__rdtsc()) << 1) | 1;
    m_loaderLinked = moduleLinked(m_selfBase);

    m_initialised = true;
    LOG_DEBUG("Security",
              "anti-tamper armed: self {:#x}-{:#x} game {:#x}-{:#x} baseline exec-private regions {}",
              m_selfBase, m_selfEnd, m_gameBase, m_gameEnd, g_baselineExecPriv.size());
}

void Scanner::scan() {
    if (!m_initialised)
        return;

    const std::lock_guard<std::mutex> lock(m_scanMutex);
    m_heartbeat.fetch_add(1, std::memory_order_relaxed);

    static int unlinkStrikes = 0;
    static int iatStrikes = 0;

    if (debuggerPresent()) {
        LOG_DEBUG("Security", "debugger detected - terminating process");
        Sleep(50);
        terminateSelf();
    }

    if (m_loaderLinked && !moduleLinked(m_selfBase)) {
        if (++unlinkStrikes >= 2) {
            LOG_DEBUG("Security", "client module unlinked from loader list - terminating process");
            Sleep(50);
            terminateSelf();
        }
    } else {
        unlinkStrikes = 0;
    }

    const std::vector<ModuleRange> modules = collectModules();

    if (iatHooked(m_selfBase, modules)) {
        if (++iatStrikes >= 2) {
            LOG_DEBUG("Security", "client IAT redirected to private memory - terminating process");
            Sleep(50);
            terminateSelf();
        }
    } else {
        iatStrikes = 0;
    }

    std::vector<Range> cheat;
    bool escalate = false;

    for (const ModuleRange& module : modules) {
        if (module.base == m_selfBase || module.base == m_gameBase)
            continue;
        const bool backed = fileExists(module.path);
        const bool suspicious = suspiciousPath(module.path);
        if (backed && !suspicious)
            continue;

        if (g_reportedModules.insert(module.path).second) {
            LOG_DEBUG("Security", "suspicious module {:#x} '{}' ({})", module.base,
                      narrow(module.path), backed ? "user-writable path" : "no file on disk");
            escalate = true;
        }

        cheat.push_back({module.base, module.end});
    }

    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t address = 0;
    while (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        const uintptr_t region = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const bool exec =
            mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                            PAGE_EXECUTE_WRITECOPY)) != 0;
        if (exec && mbi.Type == MEM_PRIVATE && !g_baselineExecPriv.count(region)) {
            cheat.push_back({region, region + mbi.RegionSize});
            if (g_reportedRegions.insert(region).second) {
                LOG_DEBUG("Security",
                          "new executable private region {:#x} size {:#x} prot {:#x} (injected "
                          "after load)",
                          region, mbi.RegionSize, mbi.Protect);
                escalate = true;
            }
        }

        const uintptr_t next = region + mbi.RegionSize;
        if (next <= address)
            break;
        address = next;
    }

    if (!cheat.empty()) {
        const int neutralised =
            neutralizeThreads(cheat, modules, m_gameBase, m_gameEnd, m_selfBase, m_selfEnd);
        if (neutralised > 0)
            LOG_DEBUG("Security", "neutralised {} cheat thread(s)", neutralised);
    }

    if (escalate)
        m_escalateTicks = 6;

    const bool heavy =
        m_escalateTicks > 0 || (m_heartbeat.load(std::memory_order_relaxed) % kHeavyEvery == 0);
    if (m_escalateTicks > 0)
        --m_escalateTicks;
    if (!heavy)
        return;

    const std::vector<uintptr_t> ownTargets = HookManager::get().hookedTargets();
    int foreignHooks = countForeignHooks(modules, ownTargets, m_gameBase);

    for (int probe = 0; foreignHooks < kHookKillThreshold && escalate && probe < 8; ++probe) {
        Sleep(250);
        foreignHooks = countForeignHooks(modules, ownTargets, m_gameBase);
    }

    if (foreignHooks >= kHookKillThreshold) {
        LOG_DEBUG("Security",
                  "cheat confirmed: {} foreign inline hook(s) in game .text - terminating process",
                  foreignHooks);
        Sleep(50);
        terminateSelf();
    }
    if (foreignHooks > 0)
        LOG_DEBUG("Security", "detected {} foreign inline hook(s) in game .text", foreignHooks);

    if (m_textBase && m_textSize) {
        uint64_t hash = fnv1a(reinterpret_cast<const uint8_t*>(m_textBase), m_textSize);
        if (hash != m_textHash) {
            const int healed = healSelfText(m_selfBase, m_textBase, m_textSize);
            if (healed > 0) {
                LOG_DEBUG("Security", "client .text tamper healed: {} patch(es) reverted", healed);
                hash = fnv1a(reinterpret_cast<const uint8_t*>(m_textBase), m_textSize);
            }

            if (hash != m_textHash) {
                if (!m_textReported) {
                    m_textReported = true;
                    LOG_DEBUG("Security", "client .text was modified: hash {:#x} != baseline {:#x}",
                              hash, m_textHash);
                }
            } else {
                m_textReported = false;
            }
        }
    }
}

}
