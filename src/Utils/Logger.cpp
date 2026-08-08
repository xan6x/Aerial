#include "Utils/Logger.h"

#include <Windows.h>
#include <ShlObj.h>
#include <chrono>
#include <filesystem>

namespace aerial {
namespace {

const char* levelName(LogLevel level) {
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

std::filesystem::path logDirectory() {
    PWSTR local = nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        dir = std::filesystem::path(local) / L"AerialClient";
        CoTaskMemFree(local);
    } else {
        dir = std::filesystem::temp_directory_path() / L"AerialClient";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

}

Logger& Logger::get() {
    static Logger instance;
    return instance;
}

void Logger::init() {
    std::lock_guard lock(m_mutex);

    if (!m_file) {
        const auto path = logDirectory() / L"latest.log";

        _wfopen_s(&m_file, path.c_str(), L"a");
        if (m_file)
            std::fputs("\n──── session start ────\n", m_file);
    }
}

void Logger::shutdown() {
    std::lock_guard lock(m_mutex);

    if (m_file) {
        fclose(m_file);
        m_file = nullptr;
    }
}

void Logger::write(LogLevel level, std::string_view tag, std::string_view message) {
    using namespace std::chrono;
    const auto now = current_zone()->to_local(system_clock::now());
    const auto stamp = std::format("{:%H:%M:%S}", floor<milliseconds>(now));

    std::lock_guard lock(m_mutex);

    if (m_file) {
        std::fprintf(m_file, "[%s] %s [%.*s] %.*s\n", stamp.c_str(), levelName(level),
                     static_cast<int>(tag.size()), tag.data(),
                     static_cast<int>(message.size()), message.data());
        std::fflush(m_file);
    }

    const auto line = std::format("[Aerial/{}] {}\n", tag, message);
    OutputDebugStringA(line.c_str());
}

}
