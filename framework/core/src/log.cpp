#include "fw/core/log.hpp"

#include <spdlog/spdlog.h>

#include <string>

namespace fw::log {
namespace {

constexpr spdlog::level::level_enum to_spdlog(Level level) noexcept {
    switch (level) {
    case Level::trace:
        return spdlog::level::trace;
    case Level::debug:
        return spdlog::level::debug;
    case Level::info:
        return spdlog::level::info;
    case Level::warn:
        return spdlog::level::warn;
    case Level::error:
        return spdlog::level::err;
    case Level::critical:
        return spdlog::level::critical;
    }
    return spdlog::level::info;
}

spdlog::level::level_enum parse_level(std::string_view level) {
    if (level == "trace") {
        return spdlog::level::trace;
    }
    if (level == "debug") {
        return spdlog::level::debug;
    }
    if (level == "warn" || level == "warning") {
        return spdlog::level::warn;
    }
    if (level == "error" || level == "err") {
        return spdlog::level::err;
    }
    if (level == "critical") {
        return spdlog::level::critical;
    }
    if (level == "off") {
        return spdlog::level::off;
    }
    return spdlog::level::info;
}

} // namespace

void init(std::string_view level, bool json) {
    spdlog::set_level(parse_level(level));
    if (json) {
        spdlog::set_pattern(
            R"({"time":"%Y-%m-%dT%H:%M:%S.%e","level":"%l","thread":%t,"msg":"%v"})");
    } else {
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
    }
    spdlog::set_error_handler([](std::string const& message) {
        spdlog::default_logger()->log(spdlog::level::err, "log error: {}", message);
    });
}

void set_level(Level level) {
    spdlog::set_level(to_spdlog(level));
}

bool enabled(Level level) noexcept {
    return spdlog::default_logger()->should_log(to_spdlog(level));
}

void write(Level level, std::string_view message) {
    spdlog::log(to_spdlog(level), std::string{message});
}

void trace(std::string_view message) {
    write(Level::trace, message);
}
void debug(std::string_view message) {
    write(Level::debug, message);
}
void info(std::string_view message) {
    write(Level::info, message);
}
void warn(std::string_view message) {
    write(Level::warn, message);
}
void error(std::string_view message) {
    write(Level::error, message);
}
void critical(std::string_view message) {
    write(Level::critical, message);
}

} // namespace fw::log
