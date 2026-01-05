cmake_minimum_required(VERSION 3.14)
project(runclient)
project(rundumper)
add_executable(runclient)
add_executable(rundumper)
target_compile_features(rundumper PRIVATE cxx_std_17)
target_sources(runclient PRIVATE runclient.cpp)
target_sources(rundumper PRIVATE rundumper.cpp)
target_include_directories(runclient PRIVATE .)
target_include_directories(rundumper PRIVATE . ../shared)
target_link_libraries(runclient PRIVATE injector pathcch)
target_link_libraries(rundumper PRIVATE injector pathcch)
set_target_properties(
    runclient
    PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
set_target_properties(
    rundumper
    PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)