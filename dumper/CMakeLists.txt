cmake_minimum_required(VERSION 3.14)
project(dumper)
add_executable(dumper)
target_sources(dumper PRIVATE dumper.cpp)
set_target_properties(
    dumper
    PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
target_include_directories(dumper PRIVATE ../shared)
target_link_libraries(dumper PRIVATE dbghelp pathcch)
add_custom_command(
    TARGET dumper POST_BUILD COMMAND
    ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/winsdk/dbghelp.dll $<TARGET_FILE_DIR:dumper>
)
add_custom_command(
    TARGET dumper POST_BUILD COMMAND
    ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/winsdk/symsrv.dll $<TARGET_FILE_DIR:dumper>
)