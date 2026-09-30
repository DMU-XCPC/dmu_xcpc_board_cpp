#include "fw/http/query.hpp"

namespace fw::http {
namespace {

int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string decode(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            out.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            int const hi = hex_digit(value[i + 1]);
            int const lo = hex_digit(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                auto const code = (static_cast<unsigned>(hi) << 4U) | static_cast<unsigned>(lo);
                out.push_back(static_cast<char>(code));
                i += 2;
            } else {
                out.push_back(value[i]);
            }
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

} // namespace

QueryParams QueryParams::parse(std::string_view query) {
    QueryParams params;
    std::size_t index = 0;
    while (index <= query.size()) {
        auto const amp = query.find('&', index);
        auto const pair = query.substr(index, amp == std::string_view::npos ? amp : amp - index);
        if (!pair.empty()) {
            auto const equals = pair.find('=');
            if (equals == std::string_view::npos) {
                params.entries_.push_back(QueryEntry{decode(pair), {}});
            } else {
                params.entries_.push_back(
                    QueryEntry{decode(pair.substr(0, equals)), decode(pair.substr(equals + 1))});
            }
        }
        if (amp == std::string_view::npos) {
            break;
        }
        index = amp + 1;
    }
    return params;
}

std::optional<std::string_view> QueryParams::get(std::string_view key) const noexcept {
    for (auto const& entry : entries_) {
        if (entry.key == key) {
            return entry.value;
        }
    }
    return std::nullopt;
}

} // namespace fw::http
