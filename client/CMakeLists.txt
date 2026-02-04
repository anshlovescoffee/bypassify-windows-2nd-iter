project(client)
add_library(client SHARED)
target_sources(
    client
    PRIVATE
    dllmain.cpp
    dwmcore.cpp
    client.cpp
    input.cpp
    offsets.cpp
    imagehelper.cpp
    gpthelper.cpp
    settings.cpp
    settings_ui.cpp
    mathrender.cpp
    auth.cpp
)
target_include_directories(client INTERFACE .)
target_include_directories(client PRIVATE ${CMAKE_SOURCE_DIR}/shared)
target_link_libraries(client PRIVATE minhook imgui shlwapi DirectXTex)
set_target_properties(
    client
    PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)