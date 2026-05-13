cmake_minimum_required(VERSION 4.3.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
project(spectra VERSION 1.0.0 LANGUAGES CXX CUDA)

option(SPECTRA_BUILD_EXTENSIONS "Build optional extensions" ON)


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
set(SPECTRA_VOLUME_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volume.slang)
set(SPECTRA_MESH_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.slang)
set(SPECTRA_PARTICLES_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/particles.slang)
set(SPECTRA_BOUNDING_BOX_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bounding_box.slang)
set(
        SPECTRA_SHADER_OUTPUTS
        ${SPECTRA_SHADER_OUTPUT_DIR}/viewport_grid.vert.spv
        ${SPECTRA_SHADER_OUTPUT_DIR}/viewport_grid.frag.spv
        ${SPECTRA_SHADER_OUTPUT_DIR}/volume.vert.spv
        ${SPECTRA_SHADER_OUTPUT_DIR}/volume.frag.spv
        ${SPECTRA_SHADER_OUTPUT_DIR}/mesh.vert.spv
        ${SPECTRA_SHADER_OUTPUT_DIR}/mesh.frag.spv
        ${SPECTRA_SHADER_OUTPUT_DIR}/particles.vert.spv
        ${SPECTRA_SHADER_OUTPUT_DIR}/particles.frag.spv
        ${SPECTRA_SHADER_OUTPUT_DIR}/bounding_box.vert.spv
        ${SPECTRA_SHADER_OUTPUT_DIR}/bounding_box.frag.spv
)
add_custom_command(
        OUTPUT ${SPECTRA_SHADER_OUTPUTS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_SHADER_OUTPUT_DIR}
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_VIEWPORT_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry vertex_main -stage vertex -o ${SPECTRA_SHADER_OUTPUT_DIR}/viewport_grid.vert.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_VIEWPORT_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry fragment_main -stage fragment -o ${SPECTRA_SHADER_OUTPUT_DIR}/viewport_grid.frag.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_VOLUME_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry vertex_main -stage vertex -o ${SPECTRA_SHADER_OUTPUT_DIR}/volume.vert.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_VOLUME_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry fragment_main -stage fragment -o ${SPECTRA_SHADER_OUTPUT_DIR}/volume.frag.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_MESH_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry vertex_main -stage vertex -o ${SPECTRA_SHADER_OUTPUT_DIR}/mesh.vert.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_MESH_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry fragment_main -stage fragment -o ${SPECTRA_SHADER_OUTPUT_DIR}/mesh.frag.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_PARTICLES_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry vertex_main -stage vertex -o ${SPECTRA_SHADER_OUTPUT_DIR}/particles.vert.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_PARTICLES_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry fragment_main -stage fragment -o ${SPECTRA_SHADER_OUTPUT_DIR}/particles.frag.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_BOUNDING_BOX_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry vertex_main -stage vertex -o ${SPECTRA_SHADER_OUTPUT_DIR}/bounding_box.vert.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_BOUNDING_BOX_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry fragment_main -stage fragment -o ${SPECTRA_SHADER_OUTPUT_DIR}/bounding_box.frag.spv
        DEPENDS ${SPECTRA_VIEWPORT_SHADER} ${SPECTRA_VOLUME_SHADER} ${SPECTRA_MESH_SHADER} ${SPECTRA_PARTICLES_SHADER} ${SPECTRA_BOUNDING_BOX_SHADER}
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
        modules/scene_object.cpp
        modules/volume.cpp
        modules/mesh.cpp
        modules/particles.cpp
        modules/scene.cpp
        modules/scene_frame.cpp

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
        modules/scene_object.ixx
        modules/volume.ixx
        modules/mesh.ixx
        modules/particles.ixx
        modules/scene.ixx
        modules/scene_frame.ixx
        modules/spectra.ixx
)
target_link_libraries(spectra PRIVATE Vulkan::Vulkan glfw)
target_compile_definitions(spectra PRIVATE IMGUI_DEFINE_MATH_OPERATORS SPECTRA_SHADER_DIR="${SPECTRA_SHADER_OUTPUT_DIR}")
target_include_directories(spectra PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGuizmo ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImNodeFlow ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw/include)
set_property(TARGET spectra PROPERTY CXX_MODULE_STD ON)

if (SPECTRA_BUILD_EXTENSIONS)
    # ============================================================================
    # CUDA Configuration
    # ============================================================================
    set(CMAKE_CUDA_STANDARD 20)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(CMAKE_CUDA_EXTENSIONS OFF)
    set(CMAKE_CUDA_ARCHITECTURES 120)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    find_package(CUDAToolkit REQUIRED)
    add_library(pyro)
    target_sources(
            pyro
            PRIVATE
            ext/pyro.cpp
            ext/pyro.cu
            PUBLIC
            FILE_SET cxx_modules TYPE CXX_MODULES FILES
            ext/pyro.ixx
    )
    target_link_libraries(pyro PUBLIC CUDA::cudart CUDA::cublas CUDA::cusparse)
    set_property(TARGET pyro PROPERTY CXX_MODULE_STD ON)

    add_library(bouncingball)
    target_sources(
            bouncingball
            PRIVATE
            ext/bouncingball.cpp
            PUBLIC
            FILE_SET cxx_modules TYPE CXX_MODULES FILES
            ext/bouncingball.ixx
    )
    target_link_libraries(bouncingball PUBLIC spectra)
    set_property(TARGET bouncingball PROPERTY CXX_MODULE_STD ON)

    add_executable(test main.cpp)
    target_link_libraries(test PRIVATE spectra pyro bouncingball)
    set_property(TARGET test PROPERTY CXX_MODULE_STD ON)
endif ()
