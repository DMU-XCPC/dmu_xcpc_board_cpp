#include "fw/http/request.hpp"

#include <gtest/gtest.h>

#include <string>

TEST(HttpRequest, SerializesGetWithoutBody) {
    fw::http::Request request;
    request.method = fw::http::Method::get;
    request.target = "/health";

    EXPECT_EQ(request.serialize(), "GET /health HTTP/1.1\r\n\r\n");
}

TEST(HttpRequest, AddsContentLengthForBody) {
    fw::http::Request request;
    request.method = fw::http::Method::post;
    request.target = "/teams";
    request.body = R"({"name":"a"})";

    auto const text = request.serialize();
    EXPECT_NE(text.find("Content-Length: 12\r\n"), std::string::npos);
    EXPECT_TRUE(text.ends_with("\r\n\r\n" + request.body));
}

TEST(HttpRequest, DerivesTargetFromPathAndQuery) {
    fw::http::Request request;
    request.method = fw::http::Method::get;
    request.path = "/standings";
    request.query = "contest=42";

    EXPECT_TRUE(request.serialize().starts_with("GET /standings?contest=42 HTTP/1.1\r\n"));
}

TEST(HttpRequest, KeepsExplicitContentLength) {
    fw::http::Request request;
    request.method = fw::http::Method::post;
    request.target = "/x";
    request.body = "abc";
    request.headers.push_back(fw::http::Header{"Content-Length", "3"});

    auto const text = request.serialize();
    EXPECT_EQ(text.find("Content-Length: 3\r\nContent-Length: 3\r\n"), std::string::npos);
}
