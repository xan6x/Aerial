#pragma once

namespace aerial::hooks {

struct Installer {
    Installer(const char* name, bool (*install)());
};

}
