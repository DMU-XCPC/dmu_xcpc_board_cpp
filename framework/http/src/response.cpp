#include "fw/http/response.hpp"

#include <utility>

namespace fw::http {

std::string_view reason_phrase(int status) noexcept {
    switch (status) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 202:
        return "Accepted";
    case 204:
        return "No Content";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 304:
        return "Not Modified";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 413:
        return "Content Too Large";
    case 415:
        return "Unsupported Media Type";
    case 429:
        return "Too Many Requests";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 503:
        return "Service Unavailable";
    default:
        return "Unknown";
    }
}

void Response::set_header(std::string name, std::string value) {
    for (auto& field : headers_) {
        if (iequals(field.name, name)) {
            field.value = std::move(value);
            return;
        }
    }
    headers_.push_back(Header{std::move(name), std::move(value)});
}

void Response::set_body(std::string body) {
    body_ = std::move(body);
}

void Response::set_content_type(std::string value) {
    set_header("Content-Type", std::move(value));
}

std::optional<std::string_view> Response::header(std::string_view name) const {
    for (auto const& field : headers_) {
        if (iequals(field.name, name)) {
            return field.value;
        }
    }
    return std::nullopt;
}

std::string Response::serialize(bool include_body) const {
    std::string out;
    out.reserve(128 + (include_body ? body_.size() : 0));

    out += "HTTP/1.1 ";
    out += std::to_string(status_);
    out += ' ';
    out += reason_phrase(status_);
    out += "\r\n";

    bool has_content_length = false;
    for (auto const& field : headers_) {
        if (iequals(field.name, "Content-Length")) {
            has_content_length = true;
        }
        out += field.name;
        out += ": ";
        out += field.value;
        out += "\r\n";
    }

    if (!has_content_length) {
        out += "Content-Length: ";
        out += std::to_string(body_.size());
        out += "\r\n";
    }

    out += "\r\n";
    if (include_body) {
        out += body_;
    }
    return out;
}

Response Response::text(int status, std::string body) {
    Response response{status};
    response.set_content_type("text/plain; charset=utf-8");
    response.set_body(std::move(body));
    return response;
}

Response Response::json(int status, std::string body) {
    Response response{status};
    response.set_content_type("application/json");
    response.set_body(std::move(body));
    return response;
}

Response Response::redirect(int status, std::string location) {
    Response response{status};
    response.set_header("Location", std::move(location));
    return response;
}

Response Response::no_content() {
    return Response{204};
}

} // namespace fw::http
