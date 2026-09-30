#include "fw/core/config.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <tuple>

namespace {

struct test_settings {
    std::string name;
    std::uint16_t port;
    bool debug;
};

} // namespace

template <> struct fw::config_schema<test_settings> {
    static constexpr auto fields() {
        return std::tuple{
            fw::field{"app.name", &test_settings::name, std::string{"default"}},
            fw::field{"app.port", &test_settings::port, std::uint16_t{8080}},
            fw::field{"app.debug", &test_settings::debug, false},
        };
    }
};

TEST(Config, BindUsesDefaultsWhenMissing) {
    auto const config = fw::Config::from_string("app.name = \"x\"\n");
    ASSERT_TRUE(config);
    auto const settings = config->bind<test_settings>();
    ASSERT_TRUE(settings);
    EXPECT_EQ(settings->name, "x");
    EXPECT_EQ(settings->port, 8080);
    EXPECT_FALSE(settings->debug);
}

TEST(Config, BindReadsTypedValues) {
    auto const config =
        fw::Config::from_string("app.name = \"dash\"\napp.port = 9000\napp.debug = true\n");
    ASSERT_TRUE(config);
    auto const settings = config->bind<test_settings>();
    ASSERT_TRUE(settings);
    EXPECT_EQ(settings->name, "dash");
    EXPECT_EQ(settings->port, 9000);
    EXPECT_TRUE(settings->debug);
}

TEST(Config, BindRejectsWrongType) {
    auto const config = fw::Config::from_string("app.port = \"not-a-number\"\n");
    ASSERT_TRUE(config);
    auto const settings = config->bind<test_settings>();
    ASSERT_FALSE(settings);
    EXPECT_EQ(settings.error(), fw::Errc::invalid_argument);
}

TEST(Config, BindRejectsOutOfRange) {
    auto const config = fw::Config::from_string("app.port = 70000\n");
    ASSERT_TRUE(config);
    auto const settings = config->bind<test_settings>();
    EXPECT_FALSE(settings);
}

TEST(Config, ReadsTypedValues) {
    auto const config =
        fw::Config::from_string("a = 1\nb = 2.5\nc = true\ns = \"hello\"\n[server]\nport = 9000\n");
    ASSERT_TRUE(config);
    EXPECT_EQ(config->get_int("a"), std::int64_t{1});
    ASSERT_TRUE(config->get_double("b").has_value());
    EXPECT_DOUBLE_EQ(*config->get_double("b"), 2.5);
    EXPECT_EQ(config->get_bool("c"), true);
    EXPECT_EQ(config->get_string("s"), std::string{"hello"});
    EXPECT_EQ(config->get_int("server.port"), std::int64_t{9000});
}

TEST(Config, MissingKeyReturnsNullopt) {
    auto const config = fw::Config::from_string("a = 1\n");
    ASSERT_TRUE(config);
    EXPECT_FALSE(config->has("missing"));
    EXPECT_FALSE(config->get_int("missing").has_value());
    EXPECT_EQ(config->get_int_or("missing", 7), 7);
}

TEST(Config, RejectsInvalidToml) {
    auto const config = fw::Config::from_string("this is not = = toml");
    EXPECT_FALSE(config);
}

TEST(Config, MissingFileReportsError) {
    auto const config = fw::Config::from_file("/nonexistent/path/config.toml");
    ASSERT_FALSE(config);
    EXPECT_EQ(config.error(), fw::Errc::io_error);
}

TEST(Config, EnvOverridesExistingKeys) {
    ::setenv("FW_SERVER_PORT", "1234", 1);
    ::setenv("FW_SERVER_DEBUG", "true", 1);

    auto config = fw::Config::from_string("[server]\nport = 9000\ndebug = false\n");
    ASSERT_TRUE(config);
    config->apply_env_overrides("FW_");

    EXPECT_EQ(config->get_int("server.port"), std::int64_t{1234});
    EXPECT_EQ(config->get_bool("server.debug"), true);

    ::unsetenv("FW_SERVER_PORT");
    ::unsetenv("FW_SERVER_DEBUG");
}
