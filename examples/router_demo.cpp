#include "fw/execution/task.hpp"
#include "fw/http/response.hpp"
#include "fw/server/router.hpp"

#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace {

fw::http::Request make_request(fw::http::Method method, std::string path) {
    fw::http::Request request;
    request.method = method;
    request.path = std::move(path);
    return request;
}

fw::http::Response dispatch(fw::server::Router const& router, fw::http::Request const& request) {
    auto result = fw::sync_wait(router.dispatch(request));
    return result ? std::move(std::get<0>(*result)) : fw::http::Response::text(0, "<none>");
}

void show(fw::server::Router const& router, fw::http::Method method, std::string_view path) {
    auto const response = dispatch(router, make_request(method, std::string{path}));
    std::cout << std::setw(7) << std::left << fw::http::to_string(method) << ' ' << std::setw(30)
              << std::left << path << " -> " << response.status() << "  " << response.body()
              << '\n';
}

fw::server::Handler json_handler(std::string body) {
    return fw::server::sync_handler(
        [body = std::move(body)](fw::http::Request const&, fw::server::RouteContext&) {
            return fw::http::Response::json(200, body);
        });
}

} // namespace

int main() {
    fw::server::Router router;

    // Global middleware: runs outermost, wraps every route.
    router.use([](fw::server::Handler next) {
        return [next = std::move(next)](
                   fw::http::Request const& request,
                   fw::server::RouteContext& context) -> fw::task<fw::http::Response> {
            auto response = co_await fw::server::as_task(next(request, context));
            response.set_header("X-Middleware", "global");
            co_return response;
        };
    });

    // Static route, synchronous handler.
    router.get("/health", json_handler(R"({"status":"ok"})"));

    // `:name` parameter (also writable as `{name}`).
    router.get("/users/:id", fw::server::sync_handler([](fw::http::Request const&,
                                                         fw::server::RouteContext& context) {
                   auto const id = context.params().get("id").value_or("?");
                   return fw::http::Response::text(200, std::string{"user "} + std::string{id});
               }));

    // Named constraint `{id:int}`.
    router.get("/orders/{id:int}", fw::server::sync_handler([](fw::http::Request const&,
                                                               fw::server::RouteContext& context) {
                   auto const id = context.params().get("id").value_or("?");
                   return fw::http::Response::text(200, std::string{"order "} + std::string{id});
               }));

    // Inline PCRE2 regex constraint `{name:/re/}`.
    router.get(
        "/files/{name:/[a-z0-9_.]+/}",
        fw::server::sync_handler([](fw::http::Request const&, fw::server::RouteContext& context) {
            auto const name = context.params().get("name").value_or("?");
            return fw::http::Response::text(200, std::string{"file "} + std::string{name});
        }));

    // Two parameters sharing a prefix, distinguished by constraint.
    router.get("/items/{id:int}", fw::server::sync_handler([](fw::http::Request const&,
                                                              fw::server::RouteContext& context) {
                   auto const id = context.params().get("id").value_or("?");
                   return fw::http::Response::text(200, std::string{"item #"} + std::string{id});
               }));
    router.get(
        "/items/{code:slug}",
        fw::server::sync_handler([](fw::http::Request const&, fw::server::RouteContext& context) {
            auto const code = context.params().get("code").value_or("?");
            return fw::http::Response::text(200, std::string{"item code "} + std::string{code});
        }));

    // Catch-all `{path:*}` (must be the last segment).
    router.get("/static/{path:*}", fw::server::sync_handler([](fw::http::Request const&,
                                                               fw::server::RouteContext& context) {
                   auto const path = context.params().get("path").value_or("");
                   return fw::http::Response::text(200, std::string{"serve "} + std::string{path});
               }));

    // Optional segment `{id?}`.
    router.get("/articles/{id?}", fw::server::sync_handler([](fw::http::Request const&,
                                                              fw::server::RouteContext& context) {
                   auto const id = context.params().get("id");
                   return fw::http::Response::text(
                       200, id ? std::string{"article "} + std::string{*id} : "article list");
               }));

    // Asynchronous handler (coroutine).
    router.get("/slow", fw::server::async_handler(
                            [](fw::http::Request const&,
                               fw::server::RouteContext&) -> fw::task<fw::http::Response> {
                                co_return fw::http::Response::text(200, "async done");
                            }));

    // Group: shared prefix + its own middleware stack.
    auto api = router.group("/api");
    api.use([](fw::server::Handler next) {
        return [next = std::move(next)](
                   fw::http::Request const& request,
                   fw::server::RouteContext& context) -> fw::task<fw::http::Response> {
            if (request.header("x-token").value_or("") != "secret") {
                co_return fw::http::Response::text(401, "unauthorized");
            }
            co_return co_await fw::server::as_task(next(request, context));
        };
    });
    api.get("/me", json_handler(R"({"me":"ok"})"));

    // Compile + validate; swap in the immutable snapshot atomically.
    if (auto built = router.rebuild(); !built) {
        std::cerr << "router rebuild failed\n";
        return 1;
    }

    std::cout << "registered routes (" << router.route_count() << "):\n";
    for (auto const& info : router.routes()) {
        std::cout << "  " << info.pattern << '\n';
    }
    std::cout << '\n';

    show(router, fw::http::Method::get, "/health");
    show(router, fw::http::Method::get, "/users/42");
    show(router, fw::http::Method::get, "/orders/123");
    show(router, fw::http::Method::get, "/orders/abc"); // constraint -> 404
    show(router, fw::http::Method::get, "/files/report_2024.pdf");
    show(router, fw::http::Method::get, "/items/7");
    show(router, fw::http::Method::get, "/items/abc-2");
    show(router, fw::http::Method::get, "/static/css/app.css");
    show(router, fw::http::Method::get, "/articles");
    show(router, fw::http::Method::get, "/articles/7");
    show(router, fw::http::Method::get, "/slow");
    show(router, fw::http::Method::post, "/health");    // 405 + Allow
    show(router, fw::http::Method::options, "/health"); // 204 + Allow
    show(router, fw::http::Method::get, "/nope");       // 404

    {
        auto const response = dispatch(router, make_request(fw::http::Method::get, "/api/me"));
        std::cout << "\nGET /api/me (no token) -> " << response.status() << "  " << response.body()
                  << '\n';
    }
    {
        auto request = make_request(fw::http::Method::get, "/api/me");
        request.headers.push_back(fw::http::Header{"X-Token", "secret"});
        auto const response = dispatch(router, request);
        std::cout << "GET /api/me (token)    -> " << response.status() << "  " << response.body()
                  << '\n';
    }
    return 0;
}
