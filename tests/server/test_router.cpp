#include "fw/server/router.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

fw::http::Request make_request(fw::http::Method method, std::string path) {
    fw::http::Request request;
    request.method = method;
    request.path = std::move(path);
    return request;
}

fw::http::Response dispatch(fw::server::Router const& router, fw::http::Request const& request) {
    auto result = fw::sync_wait(router.dispatch(request));
    return result ? std::move(std::get<0>(*result)) : fw::http::Response::text(0, "");
}

fw::server::Handler text_handler(std::string body) {
    return fw::server::sync_handler(
        [body = std::move(body)](fw::http::Request const&, fw::server::RouteContext&) {
            return fw::http::Response::text(200, body);
        });
}

fw::server::Router make_health_router() {
    fw::server::Router router;
    router.get("/health", text_handler("ok"));
    EXPECT_TRUE(router.rebuild().has_value());
    return router;
}

} // namespace

TEST(Router, MatchesStaticRoute) {
    auto const router = make_health_router();
    auto const response = dispatch(router, make_request(fw::http::Method::get, "/health"));
    EXPECT_EQ(response.status(), 200);
    EXPECT_EQ(response.body(), "ok");
}

TEST(Router, ReturnsNotFound) {
    auto const router = make_health_router();
    auto const response = dispatch(router, make_request(fw::http::Method::get, "/missing"));
    EXPECT_EQ(response.status(), 404);
}

TEST(Router, ReturnsMethodNotAllowed) {
    auto const router = make_health_router();
    auto const response = dispatch(router, make_request(fw::http::Method::post, "/health"));
    EXPECT_EQ(response.status(), 405);
    ASSERT_TRUE(response.header("Allow").has_value());
    EXPECT_NE(response.header("Allow")->find("GET"), std::string::npos);
}

TEST(Router, CapturesPathParams) {
    fw::server::Router router;
    router.get(
        "/api/contests/:id/standings",
        fw::server::sync_handler([](fw::http::Request const&, fw::server::RouteContext& context) {
            auto const id = context.params().get("id");
            return fw::http::Response::text(200, id ? std::string{*id} : std::string{});
        }));
    ASSERT_TRUE(router.rebuild().has_value());

    auto const response =
        dispatch(router, make_request(fw::http::Method::get, "/api/contests/42/standings"));
    EXPECT_EQ(response.status(), 200);
    EXPECT_EQ(response.body(), "42");
}

TEST(Router, PrefersLiteralOverParameter) {
    fw::server::Router router;
    router.get("/users/me", text_handler("me"));
    router.get("/users/:id", text_handler("id"));
    ASSERT_TRUE(router.rebuild().has_value());

    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/users/me")).body(), "me");
    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/users/42")).body(), "id");
}

TEST(Router, BacktracksToParameter) {
    fw::server::Router router;
    router.get("/a/b", text_handler("ab"));
    router.get("/:x/c", text_handler("a"));
    ASSERT_TRUE(router.rebuild().has_value());

    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/a/b")).body(), "ab");
    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/a/c")).body(), "a");
}

TEST(Router, EnforcesParameterConstraint) {
    fw::server::Router router;
    auto pattern = fw::server::PathPattern::from_segments({
        fw::server::RouteSegment::literal("users"),
        fw::server::RouteSegment::parameter("id", fw::server::make_constraint("int")),
    });
    router.get(std::move(pattern), text_handler("constrained"));
    ASSERT_TRUE(router.rebuild().has_value());

    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/users/42")).body(),
              "constrained");
    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/users/abc")).status(), 404);
}

TEST(Router, MultipleConstrainedParamsSharePrefix) {
    fw::server::Router router;
    router.get("/users/{id:int}", fw::server::sync_handler([](fw::http::Request const&,
                                                              fw::server::RouteContext& context) {
                   auto const id = context.params().get("id").value_or("");
                   return fw::http::Response::text(200, std::string{"id="} + std::string{id});
               }));
    router.get(
        "/users/{slug:slug}",
        fw::server::sync_handler([](fw::http::Request const&, fw::server::RouteContext& context) {
            auto const slug = context.params().get("slug").value_or("");
            return fw::http::Response::text(200, std::string{"slug="} + std::string{slug});
        }));
    ASSERT_TRUE(router.rebuild().has_value());

    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/users/42")).body(), "id=42");
    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/users/abc")).body(),
              "slug=abc");
}

TEST(Router, RejectsAmbiguousUnconstrainedParams) {
    fw::server::Router router;
    router.get("/x/:a", text_handler("a"));
    router.get("/x/:b", text_handler("b"));
    EXPECT_FALSE(router.rebuild().has_value());
}

TEST(Router, ConstrainedParamTriedBeforeUnconstrained) {
    fw::server::Router router;
    router.get("/n/{id:int}", fw::server::sync_handler([](fw::http::Request const&,
                                                          fw::server::RouteContext& context) {
                   auto const id = context.params().get("id").value_or("");
                   return fw::http::Response::text(200, std::string{"int="} + std::string{id});
               }));
    router.get("/n/{name}", fw::server::sync_handler([](fw::http::Request const&,
                                                        fw::server::RouteContext& context) {
                   auto const name = context.params().get("name").value_or("");
                   return fw::http::Response::text(200, std::string{"str="} + std::string{name});
               }));
    ASSERT_TRUE(router.rebuild().has_value());

    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/n/9")).body(), "int=9");
    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/n/x")).body(), "str=x");
}

TEST(Router, CapturesCatchAll) {
    fw::server::Router router;
    auto pattern = fw::server::PathPattern::from_segments(
        {fw::server::RouteSegment::literal("files"), fw::server::RouteSegment::catch_all("path")});
    router.get(std::move(pattern), fw::server::sync_handler([](fw::http::Request const&,
                                                               fw::server::RouteContext& context) {
                   auto const path = context.params().get("path");
                   return fw::http::Response::text(200, path ? std::string{*path} : std::string{});
               }));
    ASSERT_TRUE(router.rebuild().has_value());

    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/files/a/b/c")).body(),
              "a/b/c");
}

TEST(Router, RejectsConflictingParameterNames) {
    fw::server::Router router;
    router.get("/users/:id", text_handler("id"));
    router.get("/users/:name", text_handler("name"));
    EXPECT_FALSE(router.rebuild().has_value());
}

TEST(Router, RejectsCatchAllNotLast) {
    fw::server::Router router;
    auto pattern = fw::server::PathPattern::from_segments(
        {fw::server::RouteSegment::catch_all("rest"), fw::server::RouteSegment::literal("x")});
    router.get(std::move(pattern), text_handler("bad"));
    EXPECT_FALSE(router.rebuild().has_value());
}

TEST(Router, RunsMiddleware) {
    fw::server::Router router;
    router.use([](fw::server::Handler next) {
        return [next = std::move(next)](
                   fw::http::Request const& request,
                   fw::server::RouteContext& context) -> fw::task<fw::http::Response> {
            auto response = co_await fw::server::as_task(next(request, context));
            response.set_header("X-Middleware", "yes");
            co_return response;
        };
    });
    router.get("/health", text_handler("ok"));
    ASSERT_TRUE(router.rebuild().has_value());

    auto const response = dispatch(router, make_request(fw::http::Method::get, "/health"));
    ASSERT_TRUE(response.header("X-Middleware").has_value());
    EXPECT_EQ(*response.header("X-Middleware"), "yes");
}

TEST(Router, SupportsAsyncHandler) {
    fw::server::Router router;
    router.get("/async", fw::server::async_handler(
                             [](fw::http::Request const&,
                                fw::server::RouteContext&) -> fw::task<fw::http::Response> {
                                 co_return fw::http::Response::text(200, "async");
                             }));
    ASSERT_TRUE(router.rebuild().has_value());

    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/async")).body(), "async");
}

TEST(Router, ReportsRouteCount) {
    fw::server::Router router;
    router.get("/a", text_handler(""));
    router.post("/a", text_handler(""));
    router.get("/b/:id", text_handler(""));
    ASSERT_TRUE(router.rebuild().has_value());
    EXPECT_EQ(router.route_count(), 3U);
}

TEST(Router, ParsesConstraintSyntax) {
    fw::server::Router router;
    router.get("/users/{id:int}", fw::server::sync_handler([](fw::http::Request const&,
                                                              fw::server::RouteContext& context) {
                   auto const id = context.params().get("id").value_or("");
                   return fw::http::Response::text(200, std::string{id});
               }));
    ASSERT_TRUE(router.rebuild().has_value());

    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/users/42")).body(), "42");
    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/users/abc")).status(), 404);
}

TEST(Router, ParsesCatchAllSyntax) {
    fw::server::Router router;
    router.get("/files/{path:*}", fw::server::sync_handler([](fw::http::Request const&,
                                                              fw::server::RouteContext& context) {
                   auto const path = context.params().get("path").value_or("");
                   return fw::http::Response::text(200, std::string{path});
               }));
    ASSERT_TRUE(router.rebuild().has_value());

    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/files/a/b/c")).body(),
              "a/b/c");
}

TEST(Router, OptionalSegmentMatchesWithAndWithout) {
    fw::server::Router router;
    router.get("/a/{b?}/c", fw::server::sync_handler([](fw::http::Request const&,
                                                        fw::server::RouteContext& context) {
                   auto const b = context.params().get("b");
                   return fw::http::Response::text(200, b ? std::string{*b} : std::string{"none"});
               }));
    ASSERT_TRUE(router.rebuild().has_value());

    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/a/x/c")).body(), "x");
    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/a/c")).body(), "none");
}

TEST(Router, HeadFallsBackToGet) {
    fw::server::Router router;
    router.get("/x", text_handler("x"));
    ASSERT_TRUE(router.rebuild().has_value());

    auto const response = dispatch(router, make_request(fw::http::Method::head, "/x"));
    EXPECT_EQ(response.status(), 200);
    EXPECT_EQ(response.body(), "x");
}

TEST(Router, OptionsReportsAllowedMethods) {
    fw::server::Router router;
    router.get("/x", text_handler("x"));
    router.post("/x", text_handler("x"));
    ASSERT_TRUE(router.rebuild().has_value());

    auto const response = dispatch(router, make_request(fw::http::Method::options, "/x"));
    EXPECT_EQ(response.status(), 204);
    ASSERT_TRUE(response.header("Allow").has_value());
    EXPECT_NE(response.header("Allow")->find("GET"), std::string::npos);
    EXPECT_NE(response.header("Allow")->find("POST"), std::string::npos);
}

TEST(Router, GroupPrefix) {
    fw::server::Router router;
    router.group("/api").get("/ping", text_handler("pong"));
    ASSERT_TRUE(router.rebuild().has_value());

    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/api/ping")).body(), "pong");
}

TEST(Router, GroupMiddleware) {
    fw::server::Router router;
    auto api = router.group("/api");
    api.use([](fw::server::Handler next) {
        return [next = std::move(next)](
                   fw::http::Request const& request,
                   fw::server::RouteContext& context) -> fw::task<fw::http::Response> {
            auto response = co_await fw::server::as_task(next(request, context));
            response.set_header("X-Group", "1");
            co_return response;
        };
    });
    api.get("/ping", text_handler("pong"));
    ASSERT_TRUE(router.rebuild().has_value());

    auto const response = dispatch(router, make_request(fw::http::Method::get, "/api/ping"));
    EXPECT_EQ(response.body(), "pong");
    ASSERT_TRUE(response.header("X-Group").has_value());
    EXPECT_EQ(*response.header("X-Group"), "1");
}

TEST(Router, RejectsUnknownConstraint) {
    EXPECT_FALSE(fw::server::PathPattern::parse("/x/{id:bogus}").has_value());
}

TEST(Router, ThrowsOnInvalidPatternString) {
    fw::server::Router router;
    EXPECT_THROW(router.get("/x/{id:bogus}", text_handler("x")), std::invalid_argument);
}

TEST(Router, AcceptsPlainResponseLambda) {
    fw::server::Router router;
    router.get("/plain", [](fw::http::Request const&, fw::server::RouteContext&) {
        return fw::http::Response::text(200, "plain");
    });
    ASSERT_TRUE(router.rebuild().has_value());
    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/plain")).body(), "plain");
}

TEST(Router, EnforcesRegexConstraint) {
    fw::server::Router router;
    router.get(
        "/u/{name:/[a-z]+/}",
        fw::server::sync_handler([](fw::http::Request const&, fw::server::RouteContext& context) {
            auto const name = context.params().get("name").value_or("");
            return fw::http::Response::text(200, std::string{name});
        }));
    ASSERT_TRUE(router.rebuild().has_value());

    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/u/abc")).body(), "abc");
    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/u/ABC")).status(), 404);
}

TEST(Router, DecodesPercentEncodedParams) {
    fw::server::Router router;
    router.get("/u/:name", fw::server::sync_handler(
                               [](fw::http::Request const&, fw::server::RouteContext& context) {
                                   auto const name = context.params().get("name").value_or("");
                                   return fw::http::Response::text(200, std::string{name});
                               }));
    ASSERT_TRUE(router.rebuild().has_value());

    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/u/a%20b")).body(), "a b");
}

TEST(Router, TrailingSlashPolicy) {
    fw::server::Router strict;
    strict.get("/health", text_handler("ok"));
    ASSERT_TRUE(strict.rebuild().has_value());
    EXPECT_EQ(dispatch(strict, make_request(fw::http::Method::get, "/health/")).status(), 404);

    fw::server::Router relaxed;
    relaxed.set_trailing_slash(fw::server::TrailingSlash::ignore);
    relaxed.get("/health", text_handler("ok"));
    ASSERT_TRUE(relaxed.rebuild().has_value());
    EXPECT_EQ(dispatch(relaxed, make_request(fw::http::Method::get, "/health/")).body(), "ok");
}

TEST(Router, CaseInsensitiveMatching) {
    fw::server::Router router;
    router.set_case_insensitive(true);
    router.get("/Health", text_handler("ok"));
    ASSERT_TRUE(router.rebuild().has_value());

    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/health")).body(), "ok");
    EXPECT_EQ(dispatch(router, make_request(fw::http::Method::get, "/HEALTH")).body(), "ok");
}

TEST(Router, IntrospectsRoutes) {
    fw::server::Router router;
    router.add(fw::http::Method::get, *fw::server::PathPattern::parse("/a"), text_handler(""),
               "route-a");
    router.post("/b/:id", text_handler(""));
    ASSERT_TRUE(router.rebuild().has_value());

    auto const routes = router.routes();
    ASSERT_EQ(routes.size(), 2U);
    EXPECT_EQ(routes[0].name, "route-a");
    EXPECT_EQ(routes[0].pattern, "/a");
    EXPECT_EQ(routes[1].pattern, "/b/:id");
}
