file(GLOB FW_EXAMPLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

foreach(example_source IN LISTS FW_EXAMPLE_SOURCES)
    get_filename_component(example_name "${example_source}" NAME_WE)
    add_executable("${example_name}" "${example_source}")
    target_link_libraries("${example_name}" PRIVATE fw_server fw_options)
endforeach()
