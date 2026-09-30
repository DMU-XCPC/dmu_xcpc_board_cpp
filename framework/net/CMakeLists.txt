add_library(fw_net INTERFACE)

target_include_directories(fw_net INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat")

target_link_libraries(fw_net INTERFACE
    fw_execution
    stdexec
    asio
    fw_options)
