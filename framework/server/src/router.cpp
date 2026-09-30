#include "fw/server/router.hpp"

#include <ankerl/unordered_dense.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fw::server {
namespace {
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

struct RouteNode {
    StaticChildren literals;
    std::unique_ptr<RouteNode> param;
    std::string param_name;
    std::shared_ptr<Constraint const> param_constraint;
    bool param_optional = false;
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

RouteNode* literal_child(RouteNode& node, std::string const& text) {
    auto it = node.literals.find(std::string_view{text});
    if (it != node.literals.end()) {
        return it->second.get();
    }
    auto [inserted, _] = node.literals.try_emplace(text, std::make_unique<RouteNode>());
    return inserted->second.get();
}

std::string pattern_text(std::span<RouteSegment const> segments) {
    std::string out;
    for (auto const& segment : segments) {
        out.push_back('/');
        switch (segment.kind()) {
        case RouteSegment::Kind::literal:
            out += segment.text();
            break;
        case RouteSegment::Kind::parameter:
            out.push_back(':');
            out += segment.text();
            break;
        case RouteSegment::Kind::catch_all:
            out.push_back('*');
            out += segment.text();
            break;
        }
    }
    return out.empty() ? std::string{"/"} : out;
}

constexpr std::size_t kMaxSegments = 32;

int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string percent_decode(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            int const hi = hex_digit(value[i + 1]);
            int const lo = hex_digit(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                auto const code = (static_cast<unsigned>(hi) << 4U) | static_cast<unsigned>(lo);
                out.push_back(static_cast<char>(code));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

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

std::string normalize_literal(std::string_view text, bool case_insensitive) {
    std::string out{text};
    if (case_insensitive) {
        for (char& ch : out) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
    }
    return out;
}

std::size_t split_path_into(std::string_view path, std::string_view* out, std::size_t capacity) {
    std::size_t count = 0;
    std::size_t index = (!path.empty() && path.front() == '/') ? 1 : 0;
    while (true) {
        if (count >= capacity) {
            return capacity + 1;
        }
        auto const slash = path.find('/', index);
        if (slash == std::string_view::npos) {
            out[count++] = path.substr(index);
            break;
        }
        out[count++] = path.substr(index, slash - index);
        index = slash + 1;
    }
    return count;
}

struct MatchResult {
    std::int32_t route = -1;
    RouteNode const* mismatch = nullptr;
    std::size_t param_count = 0;
};

using SegmentSpan = std::span<std::string_view const>;

void match_node(RouteNode const& node, bool ci, SegmentSpan segments, std::size_t index,
                std::string_view path, http::Method method, std::string_view* values,
                MatchResult& result, std::size_t count);

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

bool try_literal(RouteNode const& node, bool ci, std::string_view segment, SegmentSpan segments,
                 std::size_t index, std::string_view path, http::Method method,
                 std::string_view* values, MatchResult& result, std::size_t count) {
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

bool try_param(RouteNode const& node, bool ci, std::string_view segment, SegmentSpan segments,
               std::size_t index, std::string_view path, http::Method method,
               std::string_view* values, MatchResult& result, std::size_t count) {
    if (node.param == nullptr || count >= kMaxRouteParams) {
        return false;
    }
    bool const consumable = !segment.empty() && (node.param_constraint == nullptr ||
                                                 node.param_constraint->matches(segment));
    if (consumable) {
        values[count] = segment;
        match_node(*node.param, ci, segments, index + 1, path, method, values, result, count + 1);
        if (result.route >= 0) {
            return true;
        }
    }
    if (node.param_optional) {
        match_node(*node.param, ci, segments, index, path, method, values, result, count);
        if (result.route >= 0) {
            return true;
        }
    }
    return false;
}

void try_catch_all(RouteNode const& node, bool ci, std::string_view segment, SegmentSpan segments,
                   std::string_view path, http::Method method, std::string_view* values,
                   MatchResult& result, std::size_t count) {
    (void)ci;
    if (node.catch_all == nullptr || count >= kMaxRouteParams) {
        return;
    }
    auto const offset = static_cast<std::size_t>(segment.data() - path.data());
    values[count] = path.substr(offset);
    match_node(*node.catch_all, ci, segments, segments.size(), path, method, values, result,
               count + 1);
}

void match_node(RouteNode const& node, bool ci, SegmentSpan segments, std::size_t index,
                std::string_view path, http::Method method, std::string_view* values,
                MatchResult& result, std::size_t count) {
    if (index == segments.size()) {
        if (record_handler(node, method, count, result)) {
            return;
        }
        if (node.param != nullptr && node.param_optional && count < kMaxRouteParams) {
            match_node(*node.param, ci, segments, index, path, method, values, result, count);
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

fw::Result<void> compile_one(CompiledTable& table, PendingRoute const& route,
                             std::vector<Middleware> const& middleware, bool case_insensitive) {
    auto const segments = route.pattern.segments();
    for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
        if (segments[i].is_catch_all()) {
            return std::unexpected{fw::make_error_code(fw::Errc::invalid_argument)};
        }
    }

    RouteNode* node = &table.root;
    std::vector<std::string> names;
    for (auto const& segment : segments) {
        if (segment.is_catch_all()) {
            if (node->catch_all == nullptr) {
                node->catch_all = std::make_unique<RouteNode>();
            }
            node->catch_all_name = segment.text();
            names.push_back(segment.text());
            node = node->catch_all.get();
        } else if (segment.is_parameter()) {
            if (node->param != nullptr && node->param_name != segment.text()) {
                return std::unexpected{fw::make_error_code(fw::Errc::already_exists)};
            }
            if (node->param == nullptr) {
                node->param = std::make_unique<RouteNode>();
                node->param_name = segment.text();
                node->param_constraint = segment.constraint();
                node->param_optional = segment.optional();
            }
            names.push_back(segment.text());
            node = node->param.get();
        } else {
            node = literal_child(*node, normalize_literal(segment.text(), case_insensitive));
        }
    }

    auto const method_index = static_cast<std::size_t>(route.method);
    if (node->handlers[method_index] >= 0) {
        return std::unexpected{fw::make_error_code(fw::Errc::already_exists)};
    }
    node->handlers[method_index] = static_cast<std::int32_t>(table.routes.size());

    Handler handler = route.handler;
    for (std::size_t i = middleware.size(); i-- > 0;) {
        handler = middleware[i](std::move(handler));
    }
    table.routes.push_back(CompiledRoute{
        route.name,
        pattern_text(segments),
        route.method,
        std::move(names),
        std::move(handler),
    });
    return {};
}

} // namespace

struct Router::Impl {
    std::vector<PendingRoute> pending;
    std::vector<Middleware> middleware;
    TrailingSlash trailing = TrailingSlash::strict;
    bool case_insensitive = false;
    std::atomic<std::shared_ptr<const CompiledTable>> snapshot{nullptr};
};

namespace {

PathPattern require_pattern(std::string_view pattern) {
    auto parsed = PathPattern::parse(pattern);
    if (!parsed) {
        throw std::invalid_argument(std::string{"invalid route pattern: "} + std::string{pattern});
    }
    return std::move(*parsed);
}

} // namespace

Router::Router() : impl_(std::make_unique<Impl>()) {}
Router::~Router() = default;
Router::Router(Router&&) noexcept = default;
Router& Router::operator=(Router&&) noexcept = default;

void Router::add(http::Method method, PathPattern pattern, Handler handler, std::string name) {
    impl_->pending.push_back(
        PendingRoute{method, std::move(pattern), std::move(handler), std::move(name)});
}

void Router::get(std::string_view pattern, Handler handler) {
    add(http::Method::get, require_pattern(pattern), std::move(handler));
}

void Router::get(PathPattern pattern, Handler handler) {
    add(http::Method::get, std::move(pattern), std::move(handler));
}

void Router::post(std::string_view pattern, Handler handler) {
    add(http::Method::post, require_pattern(pattern), std::move(handler));
}

void Router::put(std::string_view pattern, Handler handler) {
    add(http::Method::put, require_pattern(pattern), std::move(handler));
}

void Router::del(std::string_view pattern, Handler handler) {
    add(http::Method::delete_, require_pattern(pattern), std::move(handler));
}

void Router::use(Middleware middleware) {
    impl_->middleware.push_back(std::move(middleware));
}

void Router::set_trailing_slash(TrailingSlash policy) noexcept {
    impl_->trailing = policy;
}

void Router::set_case_insensitive(bool enabled) noexcept {
    impl_->case_insensitive = enabled;
}

fw::Result<void> Router::rebuild() {
    auto table = std::make_shared<CompiledTable>();
    for (auto const& pending : impl_->pending) {
        if (auto result = compile_one(*table, pending, impl_->middleware, impl_->case_insensitive);
            !result) {
            return result;
        }
    }
    impl_->snapshot.store(std::move(table));
    return {};
}

fw::task<http::Response> Router::dispatch(http::Request const& request) const {
    auto const table = impl_->snapshot.load();
    if (table == nullptr) {
        co_return http::Response::text(500, "router not built\n");
    }

    std::string_view path = request.path;
    if (impl_->trailing == TrailingSlash::ignore && path.size() > 1 && path.back() == '/') {
        path.remove_suffix(1);
    }

    std::array<std::string_view, kMaxSegments> storage{};
    auto const segment_count = split_path_into(path, storage.data(), storage.size());
    if (segment_count > kMaxSegments) {
        co_return http::Response::text(404, "not found\n");
    }
    auto const segments = std::span<std::string_view const>{storage.data(), segment_count};

    std::array<std::string_view, kMaxRouteParams> values{};
    MatchResult result;
    match_node(table->root, impl_->case_insensitive, segments, 0, path, request.method,
               values.data(), result, 0);

    if (result.route < 0) {
        if (result.mismatch != nullptr) {
            auto const allow = allow_header(*result.mismatch);
            if (request.method == http::Method::options) {
                http::Response response{204};
                response.set_header("Allow", allow);
                co_return response;
            }
            auto response = http::Response::text(405, "method not allowed\n");
            response.set_header("Allow", allow);
            co_return response;
        }
        co_return http::Response::text(404, "not found\n");
    }

    std::array<std::optional<std::string>, kMaxRouteParams> decoded{};
    for (std::size_t i = 0; i < result.param_count; ++i) {
        if (values[i].contains('%')) {
            decoded[i] = percent_decode(values[i]);
            values[i] = *decoded[i];
        }
    }

    auto const& route = table->routes[static_cast<std::size_t>(result.route)];
    RouteParams params;
    params.assign(route.param_names.data(), values.data(), result.param_count);
    RouteContext context{params, route.name, route.pattern};
    HandlerResult outcome = route.handler(request, context);
    if (outcome.ready()) {
        co_return std::move(outcome).take_value();
    }
    co_return co_await std::move(outcome).take_task();
}

std::size_t Router::route_count() const noexcept {
    auto const table = impl_->snapshot.load();
    return table != nullptr ? table->routes.size() : impl_->pending.size();
}

std::vector<RouteInfo> Router::routes() const {
    auto const table = impl_->snapshot.load();
    std::vector<RouteInfo> out;
    if (table == nullptr) {
        return out;
    }
    out.reserve(table->routes.size());
    for (auto const& route : table->routes) {
        out.push_back(RouteInfo{route.name, route.pattern, {route.method}});
    }
    return out;
}

RouteGroup Router::group(std::string_view prefix) {
    return RouteGroup{*this, std::string{prefix}, {}};
}

namespace {

std::string join_prefix(std::string prefix, std::string_view pattern) {
    if (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }
    prefix += pattern;
    return prefix;
}

Handler apply_middleware(Handler handler, std::vector<Middleware> const& middleware) {
    for (std::size_t i = middleware.size(); i-- > 0;) {
        handler = middleware[i](std::move(handler));
    }
    return handler;
}

} // namespace

RouteGroup::RouteGroup(Router& router, std::string prefix, std::vector<Middleware> middleware)
    : router_(&router), prefix_(std::move(prefix)), middleware_(std::move(middleware)) {}

void RouteGroup::add(http::Method method, std::string_view pattern, Handler handler) {
    handler = apply_middleware(std::move(handler), middleware_);
    router_->add(method, require_pattern(join_prefix(prefix_, pattern)), std::move(handler));
}

void RouteGroup::get(std::string_view pattern, Handler handler) {
    add(http::Method::get, pattern, std::move(handler));
}

void RouteGroup::post(std::string_view pattern, Handler handler) {
    add(http::Method::post, pattern, std::move(handler));
}

void RouteGroup::put(std::string_view pattern, Handler handler) {
    add(http::Method::put, pattern, std::move(handler));
}

void RouteGroup::del(std::string_view pattern, Handler handler) {
    add(http::Method::delete_, pattern, std::move(handler));
}

RouteGroup& RouteGroup::use(Middleware middleware) {
    middleware_.push_back(std::move(middleware));
    return *this;
}

RouteGroup RouteGroup::group(std::string_view prefix) const {
    return RouteGroup{*router_, join_prefix(prefix_, prefix), middleware_};
}

void RouteParams::assign(std::string const* names, std::string_view const* values,
                         std::size_t count) noexcept {
    names_ = names;
    values_ = values;
    count_ = count;
}

std::optional<std::string_view> RouteParams::get(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (names_[i] == name) {
            return values_[i];
        }
    }
    return std::nullopt;
}

Handler sync_handler(std::function<http::Response(http::Request const&, RouteContext&)> handler) {
    return [handler = std::move(handler)](http::Request const& request,
                                          RouteContext& context) -> HandlerResult {
        return handler(request, context);
    };
}

Handler async_handler(
    std::function<fw::task<http::Response>(http::Request const&, RouteContext&)> handler) {
    return [handler = std::move(handler)](http::Request const& request,
                                          RouteContext& context) -> HandlerResult {
        return handler(request, context);
    };
}

HandlerResult::HandlerResult(http::Response response) : state_(std::move(response)) {}

HandlerResult::HandlerResult(fw::task<http::Response> task) : state_(std::move(task)) {}

http::Response HandlerResult::take_value() && {
    return std::move(std::get<http::Response>(state_));
}

fw::task<http::Response> HandlerResult::take_task() && {
    return std::move(std::get<fw::task<http::Response>>(state_));
}

fw::task<http::Response> as_task(HandlerResult result) {
    if (result.ready()) {
        co_return std::move(result).take_value();
    }
    co_return co_await std::move(result).take_task();
}

} // namespace fw::server
