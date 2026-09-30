#pragma once

#include "fw/core/error.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fw::server {

// A named validation for a path parameter (e.g. "int", "uuid", "slug").
class Constraint {
public:
    virtual ~Constraint() = default;
    [[nodiscard]] virtual bool matches(std::string_view value) const = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

// Look up a built-in constraint by name; returns nullptr when unknown.
[[nodiscard]] std::shared_ptr<Constraint const> make_constraint(std::string_view name);

// Compile an inline regular expression (PCRE2); returns nullptr on error.
[[nodiscard]] std::shared_ptr<Constraint const> make_regex_constraint(std::string_view pattern);

// One parsed segment of a route pattern.
class RouteSegment {
public:
    enum class Kind : std::uint8_t { literal, parameter, catch_all };

    static RouteSegment literal(std::string text);
    static RouteSegment parameter(std::string name,
                                  std::shared_ptr<Constraint const> constraint = {},
                                  bool optional = false);
    static RouteSegment catch_all(std::string name);

    [[nodiscard]] Kind kind() const noexcept {
        return kind_;
    }
    [[nodiscard]] bool is_parameter() const noexcept {
        return kind_ == Kind::parameter;
    }
    [[nodiscard]] bool is_catch_all() const noexcept {
        return kind_ == Kind::catch_all;
    }
    [[nodiscard]] bool optional() const noexcept {
        return optional_;
    }
    [[nodiscard]] std::string const& text() const noexcept {
        return text_;
    }
    [[nodiscard]] std::shared_ptr<Constraint const> const& constraint() const noexcept {
        return constraint_;
    }

private:
    RouteSegment(Kind kind, std::string text, std::shared_ptr<Constraint const> constraint,
                 bool optional);

    Kind kind_;
    std::string text_;
    std::shared_ptr<Constraint const> constraint_;
    bool optional_;
};

// A route pattern parsed into route segment objects.
class PathPattern {
public:
    // Basic parser: literal segments and `:name` parameters. More syntax
    // (constraints, catch-all, optional) is added by a later parser; use
    // `from_segments` to build richer patterns in the meantime.
    [[nodiscard]] static fw::Result<PathPattern> parse(std::string_view path);
    [[nodiscard]] static PathPattern from_segments(std::vector<RouteSegment> segments);

    [[nodiscard]] std::span<RouteSegment const> segments() const noexcept {
        return segments_;
    }
    [[nodiscard]] bool empty() const noexcept {
        return segments_.empty();
    }

private:
    std::vector<RouteSegment> segments_;
};

[[nodiscard]] std::vector<std::string_view> split_path(std::string_view path);

} // namespace fw::server
