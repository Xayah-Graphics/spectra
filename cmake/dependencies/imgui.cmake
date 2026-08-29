include_guard(GLOBAL)

spectra_require_dependency(glfw)
FetchContent_MakeAvailable(imgui)

add_library(spectra_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
)
add_library(spectra::imgui ALIAS spectra_imgui)
target_include_directories(
        spectra_imgui
        PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends"
)
target_compile_definitions(spectra_imgui PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(spectra_imgui PUBLIC glfw)
