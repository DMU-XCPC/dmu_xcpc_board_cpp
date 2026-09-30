#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <system_error>

namespace fw {

enum class Errc : std::uint8_t {
    success = 0,
    invalid_argument,
    not_found,
    already_exists,
    not_supported,
    unavailable,
    io_error,
    internal,
};

std::error_category const& error_category() noexcept;
std::error_code make_error_code(Errc code) noexcept;

using Error = std::error_code;

template <class T> using Result = std::expected<T, Error>;

using Status = std::expected<void, Error>;

std::string to_string(Error const& error);

} // namespace fw

namespace std {
template <> struct is_error_code_enum<fw::Errc> : true_type {};
} // namespace std
