#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace aerial {

class Config {
public:
    static Config& get();

    std::vector<std::string> list() const;

    const std::string& current() const { return m_current; }
    bool exists(const std::string& name) const;

    bool save(const std::string& name = {});

    bool load(const std::string& name);

    bool create(const std::string& name);

    bool remove(const std::string& name);

    void loadActive();

    static std::string sanitise(const std::string& name);

    static std::filesystem::path directory();

private:
    Config() = default;

    void writeActiveMarker() const;
    std::string readActiveMarker() const;
    std::filesystem::path pathFor(const std::string& name) const;

    std::string m_current = "default";
};

}
