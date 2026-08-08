#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aerial::net {

struct Response {
    bool ok = false;
    unsigned status = 0;
    std::wstring error;
    std::vector<uint8_t> body;
};

Response get(const std::wstring& url);

}
