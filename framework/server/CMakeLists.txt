file(GLOB FW_SERVER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(fw_server STATIC ${FW_SERVER_SOURCES})

target_include_directories(fw_server PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(fw_server PUBLIC fw_core fw_http fw_net unordered_dense fw_options pcre2-8)
