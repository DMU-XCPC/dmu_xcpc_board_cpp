#pragma once

#include <cstdint>
#include <string_view>

namespace fw::log {

enum class Level : std::uint8_t { trace, debug, info, warn, error, critical };

void init(std::string_view level = "info", bool json = false);
void set_level(Level level);
[[nodiscard]] bool enabled(Level level) noexcept;
void write(Level level, std::string_view message);

void trace(std::string_view message);
void debug(std::string_view message);
void info(std::string_view message);
void warn(std::string_view message);
void error(std::string_view message);
void critical(std::string_view message);

} // namespace fw::log
