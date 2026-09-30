#include "fw/server/router.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

fw::http::Request make_request(fw::http::Method method, std::string path) {
    fw::http::Request request;
    request.method = method;
    request.path = std::move(path);
    return request;
}

fw::server::Router make_health_router() {
    fw::server::Router router;
    router.get("/health", [](fw::http::Request const&, fw::server::RouteParams const&) {
        return fw::http::Response::text(200, "ok");
    });
    return router;
}

} // namespace

TEST(Router, MatchesStaticRoute) {
    auto const router = make_health_router();
    auto const response = router.dispatch(make_request(fw::http::Method::get, "/health"));
    EXPECT_EQ(response.status(), 200);
    EXPECT_EQ(response.body(), "ok");
}

TEST(Router, ReturnsNotFound) {
    auto const router = make_health_router();
    auto const response = router.dispatch(make_request(fw::http::Method::get, "/missing"));
    EXPECT_EQ(response.status(), 404);
}

TEST(Router, ReturnsMethodNotAllowed) {
    auto const router = make_health_router();
    auto const response = router.dispatch(make_request(fw::http::Method::post, "/health"));
    EXPECT_EQ(response.status(), 405);
    ASSERT_TRUE(response.header("Allow").has_value());
    EXPECT_NE(response.header("Allow")->find("GET"), std::string::npos);
}

TEST(Router, CapturesPathParams) {
    fw::server::Router router;
    router.get("/api/contests/:id/standings",
               [](fw::http::Request const&, fw::server::RouteParams const& params) {
                   auto const id = params.get("id");
                   return fw::http::Response::text(200, id ? std::string{*id} : std::string{});
               });

    auto const response =
        router.dispatch(make_request(fw::http::Method::get, "/api/contests/42/standings"));
    EXPECT_EQ(response.status(), 200);
    EXPECT_EQ(response.body(), "42");
}

TEST(Router, DistinguishesSegments) {
    fw::server::Router router;
    router.get("/api/contests", [](fw::http::Request const&, fw::server::RouteParams const&) {
        return fw::http::Response::text(200, "list");
    });
    router.get("/api/contests/:id", [](fw::http::Request const&, fw::server::RouteParams const&) {
        return fw::http::Response::text(200, "one");
    });

    EXPECT_EQ(router.dispatch(make_request(fw::http::Method::get, "/api/contests")).body(), "list");
    EXPECT_EQ(router.dispatch(make_request(fw::http::Method::get, "/api/contests/7")).body(),
              "one");
}
