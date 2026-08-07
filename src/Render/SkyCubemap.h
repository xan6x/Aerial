#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aerial::render {

class SkyCubemap {
public:
    static SkyCubemap& get();

    bool load();

    void unload();

    bool ready() const { return m_ready; }

    void draw(void* camera);

    const char* status() const { return m_status; }

private:
    SkyCubemap() = default;

    bool m_ready = false;
    bool m_failed = false;
    bool m_warnedBatch = false;
    bool m_drewOnce = false;
    const char* m_status = "not loaded";

    std::vector<uint8_t> m_faces;
};

}
