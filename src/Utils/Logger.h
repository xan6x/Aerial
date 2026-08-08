#pragma once

#include <cstdio>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

namespace aerial {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

class Logger {
public:
    static Logger& get();

    void init();
    void shutdown();

    void setLevel(LogLevel level) { m_level = level; }
    LogLevel level() const { return m_level; }

    void write(LogLevel level, std::string_view tag, std::string_view message);

private:
    Logger() = default;

    std::mutex m_mutex;
    FILE* m_file = nullptr;
    LogLevel m_level = LogLevel::Debug;
};

namespace detail {
template <typename... Args>
inline void logf(LogLevel level, std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
    if (level < Logger::get().level())
        return;
    Logger::get().write(level, tag, std::format(fmt, std::forward<Args>(args)...));
}
}

}

#define LOG_TRACE(tag, ...) ::aerial::detail::logf(::aerial::LogLevel::Trace, tag, __VA_ARGS__)
#define LOG_DEBUG(tag, ...) ::aerial::detail::logf(::aerial::LogLevel::Debug, tag, __VA_ARGS__)
#define LOG_INFO(tag, ...)  ::aerial::detail::logf(::aerial::LogLevel::Info,  tag, __VA_ARGS__)
#define LOG_WARN(tag, ...)  ::aerial::detail::logf(::aerial::LogLevel::Warn,  tag, __VA_ARGS__)
#define LOG_ERROR(tag, ...) ::aerial::detail::logf(::aerial::LogLevel::Error, tag, __VA_ARGS__)
