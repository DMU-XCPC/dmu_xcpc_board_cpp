file(GLOB_RECURSE FW_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(fw_tests ${FW_TEST_SOURCES})

target_link_libraries(fw_tests PRIVATE
    fw_core
    fw_execution
    fw_net
    fw_http
    fw_server
    fw_options
    GTest::gtest
    GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(fw_tests)
