#include "fw/server/pattern.hpp"

#include <algorithm>
#include <cctype>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <cstdint>
#include <memory>
#include <string>

namespace fw::server {
namespace {

class IntConstraint final : public Constraint {
public:
    [[nodiscard]] bool matches(std::string_view value) const override {
        if (value.empty()) {
            return false;
        }
        std::size_t index = 0;
        if (value.front() == '-') {
            index = 1;
        }
        if (index == value.size()) {
            return false;
        }
        for (; index < value.size(); ++index) {
            if (std::isdigit(static_cast<unsigned char>(value[index])) == 0) {
                return false;
            }
        }
        return true;
    }
    [[nodiscard]] std::string_view name() const noexcept override {
        return "int";
    }
};

class UuidConstraint final : public Constraint {
public:
    [[nodiscard]] bool matches(std::string_view value) const override {
        if (value.size() != 36) {
            return false;
        }
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (value[i] != '-') {
                    return false;
                }
            } else if (std::isxdigit(static_cast<unsigned char>(value[i])) == 0) {
                return false;
            }
        }
        return true;
    }
    [[nodiscard]] std::string_view name() const noexcept override {
        return "uuid";
    }
};

class SlugConstraint final : public Constraint {
public:
    [[nodiscard]] bool matches(std::string_view value) const override {
        if (value.empty()) {
            return false;
        }
        return std::ranges::all_of(value, [](char ch) {
            auto const c = static_cast<unsigned char>(ch);
            return std::islower(c) != 0 || std::isdigit(c) != 0 || ch == '-';
        });
    }
    [[nodiscard]] std::string_view name() const noexcept override {
        return "slug";
    }
};

class RegexConstraint final : public Constraint {
public:
    explicit RegexConstraint(std::string pattern) : pattern_(std::move(pattern)) {
        int error = 0;
        PCRE2_SIZE offset = 0;
        code_ = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern_.c_str()), pattern_.size(), 0,
                              &error, &offset, nullptr);
    }

    RegexConstraint(RegexConstraint const&) = delete;
    RegexConstraint& operator=(RegexConstraint const&) = delete;

    ~RegexConstraint() override {
        if (code_ != nullptr) {
            pcre2_code_free(code_);
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return code_ != nullptr;
    }

    [[nodiscard]] bool matches(std::string_view value) const override {
        if (code_ == nullptr) {
            return false;
        }
        pcre2_match_data* data = pcre2_match_data_create_from_pattern(code_, nullptr);
        int const result = pcre2_match(code_, reinterpret_cast<PCRE2_SPTR>(value.data()),
                                       value.size(), 0, 0, data, nullptr);
        pcre2_match_data_free(data);
        return result >= 0;
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return pattern_;
    }

private:
    std::string pattern_;
    pcre2_code* code_ = nullptr;
};

} // namespace

std::shared_ptr<Constraint const> make_constraint(std::string_view name) {
    static std::shared_ptr<Constraint const> const integer = std::make_shared<IntConstraint>();
    static std::shared_ptr<Constraint const> const uuid = std::make_shared<UuidConstraint>();
    static std::shared_ptr<Constraint const> const slug = std::make_shared<SlugConstraint>();
    if (name == "int") {
        return integer;
    }
    if (name == "uuid") {
        return uuid;
    }
    if (name == "slug") {
        return slug;
    }
    return nullptr;
}

std::shared_ptr<Constraint const> make_regex_constraint(std::string_view pattern) {
    auto constraint = std::make_shared<RegexConstraint>(std::string{pattern});
    if (!constraint->valid()) {
        return nullptr;
    }
    return constraint;
}

RouteSegment::RouteSegment(Kind kind, std::string text,
                           std::shared_ptr<Constraint const> constraint, bool optional)
    : kind_(kind), text_(std::move(text)), constraint_(std::move(constraint)), optional_(optional) {
}

RouteSegment RouteSegment::literal(std::string text) {
    return RouteSegment{Kind::literal, std::move(text), nullptr, false};
}

RouteSegment RouteSegment::parameter(std::string name, std::shared_ptr<Constraint const> constraint,
                                     bool optional) {
    return RouteSegment{Kind::parameter, std::move(name), std::move(constraint), optional};
}

RouteSegment RouteSegment::catch_all(std::string name) {
    return RouteSegment{Kind::catch_all, std::move(name), nullptr, false};
}

std::vector<std::string_view> split_path(std::string_view path) {
    std::vector<std::string_view> segments;
    std::size_t index = (!path.empty() && path.front() == '/') ? 1 : 0;
    while (true) {
        auto const slash = path.find('/', index);
        if (slash == std::string_view::npos) {
            segments.push_back(path.substr(index));
            break;
        }
        segments.push_back(path.substr(index, slash - index));
        index = slash + 1;
    }
    return segments;
}

namespace {

std::vector<std::string_view> split_pattern(std::string_view path) {
    std::vector<std::string_view> out;
    std::size_t start = (!path.empty() && path.front() == '/') ? 1 : 0;
    int depth = 0;
    for (std::size_t i = start; i < path.size(); ++i) {
        char const c = path[i];
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            depth = depth > 0 ? depth - 1 : 0;
        } else if (c == '/' && depth == 0) {
            out.push_back(path.substr(start, i - start));
            start = i + 1;
        }
    }
    out.push_back(path.substr(start));
    return out;
}

fw::Result<RouteSegment> parse_brace_segment(std::string_view inner) {
    bool optional = false;
    if (!inner.empty() && inner.back() == '?') {
        optional = true;
        inner.remove_suffix(1);
    }
    auto const colon = inner.find(':');
    if (colon == std::string_view::npos) {
        return RouteSegment::parameter(std::string{inner}, {}, optional);
    }
    auto const name = inner.substr(0, colon);
    auto const spec = inner.substr(colon + 1);
    if (spec == "*") {
        return RouteSegment::catch_all(std::string{name});
    }
    if (spec.size() >= 2 && spec.front() == '/' && spec.back() == '/') {
        auto const constraint = make_regex_constraint(spec.substr(1, spec.size() - 2));
        if (constraint == nullptr) {
            return std::unexpected{fw::make_error_code(fw::Errc::invalid_argument)};
        }
        return RouteSegment::parameter(std::string{name}, constraint, optional);
    }
    auto const constraint = make_constraint(spec);
    if (constraint == nullptr) {
        return std::unexpected{fw::make_error_code(fw::Errc::invalid_argument)};
    }
    return RouteSegment::parameter(std::string{name}, constraint, optional);
}

} // namespace

fw::Result<PathPattern> PathPattern::parse(std::string_view path) {
    PathPattern pattern;
    for (auto const raw : split_pattern(path)) {
        if (raw.size() > 1 && raw.front() == ':') {
            pattern.segments_.push_back(RouteSegment::parameter(std::string{raw.substr(1)}));
            continue;
        }
        if (raw.size() > 1 && raw.front() == '*') {
            pattern.segments_.push_back(RouteSegment::catch_all(std::string{raw.substr(1)}));
            continue;
        }
        if (raw.size() >= 2 && raw.front() == '{' && raw.back() == '}') {
            auto segment = parse_brace_segment(raw.substr(1, raw.size() - 2));
            if (!segment) {
                return std::unexpected{segment.error()};
            }
            pattern.segments_.push_back(std::move(*segment));
            continue;
        }
        pattern.segments_.push_back(RouteSegment::literal(std::string{raw}));
    }
    return pattern;
}

PathPattern PathPattern::from_segments(std::vector<RouteSegment> segments) {
    PathPattern pattern;
    pattern.segments_ = std::move(segments);
    return pattern;
}

} // namespace fw::server
