// AerialClient injector.
//
// Minecraft: Windows 10 Edition is a UWP app running in an AppContainer, which
// makes injection differ from a normal Win32 target in one important way: the
// DLL file itself must be readable by the "ALL APPLICATION PACKAGES" group, or
// LoadLibrary inside the sandbox fails with ERROR_ACCESS_DENIED.
//
// Run this elevated.

#include <Windows.h>
#include <AccCtrl.h>
#include <AclAPI.h>
#include <Sddl.h>
#include <TlHelp32.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

constexpr wchar_t kProcessName[] = L"Minecraft.Windows.exe";
constexpr wchar_t kDefaultDll[] = L"AerialClient.dll";

void reportError(const std::wstring& what) {
    const DWORD code = GetLastError();
    std::wcerr << L"[-] " << what << L" failed (error " << code << L")\n";
}

DWORD findProcess(const wchar_t* name) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, name) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return pid;
}

// Adds read+execute for S-1-15-2-1 (ALL APPLICATION PACKAGES) to the DLL.
bool grantAppContainerAccess(const std::wstring& path) {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-15-2-1", &sid)) {
        reportError(L"ConvertStringSidToSid");
        return false;
    }

    PACL oldAcl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    DWORD status = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                                         nullptr, &oldAcl, nullptr, &descriptor);
    if (status != ERROR_SUCCESS) {
        std::wcerr << L"[-] GetNamedSecurityInfo failed (" << status << L")\n";
        LocalFree(sid);
        return false;
    }

    EXPLICIT_ACCESS_W access{};
    access.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    access.Trustee.ptstrName = static_cast<LPWSTR>(sid);

    PACL newAcl = nullptr;
    status = SetEntriesInAclW(1, &access, oldAcl, &newAcl);
    if (status == ERROR_SUCCESS) {
        status = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                                       DACL_SECURITY_INFORMATION, nullptr, nullptr, newAcl, nullptr);
    }

    if (newAcl)
        LocalFree(newAcl);
    if (descriptor)
        LocalFree(descriptor);
    LocalFree(sid);

    if (status != ERROR_SUCCESS) {
        std::wcerr << L"[-] granting ALL APPLICATION PACKAGES access failed (" << status << L")\n";
        return false;
    }
    return true;
}

bool enableDebugPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token))
        return false;

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    const BOOL ok = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr);
    CloseHandle(token);
    return ok && GetLastError() == ERROR_SUCCESS;
}

bool inject(DWORD pid, const std::wstring& dllPath) {
    const HANDLE process =
        OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                        PROCESS_VM_WRITE | PROCESS_VM_READ,
                    FALSE, pid);
    if (!process) {
        reportError(L"OpenProcess");
        return false;
    }

    const size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        reportError(L"VirtualAllocEx");
        CloseHandle(process);
        return false;
    }

    bool ok = WriteProcessMemory(process, remote, dllPath.c_str(), bytes, nullptr) != 0;
    if (!ok)
        reportError(L"WriteProcessMemory");

    if (ok) {
        const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));

        const HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
        if (!thread) {
            reportError(L"CreateRemoteThread");
            ok = false;
        } else {
            WaitForSingleObject(thread, 10000);

            DWORD exitCode = 0;
            GetExitCodeThread(thread, &exitCode);
            CloseHandle(thread);

            if (exitCode == 0) {
                std::wcerr << L"[-] LoadLibraryW returned NULL inside the game - the DLL was rejected "
                              L"(check the ACL step and that the DLL is x64)\n";
                ok = false;
            }
        }
    }

    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    return ok;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::wcout << L"AerialClient injector\n";

    std::filesystem::path dll = argc > 1 ? argv[1] : kDefaultDll;
    if (dll.is_relative()) {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        dll = std::filesystem::path(exePath).parent_path() / dll;
    }

    if (!std::filesystem::exists(dll)) {
        std::wcerr << L"[-] " << dll.wstring() << L" not found\n";
        return 1;
    }

    std::wcout << L"[*] dll: " << dll.wstring() << L"\n";

    if (!grantAppContainerAccess(dll.wstring())) {
        std::wcerr << L"[-] could not grant AppContainer access - run this as administrator\n";
        return 1;
    }
    std::wcout << L"[+] ALL APPLICATION PACKAGES granted read/execute\n";

    enableDebugPrivilege();

    const DWORD pid = findProcess(kProcessName);
    if (!pid) {
        std::wcerr << L"[-] " << kProcessName << L" is not running\n";
        return 1;
    }
    std::wcout << L"[*] target pid: " << pid << L"\n";

    if (!inject(pid, dll.wstring())) {
        std::wcerr << L"[-] injection failed\n";
        return 1;
    }

    std::wcout << L"[+] injected - press Insert in game for the menu, End to unload\n";
    return 0;
}
