file(GLOB FW_DASHBOARD_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(dashboard ${FW_DASHBOARD_SOURCES})

target_link_libraries(dashboard PRIVATE
    fw_core
    fw_execution
    fw_net
    fw_http
    fw_server
    fw_options)
