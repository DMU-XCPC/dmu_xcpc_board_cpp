#include "fw/http/request.hpp"

namespace fw::http {

std::string_view to_string(Method method) noexcept {
    switch (method) {
    case Method::get:
        return "GET";
    case Method::head:
        return "HEAD";
    case Method::post:
        return "POST";
    case Method::put:
        return "PUT";
    case Method::delete_:
        return "DELETE";
    case Method::patch:
        return "PATCH";
    case Method::options:
        return "OPTIONS";
    case Method::connect:
        return "CONNECT";
    case Method::trace:
        return "TRACE";
    case Method::other:
        return "OTHER";
    }
    return "OTHER";
}

Method method_from_string(std::string_view name) noexcept {
    if (name == "GET") {
        return Method::get;
    }
    if (name == "HEAD") {
        return Method::head;
    }
    if (name == "POST") {
        return Method::post;
    }
    if (name == "PUT") {
        return Method::put;
    }
    if (name == "DELETE") {
        return Method::delete_;
    }
    if (name == "PATCH") {
        return Method::patch;
    }
    if (name == "OPTIONS") {
        return Method::options;
    }
    if (name == "CONNECT") {
        return Method::connect;
    }
    if (name == "TRACE") {
        return Method::trace;
    }
    return Method::other;
}

std::optional<std::string_view> Request::header(std::string_view name) const {
    for (auto const& field : headers) {
        if (iequals(field.name, name)) {
            return field.value;
        }
    }
    return std::nullopt;
}

bool Request::has_header(std::string_view name) const {
    return header(name).has_value();
}

void Request::clear() {
    method = Method::get;
    target.clear();
    path.clear();
    query.clear();
    minor_version = 1;
    headers.clear();
    body.clear();
}

} // namespace fw::http
