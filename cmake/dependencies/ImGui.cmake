spectra_prepare_commit_dependency(
        SPECTRA_IMGUI_SOURCE_DIR
        ocornut
        imgui
        "b334d19b667958ed970000073644d911fae17e57"
        "504BC8171B80B8C92F035EBC899F6B3086C9CFA56EFADEE4962753DEB38626A2"
)
add_library(spectra_imgui STATIC
        "${SPECTRA_IMGUI_SOURCE_DIR}/imgui.cpp"
        "${SPECTRA_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
        "${SPECTRA_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
        "${SPECTRA_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
        "${SPECTRA_IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
)
add_library(spectra::imgui ALIAS spectra_imgui)
target_include_directories(
        spectra_imgui
        PUBLIC
        "${SPECTRA_IMGUI_SOURCE_DIR}"
        "${SPECTRA_IMGUI_SOURCE_DIR}/backends"
)
target_compile_definitions(spectra_imgui PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(spectra_imgui PUBLIC glfw)
