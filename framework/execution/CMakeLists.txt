add_library(fw_execution INTERFACE)

target_include_directories(fw_execution INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(fw_execution INTERFACE stdexec fw_options)
