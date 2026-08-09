#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aerial::render {

class SkyCubemap {
public:
    static SkyCubemap& get();

    bool load();

    bool refresh();

    void unload();

    bool ready() const { return m_ready; }

    void draw(void* camera);

    const char* status() const { return m_status; }

private:
    SkyCubemap() = default;

    void releaseFaces();

    bool m_ready = false;
    bool m_failed = false;
    bool m_warnedBatch = false;
    bool m_drewOnce = false;
    const char* m_status = "not loaded";

    void* m_group = nullptr;
    std::vector<uint8_t> m_faces;
};

}
