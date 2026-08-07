#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace aerial {

// Named configuration files under
// %LOCALAPPDATA%\AerialClient\configs\<name>.json.
//
// One of them is "active": it is what loads at startup and what save() writes
// to when given no name. The choice survives restarts in a small marker file
// beside the configs.
class Config {
public:
    static Config& get();

    // All config names on disk, sorted, without the .json suffix.
    std::vector<std::string> list() const;

    const std::string& current() const { return m_current; }
    bool exists(const std::string& name) const;

    // Writes the live module state. An empty name means the active config.
    bool save(const std::string& name = {});

    // Applies a config and makes it active.
    bool load(const std::string& name);

    // Snapshots the current state under a new name and switches to it.
    bool create(const std::string& name);

    bool remove(const std::string& name);

    // Loads whichever config was active last, falling back to "default".
    void loadActive();

    // Strips anything that has no business in a file name and trims the length.
    // Returns an empty string if nothing usable is left.
    static std::string sanitise(const std::string& name);

    static std::filesystem::path directory();

private:
    Config() = default;

    void writeActiveMarker() const;
    std::string readActiveMarker() const;
    std::filesystem::path pathFor(const std::string& name) const;

    std::string m_current = "default";
};

} // namespace aerial
