#include "fw/core/config.hpp"
#include "fw/core/log.hpp"
#include "fw/net/io.hpp"
#include "fw/server/router.hpp"
#include "fw/server/server.hpp"

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <exception>
#include <string>
#include <thread>
#include <tuple>

namespace app {

struct server_settings {
    std::string host;
    std::uint16_t port;
};

} // namespace app

template <> struct fw::config_schema<app::server_settings> {
    static constexpr auto fields() {
        return std::tuple{
            fw::field{"server.host", &app::server_settings::host, std::string{"0.0.0.0"}},
            fw::field{"server.port", &app::server_settings::port, std::uint16_t{8080}},
        };
    }
};

namespace {

fw::Config load_config(int argc, char** argv) {
    if (argc > 1) {
        auto loaded = fw::Config::from_file(argv[1]);
        if (!loaded) {
            fw::log::error("failed to load config '" + std::string{argv[1]} +
                           "': " + fw::to_string(loaded.error()));
            return fw::Config{};
        }
        return *loaded;
    }
    auto fallback = fw::Config::from_string("server.host = \"0.0.0.0\"\nserver.port = 8080\n");
    return fallback ? *fallback : fw::Config{};
}

void register_routes(fw::server::Router& router) {
    router.get("/health",
               fw::server::sync_handler([](fw::http::Request const&, fw::server::RouteContext&) {
                   return fw::http::Response::json(200, R"({"status":"ok"})");
               }));
    router.get("/api/contests/:id", fw::server::sync_handler([](fw::http::Request const&,
                                                                fw::server::RouteContext& context) {
                   auto const id = context.params().get("id").value_or("0");
                   return fw::http::Response::json(200, std::string{R"({"contest":")"} +
                                                            std::string{id} + R"("})");
               }));
}

} // namespace

int main(int argc, char** argv) {
    try {
        fw::log::init("info");
        fw::log::info("dmu_xcpc_dashboard starting");

        fw::Config config = load_config(argc, argv);
        config.apply_env_overrides("FW_");

        auto settings = config.bind<app::server_settings>();
        if (!settings) {
            fw::log::error("invalid server configuration");
            return 1;
        }

        auto const threads = std::max<std::uint32_t>(2U, std::thread::hardware_concurrency());
        fw::net::io_context io{threads};

        fw::server::Server server{io, fw::server::ServerOptions{settings->host, settings->port}};
        register_routes(server.router());

        server.start();
        fw::log::info("listening on " + settings->host + ":" + std::to_string(server.port()));

        fw::net::on_signal(io, {SIGINT, SIGTERM}, [&server] { server.stop(); });
        server.run();
        fw::log::info("shutting down");
        server.wait();
        return 0;
    } catch (std::exception const& error) {
        fw::log::error(std::string{"fatal: "} + error.what());
        return 1;
    }
}
