#include "route_table.hpp"

#include <cctype>
#include <utility>

namespace fw::server::detail {
namespace {

RouteNode* literal_child(RouteNode& node, std::string const& text) {
    auto it = node.literals.find(std::string_view{text});
    if (it != node.literals.end()) {
        return it->second.get();
    }
    auto [inserted, _] = node.literals.try_emplace(text, std::make_unique<RouteNode>());
    return inserted->second.get();
}

std::string normalize_literal(std::string_view text, bool case_insensitive) {
    std::string out{text};
    if (case_insensitive) {
        for (char& ch : out) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
    }
    return out;
}

std::string pattern_text(std::span<RouteSegment const> segments) {
    std::string out;
    for (auto const& segment : segments) {
        out.push_back('/');
        switch (segment.kind()) {
        case RouteSegment::Kind::literal:
            out += segment.text();
            break;
        case RouteSegment::Kind::parameter:
            out.push_back(':');
            out += segment.text();
            break;
        case RouteSegment::Kind::catch_all:
            out.push_back('*');
            out += segment.text();
            break;
        }
    }
    return out.empty() ? std::string{"/"} : out;
}

RouteNode* catch_all_child(RouteNode& node, RouteSegment const& segment) {
    if (node.catch_all == nullptr) {
        node.catch_all = std::make_unique<RouteNode>();
    }
    node.catch_all_name = segment.text();
    return node.catch_all.get();
}

fw::Result<RouteNode*> param_child(RouteNode& node, RouteSegment const& segment) {
    auto const constraint_name =
        segment.constraint() != nullptr ? std::string{segment.constraint()->name()} : std::string{};
    for (auto& existing : node.params) {
        auto const existing_constraint_name = existing.constraint != nullptr
                                                  ? std::string{existing.constraint->name()}
                                                  : std::string{};
        if (existing.name == segment.text() && existing_constraint_name == constraint_name &&
            existing.optional == segment.optional()) {
            return existing.child.get();
        }
    }
    if (segment.constraint() == nullptr) {
        for (auto const& existing : node.params) {
            if (existing.constraint == nullptr) {
                return std::unexpected{fw::make_error_code(fw::Errc::already_exists)};
            }
        }
    }
    node.params.push_back(ParamEdge{
        std::make_unique<RouteNode>(),
        segment.text(),
        segment.constraint(),
        segment.optional(),
    });
    return node.params.back().child.get();
}

} // namespace

fw::Result<void> compile_one(CompiledTable& table, PendingRoute const& route,
                             std::vector<Middleware> const& middleware, bool case_insensitive) {
    auto const segments = route.pattern.segments();
    for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
        if (segments[i].is_catch_all()) {
            return std::unexpected{fw::make_error_code(fw::Errc::invalid_argument)};
        }
    }

    RouteNode* node = &table.root;
    std::vector<std::string> names;
    for (auto const& segment : segments) {
        if (segment.is_catch_all()) {
            names.push_back(segment.text());
            node = catch_all_child(*node, segment);
        } else if (segment.is_parameter()) {
            auto const child = param_child(*node, segment);
            if (!child) {
                return std::unexpected{child.error()};
            }
            names.push_back(segment.text());
            node = *child;
        } else {
            node = literal_child(*node, normalize_literal(segment.text(), case_insensitive));
        }
    }

    auto const method_index = static_cast<std::size_t>(route.method);
    if (node->handlers[method_index] >= 0) {
        return std::unexpected{fw::make_error_code(fw::Errc::already_exists)};
    }
    node->handlers[method_index] = static_cast<std::int32_t>(table.routes.size());

    Handler handler = route.handler;
    for (std::size_t i = middleware.size(); i-- > 0;) {
        handler = middleware[i](std::move(handler));
    }
    table.routes.push_back(CompiledRoute{
        route.name,
        pattern_text(segments),
        route.method,
        std::move(names),
        std::move(handler),
    });
    return {};
}

} // namespace fw::server::detail
