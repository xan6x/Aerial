#pragma once

#include <filesystem>
#include <string>

namespace aerial {

// Persists module state to %LOCALAPPDATA%\AerialClient\configs\<name>.json.
class Config {
public:
    static Config& get();

    bool save(const std::string& name = "default");
    bool load(const std::string& name = "default");

    static std::filesystem::path directory();

private:
    Config() = default;
};

} // namespace aerial
