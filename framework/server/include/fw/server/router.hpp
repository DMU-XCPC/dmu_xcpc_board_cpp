#pragma once

#include "fw/core/error.hpp"
#include "fw/execution/any_task.hpp"
#include "fw/http/request.hpp"
#include "fw/http/response.hpp"
#include "fw/server/pattern.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fw::server {

constexpr std::size_t kMethodCount = static_cast<std::size_t>(http::Method::other) + 1;
constexpr std::size_t kMaxRouteParams = 16;

// Captured path parameters for one dispatch. Values are non-owning views into
// the request path; names point at the matched route's metadata.
class RouteParams {
public:
    void assign(std::string const* names, std::string_view const* values,
                std::size_t count) noexcept;
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept {
        return count_;
    }

private:
    std::string const* names_ = nullptr;
    std::string_view const* values_ = nullptr;
    std::size_t count_ = 0;
};

// Per-dispatch context passed to handlers and middleware.
class RouteContext {
public:
    RouteContext(RouteParams params, std::string_view name, std::string_view pattern) noexcept
        : params_(params), name_(name), pattern_(pattern) {}

    [[nodiscard]] RouteParams const& params() const noexcept {
        return params_;
    }
    [[nodiscard]] std::string_view name() const noexcept {
        return name_;
    }
    [[nodiscard]] std::string_view pattern() const noexcept {
        return pattern_;
    }

private:
    RouteParams params_;
    std::string_view name_;
    std::string_view pattern_;
};

// Every handler returns either a ready response or an asynchronous task.
// Implicit conversions from `http::Response` and `fw::task<http::Response>`
// mean handlers can just `return` a response (zero-allocation fast path) or be
// a coroutine.
class HandlerResult {
public:
    HandlerResult(http::Response response);
    HandlerResult(fw::task<http::Response> task);

    [[nodiscard]] bool ready() const noexcept {
        return std::holds_alternative<http::Response>(state_);
    }

    [[nodiscard]] http::Response take_value() &&;
    [[nodiscard]] fw::task<http::Response> take_task() &&;

private:
    std::variant<http::Response, fw::task<http::Response>> state_;
};

// Convert any handler result into a task (used by middleware).
[[nodiscard]] fw::task<http::Response> as_task(HandlerResult result);

using Handler = std::function<HandlerResult(http::Request const&, RouteContext&)>;

Handler sync_handler(std::function<http::Response(http::Request const&, RouteContext&)> handler);
Handler
async_handler(std::function<fw::task<http::Response>(http::Request const&, RouteContext&)> handler);

using Middleware = std::function<Handler(Handler)>;

// A registered route, for introspection.
struct RouteInfo {
    std::string name;
    std::string pattern;
    std::vector<http::Method> methods;
};

// How a request path with a trailing slash is matched.
enum class TrailingSlash : std::uint8_t { strict, ignore };

class Router;

// A group shares a path prefix and a middleware stack; groups compose.
class RouteGroup {
public:
    RouteGroup(Router& router, std::string prefix, std::vector<Middleware> middleware = {});

    void add(http::Method method, std::string_view pattern, Handler handler);
    void get(std::string_view pattern, Handler handler);
    void post(std::string_view pattern, Handler handler);
    void put(std::string_view pattern, Handler handler);
    void del(std::string_view pattern, Handler handler);

    RouteGroup& use(Middleware middleware);
    [[nodiscard]] RouteGroup group(std::string_view prefix) const;

private:
    Router* router_;
    std::string prefix_;
    std::vector<Middleware> middleware_;
};

class Router {
public:
    Router();
    ~Router();
    Router(Router const&) = delete;
    Router& operator=(Router const&) = delete;
    Router(Router&&) noexcept;
    Router& operator=(Router&&) noexcept;

    void add(http::Method method, PathPattern pattern, Handler handler, std::string name = {});
    void get(std::string_view pattern, Handler handler);
    void get(PathPattern pattern, Handler handler);
    void post(std::string_view pattern, Handler handler);
    void put(std::string_view pattern, Handler handler);
    void del(std::string_view pattern, Handler handler);

    void use(Middleware middleware);

    void set_trailing_slash(TrailingSlash policy) noexcept;
    void set_case_insensitive(bool enabled) noexcept;

    [[nodiscard]] RouteGroup group(std::string_view prefix);

    // Compile the pending routes into an immutable snapshot and swap it in
    // atomically. Concurrent dispatches always observe a consistent table.
    [[nodiscard]] fw::Result<void> rebuild();

    [[nodiscard]] fw::task<http::Response> dispatch(http::Request const& request) const;

    [[nodiscard]] std::size_t route_count() const noexcept;
    [[nodiscard]] std::vector<RouteInfo> routes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fw::server
