#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aerial {

// A reversible byte patch located by signature.
//
// The address is resolved once, on first use, and the original bytes are kept
// so the patch can be lifted again - every one of these is behind a module
// toggle, and a module that cannot undo itself is a module that forces a game
// restart.
//
// Signatures are the usual masked-hex form, with '?' for a wildcard byte:
//
//     BytePatch::nops("48 89 86 ? ? ? ?", 7)
//
class BytePatch {
public:
    BytePatch(std::string signature, std::vector<uint8_t> replacement);

    // Overwrites `count` bytes with 0x90. `count` must cover whole
    // instructions - a partial overwrite leaves the tail of one decoding as
    // something else entirely.
    static BytePatch nops(std::string signature, size_t count);

    bool apply();
    bool revert();

    bool applied() const { return m_applied; }

    // Resolves the signature if that has not happened yet. False means the
    // pattern is not in this build.
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

} // namespace aerial
