#include "Config/Config.h"

#include <Windows.h>
#include <ShlObj.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

#include "Module/ModuleManager.h"
#include "Utils/Logger.h"

using json = nlohmann::json;

namespace aerial {
namespace {

// Bump whenever a default that users cannot easily notice changes - keybinds,
// mostly. An older file is ignored rather than merged, so a changed default
// actually takes effect instead of being silently overwritten by stale state.
constexpr int kConfigVersion = 2;

json serialise(const Setting& setting) {
    switch (setting.type()) {
    case Setting::Type::Bool:    return static_cast<const BoolSetting&>(setting).value;
    case Setting::Type::Int:     return static_cast<const IntSetting&>(setting).value;
    case Setting::Type::Float:   return static_cast<const FloatSetting&>(setting).value;
    case Setting::Type::Enum:    return static_cast<const EnumSetting&>(setting).selected();
    case Setting::Type::Keybind: return static_cast<const KeybindSetting&>(setting).value;
    case Setting::Type::Text:    return static_cast<const TextSetting&>(setting).value;
    case Setting::Type::Colour: {
        const auto& colour = static_cast<const ColourSetting&>(setting).value;
        return json::array({colour.r, colour.g, colour.b, colour.a});
    }
    }
    return nullptr;
}

void deserialise(Setting& setting, const json& value) {
    try {
        switch (setting.type()) {
        case Setting::Type::Bool:
            static_cast<BoolSetting&>(setting).value = value.get<bool>();
            break;
        case Setting::Type::Int:
            static_cast<IntSetting&>(setting).set(value.get<int>());
            break;
        case Setting::Type::Float:
            static_cast<FloatSetting&>(setting).set(value.get<float>());
            break;
        case Setting::Type::Keybind:
            static_cast<KeybindSetting&>(setting).value = value.get<int>();
            break;
        case Setting::Type::Text:
            static_cast<TextSetting&>(setting).value = value.get<std::string>();
            break;
        case Setting::Type::Enum: {
            auto& target = static_cast<EnumSetting&>(setting);
            const auto name = value.get<std::string>();
            for (size_t i = 0; i < target.options.size(); ++i) {
                if (target.options[i] == name) {
                    target.value = static_cast<int>(i);
                    break;
                }
            }
            break;
        }
        case Setting::Type::Colour: {
            auto& target = static_cast<ColourSetting&>(setting);
            if (value.is_array() && value.size() == 4) {
                target.value = {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                                value[3].get<float>()};
            }
            break;
        }
        }
    } catch (const json::exception& error) {
        LOG_WARN("Config", "setting '{}' could not be read: {}", setting.name(), error.what());
    }
}

} // namespace

Config& Config::get() {
    static Config instance;
    return instance;
}

std::filesystem::path Config::directory() {
    PWSTR local = nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        dir = std::filesystem::path(local) / L"AerialClient" / L"configs";
        CoTaskMemFree(local);
    } else {
        dir = std::filesystem::temp_directory_path() / L"AerialClient" / L"configs";
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string Config::sanitise(const std::string& name) {
    std::string result;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '_' || c == '-')
            result.push_back(c);
        if (result.size() >= 32)
            break;
    }

    const size_t first = result.find_first_not_of(' ');
    const size_t last = result.find_last_not_of(' ');
    if (first == std::string::npos)
        return {};
    return result.substr(first, last - first + 1);
}

std::filesystem::path Config::pathFor(const std::string& name) const {
    return directory() / (name + ".json");
}

bool Config::exists(const std::string& name) const {
    std::error_code ec;
    return std::filesystem::exists(pathFor(name), ec);
}

std::vector<std::string> Config::list() const {
    std::vector<std::string> names;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory(), ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".json")
            continue;
        names.push_back(entry.path().stem().string());
    }

    std::sort(names.begin(), names.end());
    return names;
}

std::filesystem::path activeMarkerPath() { return Config::directory() / "active.txt"; }

void Config::writeActiveMarker() const {
    std::ofstream file(activeMarkerPath());
    if (file)
        file << m_current;
}

std::string Config::readActiveMarker() const {
    std::ifstream file(activeMarkerPath());
    std::string name;
    if (file)
        std::getline(file, name);
    return sanitise(name);
}

void Config::loadActive() {
    const std::string name = readActiveMarker();
    if (!name.empty() && exists(name)) {
        load(name);
        return;
    }
    load("default");
}

bool Config::create(const std::string& rawName) {
    const std::string name = sanitise(rawName);
    if (name.empty()) {
        LOG_WARN("Config", "refusing to create a config with an empty name");
        return false;
    }
    if (exists(name)) {
        LOG_WARN("Config", "config '{}' already exists", name);
        return false;
    }

    // A new config starts as a snapshot of what is on screen right now, which
    // is almost always what someone means by "create".
    m_current = name;
    writeActiveMarker();
    return save(name);
}

bool Config::remove(const std::string& name) {
    std::error_code ec;
    if (!std::filesystem::remove(pathFor(name), ec)) {
        LOG_WARN("Config", "could not delete '{}'", name);
        return false;
    }

    LOG_INFO("Config", "deleted '{}'", name);

    // Deleting the active config leaves nothing selected; fall back so save()
    // does not write to a file the user just removed.
    if (m_current == name) {
        m_current = "default";
        writeActiveMarker();
    }
    return true;
}

bool Config::save(const std::string& requested) {
    const std::string name = requested.empty() ? m_current : sanitise(requested);
    if (name.empty())
        return false;

    json root;
    root["version"] = kConfigVersion;

    for (const auto& module : ModuleManager::get().modules()) {
        json entry;
        entry["enabled"] = module->persistEnabled() && module->enabled();

        json settings = json::object();
        for (const auto& setting : module->settings())
            settings[setting->name()] = serialise(*setting);
        entry["settings"] = std::move(settings);

        root["modules"][module->name()] = std::move(entry);
    }

    const auto path = pathFor(name);
    std::ofstream file(path);
    if (!file) {
        LOG_ERROR("Config", "cannot write {}", path.string());
        return false;
    }

    file << root.dump(2);
    LOG_INFO("Config", "saved '{}'", name);

    m_current = name;
    writeActiveMarker();
    return true;
}

bool Config::load(const std::string& requested) {
    const std::string name = sanitise(requested);
    if (name.empty())
        return false;

    const auto path = pathFor(name);
    std::ifstream file(path);
    if (!file) {
        LOG_INFO("Config", "no config at {}, using defaults", path.string());
        return false;
    }

    json root;
    try {
        file >> root;
    } catch (const json::exception& error) {
        LOG_ERROR("Config", "{} is not valid JSON: {}", path.string(), error.what());
        return false;
    }

    const int version = root.value("version", 0);
    if (version != kConfigVersion) {
        LOG_WARN("Config", "{} was written by an older build (version {}, expected {}); "
                           "ignoring it so new defaults apply",
                 path.string(), version, kConfigVersion);
        return false;
    }

    const auto modules = root.find("modules");
    if (modules == root.end())
        return false;

    for (const auto& module : ModuleManager::get().modules()) {
        const auto entry = modules->find(module->name());
        if (entry == modules->end())
            continue;

        const auto settings = entry->find("settings");
        if (settings != entry->end()) {
            for (const auto& setting : module->settings()) {
                const auto value = settings->find(setting->name());
                if (value != settings->end())
                    deserialise(*setting, *value);
            }
        }

        // Loading a config must be able to turn things off as well as on,
        // otherwise switching between configs only ever accumulates modules.
        if (module->persistEnabled())
            module->setEnabled(entry->value("enabled", false));
    }

    m_current = name;
    writeActiveMarker();

    LOG_INFO("Config", "loaded '{}'", name);
    return true;
}

} // namespace aerial
