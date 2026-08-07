#include "Utils/ResourcePacks.h"

#include <Windows.h>

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Utils/Logger.h"

namespace aerial::packs {
namespace {

// The game's data lives under its own package folder, which the AppContainer
// can always read. The family name comes from the running package rather than
// a constant: this build is sideloaded, so its identity is not the one a Store
// install would have.
std::wstring packageFamily() {
    using Fn = LONG(WINAPI*)(UINT32*, PWSTR);
    auto* kernel = GetModuleHandleW(L"kernel32.dll");
    auto* getFamily =
        kernel ? reinterpret_cast<Fn>(GetProcAddress(kernel, "GetCurrentPackageFamilyName")) : nullptr;
    if (!getFamily)
        return {};

    UINT32 length = 0;
    if (getFamily(&length, nullptr) != ERROR_INSUFFICIENT_BUFFER || length == 0)
        return {};

    std::wstring name(length, L'\0');
    if (getFamily(&length, name.data()) != ERROR_SUCCESS)
        return {};

    name.resize(length ? length - 1 : 0);  // drop the terminator
    return name;
}

std::wstring comMojang() {
    wchar_t local[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH))
        return {};

    // Inside the AppContainer, LOCALAPPDATA does not point at the user's
    // AppData\Local at all - it is redirected to the package's own sandbox,
    // ...\Packages\<family>\AC. Appending the usual Packages\<family> to that
    // builds a path one level too deep that has never existed. The package
    // folder is the parent of AC, and LocalState is its sibling.
    std::wstring root(local);
    const std::wstring suffix = L"\\AC";
    if (root.size() > suffix.size() && root.compare(root.size() - suffix.size(), suffix.size(), suffix) == 0)
        root.resize(root.size() - suffix.size());
    else {
        // Not sandboxed - the injector, or a test harness. Take the long way.
        const std::wstring family = packageFamily();
        if (family.empty())
            return {};
        root += L"\\Packages\\" + family;
    }

    return root + L"\\LocalState\\games\\com.mojang\\";
}

std::string readFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return {};

    LARGE_INTEGER size{};
    std::string contents;
    // A manifest is a few hundred bytes; the cap is only there so a corrupt
    // file cannot make this allocate the world.
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 1 << 20) {
        contents.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, contents.data(), static_cast<DWORD>(contents.size()), &read, nullptr))
            read = 0;
        contents.resize(read);
    }

    CloseHandle(file);
    return contents;
}

bool exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Bedrock takes either extension for a texture, and packs in the wild use both.
bool hasTexture(const std::wstring& packDir, const wchar_t* relative) {
    return exists(packDir + relative + L".png") || exists(packDir + relative + L".tga");
}

// Paths here are ASCII in practice; this is for the log, not for opening files.
std::string narrow(const std::wstring& value) {
    std::string out(value.size(), '?');
    for (size_t i = 0; i < value.size(); ++i)
        out[i] = value[i] < 128 ? static_cast<char>(value[i]) : '?';
    return out;
}

std::string lower(std::string value) {
    for (char& c : value)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    return value;
}

// Comments are not legal JSON but Bedrock accepts them, so plenty of packs in
// the wild ship a manifest with `//` in it. Parsing strictly would reject those
// packs and quietly report that the player has no skybox.
nlohmann::json parse(const std::string& text) {
    if (text.empty())
        return {};
    return nlohmann::json::parse(text, nullptr, false, true);
}

// The uuid that identifies a pack is the one inside "header" - the entries
// under "modules" have their own, and matching against those finds nothing.
std::string manifestUuid(const std::string& text) {
    const nlohmann::json manifest = parse(text);
    if (!manifest.is_object())
        return {};

    const auto header = manifest.find("header");
    if (header == manifest.end() || !header->is_object())
        return {};

    const auto uuid = header->find("uuid");
    return uuid != header->end() && uuid->is_string() ? lower(uuid->get<std::string>()) : std::string{};
}

std::vector<std::string> activePackIds(const std::wstring& root) {
    std::vector<std::string> ids;

    const nlohmann::json active = parse(readFile(root + L"minecraftpe\\global_resource_packs.json"));
    if (!active.is_array())
        return ids;

    for (const nlohmann::json& entry : active) {
        if (!entry.is_object())
            continue;
        const auto id = entry.find("pack_id");
        if (id != entry.end() && id->is_string())
            ids.push_back(lower(id->get<std::string>()));
    }
    return ids;
}

} // namespace

SkyAssets scanActive() {
    SkyAssets assets;

    const std::wstring root = comMojang();
    if (root.empty()) {
        LOG_WARN("Packs", "could not locate the game's LocalState folder");
        return assets;
    }

    const std::vector<std::string> active = activePackIds(root);
    if (active.empty()) {
        // Log the path, not just the verdict: every way this can go wrong -
        // the sandbox redirect, a renamed package, the player having no packs -
        // looks identical from the outside otherwise.
        LOG_INFO("Packs", "no active packs listed in {}minecraftpe\\global_resource_packs.json",
                 narrow(root));
        return assets;
    }

    // Walk the pack folders once and keep only those whose manifest uuid is in
    // the active list. Scanning every folder instead would report a pack the
    // player has installed but switched off, and the whole point of doing this
    // on disk is to not guess.
    WIN32_FIND_DATAW entry{};
    const std::wstring packs = root + L"resource_packs\\";
    HANDLE find = FindFirstFileW((packs + L"*").c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE) {
        LOG_INFO("Packs", "no resource_packs folder");
        return assets;
    }

    int matched = 0;
    do {
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || entry.cFileName[0] == L'.')
            continue;

        const std::wstring dir = packs + entry.cFileName + std::wstring(L"\\");
        const std::string uuid = manifestUuid(readFile(dir + L"manifest.json"));
        if (uuid.empty())
            continue;

        bool isActive = false;
        for (const std::string& id : active)
            isActive = isActive || id == uuid;
        if (!isActive)
            continue;

        ++matched;

        if (hasTexture(dir, L"textures\\environment\\end_sky")) {
            assets.endSky = true;
            // The material file is what turns that one texture into a real
            // cubemap; without it the pack still draws, just the same square on
            // every side. Only worth knowing so the log can say which it is.
            assets.cubemapShader =
                assets.cubemapShader || exists(dir + L"materials\\sky.material");
        }

        // All six have to be there. A partial set would leave vanilla on the
        // missing sides, which looks worse than not trying.
        bool sixFaces = true;
        for (int face = 0; face < 6 && sixFaces; ++face) {
            wchar_t relative[64]{};
            wsprintfW(relative, L"textures\\environment\\overworld_cubemap\\cubemap_%d", face);
            sixFaces = hasTexture(dir, relative);
        }
        if (sixFaces && !assets.faces) {
            assets.faces = true;
            assets.facesDir = dir;
        }
    } while (FindNextFileW(find, &entry));

    FindClose(find);

    LOG_INFO("Packs", "{} of {} active pack(s) on disk: end_sky {}, sky.material {}, cubemap faces {}",
             matched, active.size(), assets.endSky, assets.cubemapShader, assets.faces);
    return assets;
}

} // namespace aerial::packs
