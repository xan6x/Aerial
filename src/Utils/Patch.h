#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aerial {

class BytePatch {
public:
    BytePatch(std::string signature, std::vector<uint8_t> replacement);

    static BytePatch nops(std::string signature, size_t count);

    bool apply();
    bool revert();

    bool applied() const { return m_applied; }

    bool resolve();
    uintptr_t address() const { return m_address; }

    const std::string& signature() const { return m_signature; }

private:
    std::string m_signature;
    std::vector<uint8_t> m_replacement;
    std::vector<uint8_t> m_original;

    uintptr_t m_address = 0;
    bool m_searched = false;
    bool m_applied = false;
};

}
