cmake_minimum_required(VERSION 3.14)
project(injector)
add_library(injector)
target_sources(injector PRIVATE injector.cpp injectorlib.cpp)
target_include_directories(injector INTERFACE .)
target_link_libraries(injector PRIVATE ntdll)
set_target_properties(
    injector
    PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)