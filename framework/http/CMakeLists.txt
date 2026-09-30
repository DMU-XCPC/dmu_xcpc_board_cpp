add_library(picohttpparser STATIC
    "${CMAKE_SOURCE_DIR}/third_party/picohttpparser/picohttpparser.c")
target_include_directories(picohttpparser SYSTEM PUBLIC
    "${CMAKE_SOURCE_DIR}/third_party/picohttpparser")

file(GLOB FW_HTTP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(fw_http STATIC ${FW_HTTP_SOURCES})

target_include_directories(fw_http PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(fw_http PUBLIC fw_options)
target_link_libraries(fw_http PRIVATE picohttpparser)
