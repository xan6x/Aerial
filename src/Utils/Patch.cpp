#include "Utils/Patch.h"

#include <cstring>

#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial {

BytePatch::BytePatch(std::string signature, std::vector<uint8_t> replacement)
    : m_signature(std::move(signature)), m_replacement(std::move(replacement)) {}

BytePatch BytePatch::nops(std::string signature, size_t count) {
    return BytePatch(std::move(signature), std::vector<uint8_t>(count, 0x90));
}

bool BytePatch::resolve() {
    if (m_searched)
        return m_address != 0;

    m_searched = true;
    m_address = memory::findPattern(m_signature);

    if (!m_address) {
        LOG_ERROR("Patch", "signature not found: {}", m_signature);
        return false;
    }

    // Keep the bytes we are about to overwrite so the patch can be lifted.
    m_original.resize(m_replacement.size());
    std::memcpy(m_original.data(), reinterpret_cast<const void*>(m_address), m_original.size());

    LOG_DEBUG("Patch", "{} -> {:#x} (rva {:#x})", m_signature, m_address,
              m_address - memory::base());
    return true;
}

bool BytePatch::apply() {
    if (m_applied)
        return true;
    if (!resolve())
        return false;

    if (!memory::patch(m_address, m_replacement.data(), m_replacement.size())) {
        LOG_ERROR("Patch", "could not write at {:#x}", m_address);
        return false;
    }

    m_applied = true;
    return true;
}

bool BytePatch::revert() {
    if (!m_applied)
        return true;

    const bool ok = memory::patch(m_address, m_original.data(), m_original.size());
    if (!ok)
        LOG_ERROR("Patch", "could not restore at {:#x}", m_address);

    m_applied = false;
    return ok;
}

} // namespace aerial
