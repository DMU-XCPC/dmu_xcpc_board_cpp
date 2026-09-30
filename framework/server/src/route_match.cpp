#include "route_table.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace fw::server::detail {
namespace {

std::string_view literal_key(std::string_view value, bool case_insensitive,
                             std::array<char, 128>& buffer) {
    if (!case_insensitive || value.size() > buffer.size()) {
        return value;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        buffer[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
    }
    return std::string_view{buffer.data(), value.size()};
}

bool record_handler(RouteNode const& node, http::Method method, std::size_t count,
                    MatchResult& result) {
    auto const method_index = static_cast<std::size_t>(method);
    if (node.handlers[method_index] >= 0) {
        result.route = node.handlers[method_index];
        result.param_count = count;
        return true;
    }
    auto const get_index = static_cast<std::size_t>(http::Method::get);
    if (method == http::Method::head && node.handlers[get_index] >= 0) {
        result.route = node.handlers[get_index];
        result.param_count = count;
        return true;
    }
    if (node.has_handlers()) {
        result.mismatch = &node;
    }
    return false;
}

bool try_literal(RouteNode const& node, bool ci, std::string_view segment,
                 std::span<std::string_view const> segments, std::size_t index,
                 std::string_view path, http::Method method, std::string_view* values,
                 MatchResult& result, std::size_t count) {
    std::array<char, 128> buffer{};
    auto const it = node.literals.find(literal_key(segment, ci, buffer));
    if (it != node.literals.end()) {
        match_node(*it->second, ci, segments, index + 1, path, method, values, result, count);
        if (result.route >= 0) {
            return true;
        }
    }
    return false;
}

bool try_param(RouteNode const& node, bool ci, std::string_view segment,
               std::span<std::string_view const> segments, std::size_t index, std::string_view path,
               http::Method method, std::string_view* values, MatchResult& result,
               std::size_t count) {
    if (count >= kMaxRouteParams) {
        return false;
    }
    for (auto const& edge : node.params) {
        bool const consumable =
            !segment.empty() && (edge.constraint == nullptr || edge.constraint->matches(segment));
        if (consumable) {
            values[count] = segment;
            match_node(*edge.child, ci, segments, index + 1, path, method, values, result,
                       count + 1);
            if (result.route >= 0) {
                return true;
            }
        }
        if (edge.optional) {
            match_node(*edge.child, ci, segments, index, path, method, values, result, count);
            if (result.route >= 0) {
                return true;
            }
        }
    }
    return false;
}

void try_catch_all(RouteNode const& node, bool ci, std::string_view segment,
                   std::span<std::string_view const> segments, std::string_view path,
                   http::Method method, std::string_view* values, MatchResult& result,
                   std::size_t count) {
    (void)ci;
    if (node.catch_all == nullptr || count >= kMaxRouteParams) {
        return;
    }
    auto const offset = static_cast<std::size_t>(segment.data() - path.data());
    values[count] = path.substr(offset);
    match_node(*node.catch_all, ci, segments, segments.size(), path, method, values, result,
               count + 1);
}

} // namespace

void match_node(RouteNode const& node, bool ci, std::span<std::string_view const> segments,
                std::size_t index, std::string_view path, http::Method method,
                std::string_view* values, MatchResult& result, std::size_t count) {
    if (index == segments.size()) {
        if (record_handler(node, method, count, result)) {
            return;
        }
        for (auto const& edge : node.params) {
            if (edge.optional && count < kMaxRouteParams) {
                match_node(*edge.child, ci, segments, index, path, method, values, result, count);
                if (result.route >= 0) {
                    return;
                }
            }
        }
        return;
    }

    auto const segment = segments[index];
    if (try_literal(node, ci, segment, segments, index, path, method, values, result, count)) {
        return;
    }
    if (try_param(node, ci, segment, segments, index, path, method, values, result, count)) {
        return;
    }
    try_catch_all(node, ci, segment, segments, path, method, values, result, count);
}

std::string allow_header(RouteNode const& node) {
    std::string allow;
    for (std::size_t method = 0; method < kMethodCount; ++method) {
        if (node.handlers[method] >= 0) {
            if (!allow.empty()) {
                allow += ", ";
            }
            allow += http::to_string(static_cast<http::Method>(method));
        }
    }
    return allow;
}

} // namespace fw::server::detail
