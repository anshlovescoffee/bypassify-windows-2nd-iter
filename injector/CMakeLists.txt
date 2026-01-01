cmake_minimum_required(VERSION 3.14)
project(injector)
add_executable(injector injector.cpp injector.hpp injectorlib.cpp)
target_link_libraries(injector PRIVATE ntdll pathcch)
set_target_properties(
    injector
    PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)