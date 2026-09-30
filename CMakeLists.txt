cmake_minimum_required(VERSION 3.25)

project(dmu_xcpc_dashboard VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FW_BUILD_TESTS "Build the unit test suite" ON)
option(FW_METRICS "(reserved) build metrics support" OFF)
option(FW_REFLECTION "(reserved) enable C++26 reflection" OFF)

find_package(spdlog REQUIRED)
find_package(Threads REQUIRED)

add_library(fw_options INTERFACE)
target_compile_options(fw_options INTERFACE -Wall -Wextra)

add_library(stdexec INTERFACE)
target_include_directories(stdexec SYSTEM INTERFACE
    "${CMAKE_SOURCE_DIR}/third_party/stdexec/include")

add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus SYSTEM INTERFACE
    "${CMAKE_SOURCE_DIR}/third_party/tomlplusplus/include")

add_library(asio INTERFACE)
target_include_directories(asio SYSTEM INTERFACE
    "${CMAKE_SOURCE_DIR}/third_party/asio/include")
target_compile_definitions(asio INTERFACE ASIO_STANDALONE=1)
target_link_libraries(asio INTERFACE Threads::Threads)

add_library(unordered_dense INTERFACE)
target_include_directories(unordered_dense SYSTEM INTERFACE
    "${CMAKE_SOURCE_DIR}/third_party/unordered_dense/include")

add_subdirectory(framework/core)
add_subdirectory(framework/execution)
add_subdirectory(framework/net)
add_subdirectory(framework/http)
add_subdirectory(framework/server)
add_subdirectory(apps/dashboard)
add_subdirectory(examples)

if(FW_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()
