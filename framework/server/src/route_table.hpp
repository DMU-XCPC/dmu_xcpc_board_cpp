#pragma once

#include "fw/server/pattern.hpp"
#include "fw/server/router.hpp"

#include <ankerl/unordered_dense.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fw::server::detail {

struct RouteNode;

struct StringHash {
    using is_transparent = void;
    using is_avalanching = void;

    auto operator()(std::string_view value) const noexcept -> std::uint64_t {
        return ankerl::unordered_dense::hash<std::string_view>{}(value);
    }
    auto operator()(std::string const& value) const noexcept -> std::uint64_t {
        return (*this)(std::string_view{value});
    }
};

struct StringEq {
    using is_transparent = void;

    auto operator()(std::string_view lhs, std::string_view rhs) const noexcept -> bool {
        return lhs == rhs;
    }
};

using StaticChildren =
    ankerl::unordered_dense::map<std::string, std::unique_ptr<RouteNode>, StringHash, StringEq>;

struct ParamEdge {
    std::unique_ptr<RouteNode> child;
    std::string name;
    std::shared_ptr<Constraint const> constraint;
    bool optional = false;
};

struct RouteNode {
    StaticChildren literals;
    std::vector<ParamEdge> params;
    std::unique_ptr<RouteNode> catch_all;
    std::string catch_all_name;
    std::array<std::int32_t, kMethodCount> handlers;

    RouteNode() {
        handlers.fill(-1);
    }

    [[nodiscard]] bool has_handlers() const noexcept {
        return std::ranges::any_of(handlers, [](std::int32_t handler) { return handler >= 0; });
    }
};

struct CompiledRoute {
    std::string name;
    std::string pattern;
    http::Method method;
    std::vector<std::string> param_names;
    Handler handler;
};

struct CompiledTable {
    RouteNode root;
    std::vector<CompiledRoute> routes;
};

struct PendingRoute {
    http::Method method;
    PathPattern pattern;
    Handler handler;
    std::string name;
};

struct MatchResult {
    std::int32_t route = -1;
    RouteNode const* mismatch = nullptr;
    std::size_t param_count = 0;
};

fw::Result<void> compile_one(CompiledTable& table, PendingRoute const& route,
                             std::vector<Middleware> const& middleware, bool case_insensitive);

void match_node(RouteNode const& node, bool ci, std::span<std::string_view const> segments,
                std::size_t index, std::string_view path, http::Method method,
                std::string_view* values, MatchResult& result, std::size_t count);

std::string allow_header(RouteNode const& node);

} // namespace fw::server::detail
