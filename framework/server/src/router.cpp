#include "fw/server/router.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fw::server {
namespace {

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

bool match_route(std::vector<std::string> const& pattern, std::vector<std::string_view> const& path,
                 RouteParams& params) {
    if (pattern.size() != path.size()) {
        return false;
    }
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        std::string const& segment = pattern[i];
        if (!segment.empty() && segment.front() == ':') {
            params.set(segment.substr(1), std::string{path[i]});
        } else if (segment != path[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

void RouteParams::set(std::string name, std::string value) {
    values_.emplace_back(std::move(name), std::move(value));
}

std::optional<std::string_view> RouteParams::get(std::string_view name) const {
    for (auto const& [key, value] : values_) {
        if (key == name) {
            return value;
        }
    }
    return std::nullopt;
}

void Router::add(http::Method method, std::string_view pattern, Handler handler) {
    Route route{method, {}, std::move(handler)};
    for (auto const segment : split_path(pattern)) {
        route.segments.emplace_back(segment);
    }
    routes_.push_back(std::move(route));
}

void Router::get(std::string_view pattern, Handler handler) {
    add(http::Method::get, pattern, std::move(handler));
}

void Router::post(std::string_view pattern, Handler handler) {
    add(http::Method::post, pattern, std::move(handler));
}

void Router::put(std::string_view pattern, Handler handler) {
    add(http::Method::put, pattern, std::move(handler));
}

void Router::del(std::string_view pattern, Handler handler) {
    add(http::Method::delete_, pattern, std::move(handler));
}

http::Response Router::dispatch(http::Request const& request) const {
    auto const path = split_path(request.path);

    bool path_matched = false;
    std::string allow;

    for (auto const& route : routes_) {
        RouteParams params;
        if (!match_route(route.segments, path, params)) {
            continue;
        }
        path_matched = true;
        if (route.method != request.method) {
            if (!allow.empty()) {
                allow += ", ";
            }
            allow += http::to_string(route.method);
            continue;
        }
        return route.handler(request, params);
    }

    if (path_matched) {
        auto response = http::Response::text(405, "method not allowed\n");
        response.set_header("Allow", allow);
        return response;
    }
    return http::Response::text(404, "not found\n");
}

} // namespace fw::server
