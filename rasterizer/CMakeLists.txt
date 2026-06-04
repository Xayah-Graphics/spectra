add_library(spectra_rasterizer)
add_library(spectra::rasterizer ALIAS spectra_rasterizer)

find_program(SLANGC_EXECUTABLE NAMES slangc REQUIRED)
set(SPECTRA_RASTERIZER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(SPECTRA_RASTERIZER_MESH_SHADER "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.slang")
set(SPECTRA_RASTERIZER_MESH_VERTEX_HEADER "${SPECTRA_RASTERIZER_GENERATED_DIR}/spectra_rasterizer_mesh_vertex_spv.h")
set(SPECTRA_RASTERIZER_MESH_FRAGMENT_HEADER "${SPECTRA_RASTERIZER_GENERATED_DIR}/spectra_rasterizer_mesh_fragment_spv.h")
add_custom_command(
        OUTPUT "${SPECTRA_RASTERIZER_MESH_VERTEX_HEADER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPECTRA_RASTERIZER_GENERATED_DIR}"
        COMMAND "${SLANGC_EXECUTABLE}" "${SPECTRA_RASTERIZER_MESH_SHADER}" -target spirv -profile glsl_460 -entry vertex_main -stage vertex -matrix-layout-row-major -source-embed-style u32 -source-embed-name spectra_rasterizer_mesh_vertex_spv -o "${SPECTRA_RASTERIZER_MESH_VERTEX_HEADER}"
        DEPENDS "${SPECTRA_RASTERIZER_MESH_SHADER}"
        VERBATIM
)
add_custom_command(
        OUTPUT "${SPECTRA_RASTERIZER_MESH_FRAGMENT_HEADER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPECTRA_RASTERIZER_GENERATED_DIR}"
        COMMAND "${SLANGC_EXECUTABLE}" "${SPECTRA_RASTERIZER_MESH_SHADER}" -target spirv -profile glsl_460 -entry fragment_main -stage fragment -matrix-layout-row-major -source-embed-style u32 -source-embed-name spectra_rasterizer_mesh_fragment_spv -o "${SPECTRA_RASTERIZER_MESH_FRAGMENT_HEADER}"
        DEPENDS "${SPECTRA_RASTERIZER_MESH_SHADER}"
        VERBATIM
)
target_sources(
        spectra_rasterizer
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        rasterizer.ixx
        PRIVATE
        rasterizer.cpp
        "${SPECTRA_RASTERIZER_MESH_VERTEX_HEADER}"
        "${SPECTRA_RASTERIZER_MESH_FRAGMENT_HEADER}"
)
target_link_libraries(spectra_rasterizer PUBLIC Vulkan::Vulkan vendor::imgui)
set_property(TARGET spectra_rasterizer PROPERTY CXX_MODULE_STD ON)
target_compile_definitions(spectra_rasterizer PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_include_directories(spectra_rasterizer PRIVATE ${SPECTRA_ROOT_DIR}/vendor "${SPECTRA_RASTERIZER_GENERATED_DIR}")
