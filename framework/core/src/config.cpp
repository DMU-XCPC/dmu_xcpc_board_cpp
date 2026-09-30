#include "fw/core/config.hpp"

#include <toml++/toml.hpp>

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fw {
namespace {

std::optional<std::int64_t> parse_integer(std::string_view text) {
    std::int64_t value = 0;
    auto const* const begin = text.data();
    auto const* const end = text.data() + text.size();
    auto const [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> parse_double(std::string_view text) {
    double value = 0.0;
    auto const* const begin = text.data();
    auto const* const end = text.data() + text.size();
    auto const [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::string read_file(std::filesystem::path const& path, Error& error) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        error = make_error_code(Errc::io_error);
        return {};
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (stream.bad()) {
        error = make_error_code(Errc::io_error);
        return {};
    }
    return buffer.str();
}

std::string env_name(std::string_view prefix, std::string_view path) {
    std::string result{prefix};
    result.reserve(prefix.size() + path.size());
    for (char const ch : path) {
        if (ch == '.' || ch == '-') {
            result.push_back('_');
        } else if (ch >= 'a' && ch <= 'z') {
            result.push_back(static_cast<char>(ch - 'a' + 'A'));
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

toml::table& descend(toml::table& root, std::string_view path, std::string_view leaf) {
    toml::table* current = &root;
    std::size_t start = 0;
    while (true) {
        std::size_t const dot = path.find('.', start);
        std::string_view const component =
            dot == std::string_view::npos ? path.substr(start) : path.substr(start, dot - start);
        if (component.empty()) {
            break;
        }
        auto position = current->find(component);
        if (position == current->end() || !position->second.is_table()) {
            current->insert_or_assign(component, toml::table{});
            position = current->find(component);
        }
        current = position->second.as_table();
        if (dot == std::string_view::npos) {
            break;
        }
        start = dot + 1;
    }
    (void)leaf;
    return *current;
}

void set_value(toml::table& root, std::string_view key, auto value) {
    std::size_t const dot = key.rfind('.');
    if (dot == std::string_view::npos) {
        root.insert_or_assign(std::string{key}, std::move(value));
        return;
    }
    auto& parent = descend(root, key.substr(0, dot), key.substr(dot + 1));
    parent.insert_or_assign(std::string{key.substr(dot + 1)}, std::move(value));
}

void collect_leaves(toml::table const& table, std::string& path, std::vector<std::string>& out) {
    for (auto const& [key, node] : table) {
        std::string const previous = path;
        if (!path.empty()) {
            path.push_back('.');
        }
        path += std::string{key.str()};
        if (node.is_table()) {
            collect_leaves(*node.as_table(), path, out);
        } else if (node.is_string() || node.is_integer() || node.is_floating_point() ||
                   node.is_boolean()) {
            out.push_back(path);
        }
        path = previous;
    }
}

} // namespace

struct Config::Impl {
    toml::table root;
};

Config::Config() : impl_(std::make_shared<Impl>()) {}

Config::~Config() = default;
Config::Config(Config const&) = default;
Config& Config::operator=(Config const&) = default;
Config::Config(Config&&) noexcept = default;
Config& Config::operator=(Config&&) noexcept = default;

Result<Config> Config::from_file(std::filesystem::path const& path) {
    Error error;
    std::string const text = read_file(path, error);
    if (error) {
        return std::unexpected{error};
    }
    auto parsed = from_string(text);
    if (!parsed) {
        return std::unexpected{parsed.error()};
    }
    return parsed;
}

Result<Config> Config::from_string(std::string_view text) {
    try {
        Config config;
        config.impl_->root = toml::parse(text);
        return config;
    } catch (toml::parse_error const&) {
        return std::unexpected{make_error_code(Errc::invalid_argument)};
    }
}

bool Config::has(std::string_view key) const {
    return static_cast<bool>(impl_->root.at_path(key));
}

std::optional<std::string> Config::get_string(std::string_view key) const {
    return impl_->root.at_path(key).value<std::string>();
}

std::optional<std::int64_t> Config::get_int(std::string_view key) const {
    return impl_->root.at_path(key).value<std::int64_t>();
}

std::optional<double> Config::get_double(std::string_view key) const {
    return impl_->root.at_path(key).value<double>();
}

std::optional<bool> Config::get_bool(std::string_view key) const {
    return impl_->root.at_path(key).value<bool>();
}

std::string Config::get_string_or(std::string_view key, std::string fallback) const {
    return get_string(key).value_or(std::move(fallback));
}

std::int64_t Config::get_int_or(std::string_view key, std::int64_t fallback) const {
    return get_int(key).value_or(fallback);
}

bool Config::get_bool_or(std::string_view key, bool fallback) const {
    return get_bool(key).value_or(fallback);
}

void Config::set_string(std::string_view key, std::string value) {
    set_value(impl_->root, key, std::move(value));
}

void Config::set_int(std::string_view key, std::int64_t value) {
    set_value(impl_->root, key, value);
}

void Config::set_bool(std::string_view key, bool value) {
    set_value(impl_->root, key, value);
}

void Config::apply_env_overrides(std::string_view prefix) {
    std::vector<std::string> leaves;
    std::string path;
    collect_leaves(impl_->root, path, leaves);

    for (auto const& leaf : leaves) {
        std::string const name = env_name(prefix, leaf);
        char const* const raw = std::getenv(name.c_str());
        if (raw == nullptr) {
            continue;
        }
        std::string const value{raw};
        auto node = impl_->root.at_path(leaf);
        if (node.is_integer()) {
            if (auto parsed = parse_integer(value)) {
                set_value(impl_->root, leaf, *parsed);
            }
        } else if (node.is_floating_point()) {
            if (auto parsed = parse_double(value)) {
                set_value(impl_->root, leaf, *parsed);
            }
        } else if (node.is_boolean()) {
            bool const parsed = value == "true" || value == "1" || value == "yes" || value == "on";
            set_value(impl_->root, leaf, parsed);
        } else {
            set_value(impl_->root, leaf, value);
        }
    }
}

} // namespace fw
