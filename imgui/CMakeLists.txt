project(imgui)
add_library(imgui)
target_sources(
    imgui
    PRIVATE
    imgui_draw.cpp
    imgui_impl_dx11.cpp
    imgui_impl_win32.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    imgui.cpp
)
target_include_directories(imgui INTERFACE .)
