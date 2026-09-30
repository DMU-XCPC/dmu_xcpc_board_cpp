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

std::string Request::serialize() const {
    std::string out;
    out.reserve(128 + body.size());

    out += to_string(method);
    out += ' ';
    if (!target.empty()) {
        out += target;
    } else {
        out += path;
        if (!query.empty()) {
            out += '?';
            out += query;
        }
    }
    out += " HTTP/1.1\r\n";

    bool has_content_length = false;
    for (auto const& field : headers) {
        if (iequals(field.name, "Content-Length")) {
            has_content_length = true;
        }
        out += field.name;
        out += ": ";
        out += field.value;
        out += "\r\n";
    }
    if (!has_content_length && !body.empty()) {
        out += "Content-Length: ";
        out += std::to_string(body.size());
        out += "\r\n";
    }

    out += "\r\n";
    out += body;
    return out;
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
