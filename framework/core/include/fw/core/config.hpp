#pragma once

#include "fw/core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fw {

class Config {
public:
    Config();
    ~Config();
    Config(Config const&);
    Config& operator=(Config const&);
    Config(Config&&) noexcept;
    Config& operator=(Config&&) noexcept;

    static Result<Config> from_file(std::filesystem::path const& path);
    static Result<Config> from_string(std::string_view text);

    [[nodiscard]] bool has(std::string_view key) const;

    std::optional<std::string> get_string(std::string_view key) const;
    std::optional<std::int64_t> get_int(std::string_view key) const;
    std::optional<double> get_double(std::string_view key) const;
    std::optional<bool> get_bool(std::string_view key) const;

    [[nodiscard]] std::string get_string_or(std::string_view key, std::string fallback) const;
    [[nodiscard]] std::int64_t get_int_or(std::string_view key, std::int64_t fallback) const;
    [[nodiscard]] bool get_bool_or(std::string_view key, bool fallback) const;

    void set_string(std::string_view key, std::string value);
    void set_int(std::string_view key, std::int64_t value);
    void set_bool(std::string_view key, bool value);

    void apply_env_overrides(std::string_view prefix);

    // Materialize a strongly-typed view of the configuration. `T` must have a
    // `config_schema<T>` specialization (see below). Missing keys fall back to
    // the field default; keys present with the wrong type yield an error.
    template <class T> [[nodiscard]] Result<T> bind() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

// A single typed field of a configuration view. The member pointer ties the
// field to the view struct, so the compiler checks the declared type.
template <class Owner, class T> struct field {
    using value_type = T;
    std::string_view key;
    T Owner::* member;
    T fallback;
};

// Specialize for each configuration view:
//
//   struct server_settings { std::string host; std::uint16_t port; };
//
//   template <>
//   struct fw::config_schema<server_settings> {
//       static constexpr auto fields() {
//           return std::tuple{
//               fw::field{"server.host", &server_settings::host,
//                         std::string{"0.0.0.0"}},
//               fw::field{"server.port", &server_settings::port,
//                         std::uint16_t{8080}},
//           };
//       }
//   };
template <class T> struct config_schema;
namespace detail {

template <class> inline constexpr bool always_false = false;

template <class Field> using field_value_t = typename Field::value_type;

template <class T> std::optional<T> read_field(Config const& config, std::string_view key) {
    if constexpr (std::is_same_v<T, std::string>) {
        return config.get_string(key);
    } else if constexpr (std::is_same_v<T, bool>) {
        return config.get_bool(key);
    } else if constexpr (std::is_floating_point_v<T>) {
        return config.get_double(key);
    } else if constexpr (std::is_integral_v<T>) {
        auto const raw = config.get_int(key);
        if (!raw) {
            return std::nullopt;
        }
        if constexpr (std::is_unsigned_v<T>) {
            if (*raw < 0) {
                return std::nullopt;
            }
            if constexpr (sizeof(T) < sizeof(std::int64_t)) {
                if (*raw > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
                    return std::nullopt;
                }
            }
        } else {
            if (*raw < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
                *raw > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
                return std::nullopt;
            }
        }
        return static_cast<T>(*raw);
    } else {
        static_assert(always_false<T>, "unsupported configuration field type");
    }
}

} // namespace detail

template <class T> Result<T> Config::bind() const {
    T settings{};
    bool failed = false;

    auto const fields = config_schema<T>::fields();
    std::apply(
        [&](auto const&... descriptor) {
            auto apply_one = [&](auto const& f) {
                using value_type = detail::field_value_t<std::remove_cvref_t<decltype(f)>>;
                if (has(f.key)) {
                    auto value = detail::read_field<value_type>(*this, f.key);
                    if (!value) {
                        failed = true;
                        return;
                    }
                    settings.*f.member = std::move(*value);
                } else {
                    settings.*f.member = f.fallback;
                }
            };
            (apply_one(descriptor), ...);
        },
        fields);

    if (failed) {
        return std::unexpected{make_error_code(Errc::invalid_argument)};
    }
    return settings;
}

} // namespace fw
