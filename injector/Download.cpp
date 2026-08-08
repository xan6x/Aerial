#include "Download.h"

#include <Windows.h>
#include <winhttp.h>

#include <format>

namespace aerial::net {
namespace {

constexpr wchar_t kUserAgent[] = L"AerialClient-Injector";

struct Handle {
    HINTERNET value = nullptr;

    ~Handle() {
        if (value)
            WinHttpCloseHandle(value);
    }

    Handle() = default;
    explicit Handle(HINTERNET handle) : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    explicit operator bool() const { return value != nullptr; }
};

std::wstring lastError(const wchar_t* what) {
    return std::format(L"{} failed (error {})", what, GetLastError());
}

}

Response get(const std::wstring& url) {
    Response result;

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = 1;
    parts.dwUrlPathLength = 1;
    parts.dwExtraInfoLength = 1;

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        result.error = lastError(L"WinHttpCrackUrl");
        return result;
    }

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    Handle session(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        result.error = lastError(L"WinHttpOpen");
        return result;
    }

    Handle connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
    if (!connection) {
        result.error = lastError(L"WinHttpConnect");
        return result;
    }

    const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        result.error = lastError(L"WinHttpOpenRequest");
        return result;
    }

    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        result.error = lastError(L"WinHttpSendRequest");
        return result;
    }

    if (!WinHttpReceiveResponse(request.value, nullptr)) {
        result.error = lastError(L"WinHttpReceiveResponse");
        return result;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    result.status = status;

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.value, &available)) {
            result.error = lastError(L"WinHttpQueryDataAvailable");
            return result;
        }
        if (available == 0)
            break;

        const size_t offset = result.body.size();
        result.body.resize(offset + available);

        DWORD read = 0;
        if (!WinHttpReadData(request.value, result.body.data() + offset, available, &read)) {
            result.error = lastError(L"WinHttpReadData");
            return result;
        }
        result.body.resize(offset + read);
    }

    if (status != 200) {
        result.error = std::format(L"server answered {}", status);
        return result;
    }

    result.ok = true;
    return result;
}

}
