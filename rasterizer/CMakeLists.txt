add_library(spectra_rasterizer)
add_library(spectra::rasterizer ALIAS spectra_rasterizer)
target_sources(
        spectra_rasterizer
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        rasterizer.ixx
        PRIVATE
        rasterizer.cpp
)
target_link_libraries(spectra_rasterizer PUBLIC Vulkan::Vulkan vendor::imgui)
set_property(TARGET spectra_rasterizer PROPERTY CXX_MODULE_STD ON)
target_compile_definitions(spectra_rasterizer PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_include_directories(spectra_rasterizer PRIVATE ${SPECTRA_ROOT_DIR}/vendor)
