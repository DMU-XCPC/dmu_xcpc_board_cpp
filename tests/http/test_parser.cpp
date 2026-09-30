#include "fw/http/parser.hpp"

#include <gtest/gtest.h>

#include <string>

TEST(HttpParser, ParsesGetRequest) {
    std::string const raw = "GET /api/standings?contest=42 HTTP/1.1\r\n"
                            "Host: example.com\r\n"
                            "Accept: application/json\r\n"
                            "\r\n";
    fw::http::Request request;

    auto const result = fw::http::parse_request(raw, request);
    ASSERT_EQ(result.state, fw::http::ParseState::complete);
    EXPECT_EQ(result.consumed, raw.size());
    EXPECT_EQ(request.method, fw::http::Method::get);
    EXPECT_EQ(request.target, "/api/standings?contest=42");
    EXPECT_EQ(request.path, "/api/standings");
    EXPECT_EQ(request.query, "contest=42");
    ASSERT_TRUE(request.header("host").has_value());
    EXPECT_EQ(*request.header("host"), "example.com");
    EXPECT_EQ(request.header("HOST").value(), "example.com");
    EXPECT_TRUE(request.body.empty());
}

TEST(HttpParser, ParsesPostWithBody) {
    std::string const body = R"({"name":"team"})";
    std::string const raw =
        "POST /teams HTTP/1.1\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" +
        body;
    fw::http::Request request;

    auto const result = fw::http::parse_request(raw, request);
    ASSERT_EQ(result.state, fw::http::ParseState::complete);
    EXPECT_EQ(request.method, fw::http::Method::post);
    EXPECT_EQ(request.body, body);
}

TEST(HttpParser, IncompleteWhenHeaderMissing) {
    fw::http::Request request;
    auto const result = fw::http::parse_request("GET / HTTP/1.1\r\nHost: x\r\n", request);
    EXPECT_EQ(result.state, fw::http::ParseState::incomplete);
}

TEST(HttpParser, IncompleteWhenBodyMissing) {
    fw::http::Request request;
    auto const result =
        fw::http::parse_request("POST /x HTTP/1.1\r\nContent-Length: 5\r\n\r\nab", request);
    EXPECT_EQ(result.state, fw::http::ParseState::incomplete);
}

TEST(HttpParser, RejectsMalformed) {
    fw::http::Request request;
    auto const result = fw::http::parse_request("this is not http\r\n\r\n", request);
    EXPECT_EQ(result.state, fw::http::ParseState::error);
}

TEST(HttpParser, RejectsChunkedEncoding) {
    fw::http::Request request;
    auto const result = fw::http::parse_request(
        "POST /x HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", request);
    EXPECT_EQ(result.state, fw::http::ParseState::error);
}
