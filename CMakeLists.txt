cmake_minimum_required(VERSION 4.3.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
project(spectra VERSION 1.0.0 LANGUAGES CXX CUDA)


# ============================================================================
# C++ Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)


# ============================================================================
# Dependencies
# ============================================================================
find_package(Vulkan 1.4 REQUIRED)
find_program(SLANGC_EXECUTABLE slangc REQUIRED)
add_subdirectory(vendor/glfw)


# ============================================================================
# Shaders
# ============================================================================
set(SPECTRA_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(SPECTRA_VIEWPORT_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/viewport_grid.slang)
set(SPECTRA_SHADER_OUTPUTS ${SPECTRA_SHADER_OUTPUT_DIR}/viewport_grid.vert.spv ${SPECTRA_SHADER_OUTPUT_DIR}/viewport_grid.frag.spv)
add_custom_command(
        OUTPUT ${SPECTRA_SHADER_OUTPUTS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_SHADER_OUTPUT_DIR}
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_VIEWPORT_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry vertex_main -stage vertex -o ${SPECTRA_SHADER_OUTPUT_DIR}/viewport_grid.vert.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_VIEWPORT_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry fragment_main -stage fragment -o ${SPECTRA_SHADER_OUTPUT_DIR}/viewport_grid.frag.spv
        DEPENDS ${SPECTRA_VIEWPORT_SHADER}
        VERBATIM
)
add_custom_target(spectra_shaders DEPENDS ${SPECTRA_SHADER_OUTPUTS})


# ============================================================================
# Libraries
# ============================================================================
add_library(spectra)
add_dependencies(spectra spectra_shaders)
target_sources(
        spectra
        PRIVATE
        modules/spectra.cpp

        vendor/imgui/imgui.cpp
        vendor/imgui/imgui_draw.cpp
        vendor/imgui/imgui_tables.cpp
        vendor/imgui/imgui_widgets.cpp
        vendor/imgui/backends/imgui_impl_glfw.cpp
        vendor/imgui/backends/imgui_impl_vulkan.cpp
        vendor/ImGuizmo/ImGuizmo.cpp
        vendor/ImGuizmo/GraphEditor.cpp
        vendor/ImGuizmo/ImCurveEdit.cpp
        vendor/ImGuizmo/ImGradient.cpp
        vendor/ImGuizmo/ImSequencer.cpp
        vendor/ImGuizmo/ImVectorEditor.cpp
        vendor/ImNodeFlow/ImNodeFlow.cpp
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        modules/camera.ixx
        modules/spectra.ixx
)
target_link_libraries(spectra PRIVATE Vulkan::Vulkan glfw)
target_compile_definitions(spectra PRIVATE IMGUI_DEFINE_MATH_OPERATORS SPECTRA_SHADER_DIR="${SPECTRA_SHADER_OUTPUT_DIR}")
target_include_directories(spectra PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGuizmo ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImNodeFlow ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw/include)
set_property(TARGET spectra PROPERTY CXX_MODULE_STD ON)

add_executable(test main.cpp)
target_link_libraries(test PRIVATE spectra)
set_property(TARGET test PROPERTY CXX_MODULE_STD ON)
