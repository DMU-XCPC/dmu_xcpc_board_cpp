#include "fw/http/response.hpp"

#include <gtest/gtest.h>

#include <string>

TEST(HttpResponse, SerializesStatusHeadersBody) {
    auto const response = fw::http::Response::text(200, "hello");
    auto const text = response.serialize();

    EXPECT_EQ(text.substr(0, 17), "HTTP/1.1 200 OK\r\n");
    EXPECT_NE(text.find("Content-Type: text/plain; charset=utf-8\r\n"), std::string::npos);
    EXPECT_NE(text.find("Content-Length: 5\r\n"), std::string::npos);
    EXPECT_NE(text.find("\r\n\r\nhello"), std::string::npos);
}

TEST(HttpResponse, JsonSetsContentType) {
    auto const response = fw::http::Response::json(404, "{}");
    EXPECT_EQ(response.status(), 404);
    ASSERT_TRUE(response.header("content-type").has_value());
    EXPECT_EQ(*response.header("content-type"), "application/json");
    EXPECT_EQ(response.body(), "{}");
}

TEST(HttpResponse, KeepsExplicitContentLength) {
    fw::http::Response response{200};
    response.set_header("Content-Length", "0");
    response.set_body("ignored");
    auto const text = response.serialize();
    EXPECT_NE(text.find("Content-Length: 0\r\n"), std::string::npos);
}

TEST(HttpResponse, ReasonPhrases) {
    EXPECT_EQ(fw::http::reason_phrase(404), "Not Found");
    EXPECT_EQ(fw::http::reason_phrase(500), "Internal Server Error");
}
