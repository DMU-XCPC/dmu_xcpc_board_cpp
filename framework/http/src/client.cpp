#include "fw/http/client.hpp"

#include "fw/http/gzip.hpp"
#include "fw/http/parser.hpp"
#include "fw/net/io.hpp"
#include "fw/net/tcp.hpp"

#include <asio.hpp>

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fw::http {
namespace {

struct timeout_error : std::exception {};

struct Url {
    std::string host;
    std::uint16_t port = 80;
    std::string target;
};

std::optional<Url> parse_absolute_url(std::string_view url) {
    constexpr std::string_view scheme = "http://";
    if (!url.starts_with(scheme)) {
        return std::nullopt;
    }
    url.remove_prefix(scheme.size());

    auto const slash = url.find('/');
    auto const authority = url.substr(0, slash);
    if (authority.empty()) {
        return std::nullopt;
    }

    std::string host{authority};
    std::uint16_t port = 80;
    if (auto const colon = authority.rfind(':'); colon != std::string_view::npos) {
        host.assign(authority.substr(0, colon));
        auto const port_text = authority.substr(colon + 1);
        unsigned value = 0;
        auto const [ptr, ec] =
            std::from_chars(port_text.data(), port_text.data() + port_text.size(), value);
        if (ec != std::errc{} || ptr != port_text.data() + port_text.size() || value == 0 ||
            value > 65535) {
            return std::nullopt;
        }
        port = static_cast<std::uint16_t>(value);
    }
    if (host.empty()) {
        return std::nullopt;
    }

    std::string target =
        slash == std::string_view::npos ? std::string{"/"} : std::string{url.substr(slash)};
    return Url{std::move(host), port, std::move(target)};
}

std::optional<Url> resolve_url(std::string_view location, Url const& base) {
    if (location.starts_with("//")) {
        return parse_absolute_url("http:" + std::string{location});
    }
    if (auto absolute = parse_absolute_url(location)) {
        return absolute;
    }
    if (location.empty()) {
        return std::nullopt;
    }
    if (location.starts_with('/')) {
        return Url{base.host, base.port, std::string{location}};
    }
    auto const slash = base.target.rfind('/');
    std::string target =
        slash == std::string::npos ? std::string{"/"} : base.target.substr(0, slash + 1);
    target += location;
    return Url{base.host, base.port, std::move(target)};
}

bool is_redirect(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

bool is_idempotent(Method method) {
    switch (method) {
    case Method::get:
    case Method::head:
    case Method::put:
    case Method::delete_:
    case Method::options:
    case Method::trace:
        return true;
    default:
        return false;
    }
}

bool connection_close(std::optional<std::string_view> const& value) {
    return value.has_value() && iequals(*value, "close");
}

bool wants_close(Request const& request) {
    return connection_close(request.header("Connection")) || request.minor_version < 1;
}

void set_header(Request& request, std::string name, std::string value) {
    for (auto& field : request.headers) {
        if (iequals(field.name, name)) {
            field.value = std::move(value);
            return;
        }
    }
    request.headers.push_back(Header{std::move(name), std::move(value)});
}

void remove_header(Request& request, std::string_view name) {
    std::erase_if(request.headers,
                  [name](Header const& field) { return iequals(field.name, name); });
}

std::string host_header(Url const& url) {
    if (url.port == 80) {
        return url.host;
    }
    return url.host + ':' + std::to_string(url.port);
}

Url initial_url(Request const& request, ClientOptions const& options) {
    if (auto const absolute = parse_absolute_url(request.target)) {
        return *absolute;
    }
    Url url{options.host, options.port, {}};
    url.target = request.target.empty() ? std::string{"/"} : request.target;
    return url;
}

void apply_redirect(Request& request, int status) {
    bool const switch_to_get =
        status == 303 || ((status == 301 || status == 302) && request.method == Method::post);
    if (switch_to_get) {
        request.method = Method::get;
        request.body.clear();
        remove_header(request, "Content-Length");
        remove_header(request, "Content-Type");
    }
}

} // namespace

struct Client::Impl {
    fw::net::io_context& context;
    ClientOptions options;

    mutable std::mutex mutex;
    struct Pooled {
        std::shared_ptr<fw::net::tcp_socket> socket;
        std::chrono::steady_clock::time_point idle_since;
    };
    std::unordered_map<std::string, std::vector<Pooled>> pool;

    Impl(fw::net::io_context& ctx, ClientOptions opts) : context(ctx), options(std::move(opts)) {}

    static std::string key(std::string const& host, std::uint16_t port) {
        return host + ':' + std::to_string(port);
    }

    std::shared_ptr<fw::net::tcp_socket> take(std::string const& host, std::uint16_t port) {
        std::scoped_lock lock{mutex};
        auto const it = pool.find(key(host, port));
        if (it == pool.end()) {
            return nullptr;
        }
        auto const now = std::chrono::steady_clock::now();
        while (!it->second.empty()) {
            auto entry = std::move(it->second.back());
            it->second.pop_back();
            if (now - entry.idle_since <= options.idle_timeout) {
                return std::move(entry.socket);
            }
            entry.socket->close();
        }
        return nullptr;
    }

    void put(std::string const& host, std::uint16_t port,
             std::shared_ptr<fw::net::tcp_socket> socket) {
        std::scoped_lock lock{mutex};
        auto const now = std::chrono::steady_clock::now();
        auto& list = pool[key(host, port)];
        std::erase_if(list, [&](Pooled const& entry) {
            if (now - entry.idle_since > options.idle_timeout) {
                entry.socket->close();
                return true;
            }
            return false;
        });
        if (list.size() >= options.max_idle_connections) {
            socket->close();
            return;
        }
        list.push_back(Pooled{std::move(socket), now});
    }

    void close_all() {
        std::scoped_lock lock{mutex};
        for (auto& [authority, list] : pool) {
            for (auto& entry : list) {
                entry.socket->close();
            }
        }
        pool.clear();
    }

    [[nodiscard]] std::size_t idle_connections() const {
        std::scoped_lock lock{mutex};
        std::size_t total = 0;
        for (auto const& [authority, list] : pool) {
            total += list.size();
        }
        return total;
    }

    fw::task<fw::Result<std::shared_ptr<fw::net::tcp_socket>>> connect(std::string host,
                                                                       std::uint16_t port) {
        auto connector = std::make_shared<fw::net::tcp_connector>(context, std::move(host), port);
        try {
            auto socket = co_await connector->async_connect();
            co_return std::make_shared<fw::net::tcp_socket>(std::move(socket));
        } catch (std::exception const&) {
            co_return std::unexpected{fw::make_error_code(fw::Errc::unavailable)};
        }
    }

    static fw::task<fw::Result<std::size_t>> read_some(std::shared_ptr<fw::net::tcp_socket> socket,
                                                       std::span<char> buffer,
                                                       std::exception_ptr timeout) {
        try {
            std::size_t const received =
                co_await (socket->read_some(buffer) | fw::stopped_as_error(timeout));
            co_return received;
        } catch (timeout_error const&) {
            co_return std::unexpected{fw::make_error_code(fw::Errc::timed_out)};
        } catch (std::system_error const& error) {
            if (error.code() == asio::error::eof) {
                co_return std::size_t{0};
            }
            co_return std::unexpected{fw::make_error_code(fw::Errc::io_error)};
        } catch (std::exception const&) {
            co_return std::unexpected{fw::make_error_code(fw::Errc::io_error)};
        }
    }

    fw::task<fw::Result<std::string>>
    collect_until_close(std::shared_ptr<fw::net::tcp_socket> socket, std::string body,
                        std::exception_ptr timeout) const {
        std::array<char, 16384> chunk{};
        while (true) {
            auto const read = co_await read_some(socket, chunk, timeout);
            if (!read) {
                co_return std::unexpected{read.error()};
            }
            if (*read == 0) {
                break;
            }
            body.append(chunk.data(), *read);
            if (body.size() > options.max_response_bytes) {
                co_return std::unexpected{fw::make_error_code(fw::Errc::protocol_error)};
            }
        }
        co_return body;
    }

    fw::task<fw::Result<Response>> read_message(std::shared_ptr<fw::net::tcp_socket> socket,
                                                Request const& request, std::exception_ptr timeout,
                                                bool& reusable) const {
        std::string buffer;
        std::array<char, 16384> chunk{};
        while (true) {
            Response response;
            auto const parsed = parse_response(buffer, response, request.method == Method::head);
            if (parsed.state == ParseState::error) {
                co_return std::unexpected{fw::make_error_code(fw::Errc::protocol_error)};
            }
            if (parsed.state == ParseState::complete) {
                reusable =
                    !connection_close(response.header("Connection")) && !wants_close(request);
                co_return response;
            }
            if (parsed.framing == BodyFraming::until_close) {
                auto body = co_await collect_until_close(
                    socket, std::string{buffer.substr(parsed.consumed)}, timeout);
                if (!body) {
                    co_return std::unexpected{body.error()};
                }
                response.set_body(std::move(*body));
                reusable = false;
                co_return response;
            }
            auto const read = co_await read_some(socket, chunk, timeout);
            if (!read) {
                co_return std::unexpected{read.error()};
            }
            if (buffer.size() + *read > options.max_response_bytes) {
                co_return std::unexpected{fw::make_error_code(fw::Errc::protocol_error)};
            }
            buffer.append(chunk.data(), *read);
        }
    }

    fw::task<fw::Result<Response>> exchange(std::shared_ptr<fw::net::tcp_socket> socket,
                                            Request const& request, bool& reusable) const {
        auto fired = std::make_shared<std::atomic<bool>>(false);
        auto timer = std::make_shared<asio::steady_timer>(context.get_executor());
        auto timeout = std::make_exception_ptr(timeout_error{});
        timer->expires_after(options.timeout);
        std::weak_ptr<fw::net::tcp_socket> weak = socket;
        timer->async_wait([weak, fired](std::error_code error) {
            if (!error) {
                fired->store(true);
                if (auto locked = weak.lock()) {
                    std::error_code ignored;
                    [[maybe_unused]] auto const cancelled = locked->native().cancel(ignored);
                }
            }
        });

        try {
            auto const out = request.serialize();
            co_await (socket->write(out) | fw::stopped_as_error(timeout));
            auto result = co_await read_message(socket, request, timeout, reusable);
            timer->cancel();
            if (result) {
                reusable = reusable && !fired->load();
            }
            co_return result;
        } catch (timeout_error const&) {
            timer->cancel();
            co_return std::unexpected{fw::make_error_code(fw::Errc::timed_out)};
        } catch (std::exception const&) {
            timer->cancel();
            co_return std::unexpected{fw::make_error_code(fw::Errc::io_error)};
        }
    }

    fw::task<fw::Result<Response>> attempt(Request const& request, std::string host,
                                           std::uint16_t port) {
        auto socket = take(host, port);
        bool const reused = socket != nullptr;
        if (socket == nullptr) {
            auto connected = co_await connect(host, port);
            if (!connected) {
                co_return std::unexpected{connected.error()};
            }
            socket = std::move(*connected);
        }

        bool reusable = false;
        auto result = co_await exchange(socket, request, reusable);
        if (result) {
            if (reusable) {
                put(host, port, std::move(socket));
            } else {
                socket->close();
            }
            co_return result;
        }

        socket->close();
        if (!reused || !is_idempotent(request.method) ||
            result.error() != fw::make_error_code(fw::Errc::io_error)) {
            co_return std::unexpected{result.error()};
        }

        auto reconnected = co_await connect(host, port);
        if (!reconnected) {
            co_return std::unexpected{reconnected.error()};
        }
        bool retry_reusable = false;
        result = co_await exchange(*reconnected, request, retry_reusable);
        if (!result) {
            (*reconnected)->close();
            co_return std::unexpected{result.error()};
        }
        if (retry_reusable) {
            put(host, port, std::move(*reconnected));
        } else {
            (*reconnected)->close();
        }
        co_return result;
    }

    fw::Status decompress(Response& response) const {
        if (!options.decompress) {
            return {};
        }
        auto const encoding = response.header("Content-Encoding");
        if (!encoding.has_value() || !iequals(*encoding, "gzip")) {
            return {};
        }
        auto decoded = gzip_decompress(response.body());
        if (!decoded) {
            return std::unexpected{decoded.error()};
        }
        response.set_body(std::move(*decoded));
        response.remove_header("Content-Encoding");
        return {};
    }

    fw::task<fw::Result<Response>> run(Request request) {
        Url current = initial_url(request, options);
        set_header(request, "Host", host_header(current));
        if (!options.user_agent.empty()) {
            set_header(request, "User-Agent", options.user_agent);
        }

        std::size_t redirects = 0;
        while (true) {
            request.target = current.target;
            auto result = co_await attempt(request, current.host, current.port);
            if (!result) {
                co_return std::unexpected{result.error()};
            }
            Response response = std::move(*result);

            if (auto status = decompress(response); !status) {
                co_return std::unexpected{status.error()};
            }

            auto const location = response.header("Location");
            bool const redirect =
                options.follow_redirects && is_redirect(response.status()) && location.has_value();
            if (!redirect) {
                co_return response;
            }
            if (redirects >= options.max_redirects) {
                co_return std::unexpected{fw::make_error_code(fw::Errc::protocol_error)};
            }
            auto next = resolve_url(*location, current);
            if (!next) {
                co_return response;
            }
            ++redirects;
            apply_redirect(request, response.status());
            current = std::move(*next);
            set_header(request, "Host", host_header(current));
        }
    }
};

Client::Client(fw::net::io_context& context, ClientOptions options)
    : impl_(std::make_unique<Impl>(context, std::move(options))) {}

Client::~Client() = default;
Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;

fw::task<fw::Result<Response>> Client::send(Request request) {
    co_return co_await impl_->run(std::move(request));
}

fw::task<fw::Result<Response>> Client::get(std::string target) {
    Request request;
    request.method = Method::get;
    request.target = std::move(target);
    co_return co_await impl_->run(std::move(request));
}

fw::task<fw::Result<Response>> Client::post(std::string target, std::string body,
                                            std::string content_type) {
    Request request;
    request.method = Method::post;
    request.target = std::move(target);
    request.body = std::move(body);
    if (!content_type.empty()) {
        request.headers.push_back(Header{"Content-Type", std::move(content_type)});
    }
    co_return co_await impl_->run(std::move(request));
}

fw::task<fw::Result<Response>> Client::put(std::string target, std::string body,
                                           std::string content_type) {
    Request request;
    request.method = Method::put;
    request.target = std::move(target);
    request.body = std::move(body);
    if (!content_type.empty()) {
        request.headers.push_back(Header{"Content-Type", std::move(content_type)});
    }
    co_return co_await impl_->run(std::move(request));
}

fw::task<fw::Result<Response>> Client::del(std::string target) {
    Request request;
    request.method = Method::delete_;
    request.target = std::move(target);
    co_return co_await impl_->run(std::move(request));
}

void Client::close() {
    impl_->close_all();
}

std::size_t Client::idle_connections() const {
    return impl_->idle_connections();
}

} // namespace fw::http
