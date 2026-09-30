#pragma once

#include "fw/http/request.hpp"
#include "fw/http/response.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fw::server {

class RouteParams {
public:
    void set(std::string name, std::string value);
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const;
    [[nodiscard]] std::size_t size() const noexcept {
        return values_.size();
    }
    [[nodiscard]] bool empty() const noexcept {
        return values_.empty();
    }

private:
    std::vector<std::pair<std::string, std::string>> values_;
};

using Handler = std::function<http::Response(http::Request const&, RouteParams const&)>;

class Router {
public:
    void add(http::Method method, std::string_view pattern, Handler handler);
    void get(std::string_view pattern, Handler handler);
    void post(std::string_view pattern, Handler handler);
    void put(std::string_view pattern, Handler handler);
    void del(std::string_view pattern, Handler handler);

    // Dispatches `request`. Patterns may contain `:name` segments that capture a
    // single path segment. Returns 404 when nothing matches or 405 (with an
    // `Allow` header) when the path matches but the method does not.
    [[nodiscard]] http::Response dispatch(http::Request const& request) const;

    [[nodiscard]] std::size_t size() const noexcept {
        return routes_.size();
    }

private:
    struct Route {
        http::Method method;
        std::vector<std::string> segments;
        Handler handler;
    };

    std::vector<Route> routes_;
};

} // namespace fw::server
