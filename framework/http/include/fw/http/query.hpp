#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fw::http {

struct QueryEntry {
    std::string key;
    std::string value;
};

// Parsed query string (`?a=1&b=two`). Keys and values are percent-decoded and
// `+` is treated as a space.
class QueryParams {
public:
    [[nodiscard]] static QueryParams parse(std::string_view query);

    [[nodiscard]] std::optional<std::string_view> get(std::string_view key) const noexcept;
    [[nodiscard]] std::vector<QueryEntry> const& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] bool empty() const noexcept {
        return entries_.empty();
    }

private:
    std::vector<QueryEntry> entries_;
};

} // namespace fw::http
