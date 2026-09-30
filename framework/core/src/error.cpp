#include "fw/core/error.hpp"

#include <string_view>

namespace fw {
namespace {

class ErrcCategory final : public std::error_category {
public:
    [[nodiscard]] char const* name() const noexcept override {
        return "fw";
    }

    [[nodiscard]] std::string message(int condition) const override {
        switch (static_cast<Errc>(condition)) {
        case Errc::success:
            return "success";
        case Errc::invalid_argument:
            return "invalid argument";
        case Errc::not_found:
            return "not found";
        case Errc::already_exists:
            return "already exists";
        case Errc::not_supported:
            return "not supported";
        case Errc::unavailable:
            return "unavailable";
        case Errc::io_error:
            return "io error";
        case Errc::internal:
            return "internal error";
        }
        return "unknown error";
    }
};

} // namespace

std::error_category const& error_category() noexcept {
    static ErrcCategory const instance;
    return instance;
}

std::error_code make_error_code(Errc code) noexcept {
    return {static_cast<int>(code), error_category()};
}

std::string to_string(Error const& error) {
    if (!error) {
        return "success";
    }
    return std::string{error.category().name()} + ": " + error.message();
}

} // namespace fw
