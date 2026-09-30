-- Dependency manifest: this file is the single source of truth for the build.
-- Keep CMakeLists.txt in sync when dependencies change.

set_project("dmu_xcpc_dashboard")
set_version("0.1.0")
set_license("Apache-2.0")
set_languages("c++23")
set_allowedplats("linux")
set_allowedarchs("x86_64", "arm64", "aarch64")
set_warnings("all", "extra")
add_rules("mode.debug", "mode.release")

option("tests")
    set_default(true)
    set_description("Build the unit test suite")
option_end()

option("metrics")
    set_default(false)
    set_description("(reserved) build metrics support")
option_end()

option("reflection")
    set_default(false)
    set_description("(reserved) enable C++26 reflection")
option_end()

add_requires("spdlog", {system = true})
if has_config("tests") then
    add_requires("gtest", {system = true})
end

-- Vendored, header-only third-party targets -------------------------------

target("stdexec")
    set_kind("headeronly")
    add_sysincludedirs("third_party/stdexec/include", {public = true})

target("tomlplusplus")
    set_kind("headeronly")
    add_sysincludedirs("third_party/tomlplusplus/include", {public = true})

target("asio")
    set_kind("headeronly")
    add_sysincludedirs("third_party/asio/include", {public = true})
    add_defines("ASIO_STANDALONE=1", {public = true})
    add_links("pthread")

target("unordered_dense")
    set_kind("headeronly")
    add_sysincludedirs("third_party/unordered_dense/include", {public = true})

-- Framework ---------------------------------------------------------------

target("fw_core")
    set_kind("static")
    add_files("framework/core/src/*.cpp")
    add_includedirs("framework/core/include", {public = true})
    add_packages("spdlog")
    add_deps("tomlplusplus")

target("fw_execution")
    set_kind("headeronly")
    add_includedirs("framework/execution/include", {public = true})
    add_deps("stdexec")

target("fw_net")
    set_kind("headeronly")
    add_includedirs("framework/net/include", {public = true})
    add_includedirs("framework/net/compat", {public = true})
    add_deps("fw_execution", "stdexec", "asio")

target("picohttpparser")
    set_kind("static")
    add_files("third_party/picohttpparser/picohttpparser.c")
    add_sysincludedirs("third_party/picohttpparser", {public = true})

target("fw_http")
    set_kind("static")
    add_files("framework/http/src/*.cpp")
    add_includedirs("framework/http/include", {public = true})
    add_deps("picohttpparser")

target("fw_server")
    set_kind("static")
    add_files("framework/server/src/*.cpp")
    add_includedirs("framework/server/include", {public = true})
    add_deps("fw_core", "fw_http", "fw_net", "unordered_dense")
    add_links("pcre2-8")

-- Applications ------------------------------------------------------------

target("dashboard")
    set_kind("binary")
    add_files("apps/dashboard/src/*.cpp")
    add_deps("fw_core", "fw_execution", "fw_net", "fw_http", "fw_server")

-- Tests -------------------------------------------------------------------

if has_config("tests") then
    target("fw_tests")
        set_kind("binary")
        add_files("tests/**/*.cpp")
        add_deps("fw_core", "fw_execution", "fw_net", "fw_http", "fw_server")
        add_packages("gtest")
        add_links("gtest_main")
        add_tests("default")
end
