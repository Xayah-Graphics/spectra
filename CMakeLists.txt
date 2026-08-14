cmake_minimum_required(VERSION 4.4.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
project(spectra VERSION 2.0.0 LANGUAGES C CXX)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Spectra installation directory" FORCE)
endif ()
include(GNUInstallDirs)

set(SPECTRA_BUILD_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
set(SPECTRA_BUILD_RESOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/spectra")
cmake_path(
        RELATIVE_PATH SPECTRA_BUILD_RESOURCE_DIR
        BASE_DIRECTORY "${SPECTRA_BUILD_BIN_DIR}"
        OUTPUT_VARIABLE SPECTRA_RESOURCE_RELATIVE_DIR
)


# ============================================================================
# Project Configuration
# ============================================================================
if (WIN32)
    set(SPECTRA_BUILD_UI_DEFAULT ON)
else ()
    set(SPECTRA_BUILD_UI_DEFAULT OFF)
endif ()
option(SPECTRA_BUILD_UI "Build the Windows visualization UI into the spectra executable" ${SPECTRA_BUILD_UI_DEFAULT})
option(SPECTRA_BUILD_SDK "Build and install the Spectra SDK" OFF)
if (SPECTRA_BUILD_UI AND NOT WIN32)
    message(FATAL_ERROR "SPECTRA_BUILD_UI is currently supported only on Windows.")
endif ()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_MODULE_STD ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>")
    # MSVC interprets CMake's relative @cmake_cxx_std.lib entry as a response file.
    add_link_options("$<$<LINK_LANGUAGE:CXX>:${CMAKE_CURRENT_BINARY_DIR}/@cmake_cxx_std.lib>")
endif ()

include(cmake/Dependencies.cmake)
include(cmake/Shaders.cmake)


# ============================================================================
# Spectra Core
# ============================================================================
add_library(spectra_core STATIC)
target_sources(
        spectra_core
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
        spectra/core/runtime/graphics.ixx
        spectra/core/runtime/resources.ixx
        spectra/core/runtime/frames.ixx
        spectra/core/runtime/runtime.ixx
        spectra/core/util/math.ixx
        spectra/core/headless.ixx
        spectra/core/scene/scene.ixx
        spectra/core/scene/format.ixx
        spectra/core/scene/document.ixx
        spectra/core/dynamics/contract.ixx
        spectra/core/dynamics/gpu.ixx
        spectra/core/dynamics/runtime.ixx
        spectra/core/render/contract.ixx
        spectra/core/render/gpu_scene.ixx
        spectra/core/render/capture.ixx
        spectra/core/render/rasterizer.ixx
        spectra/core/render/pathtracer/resources.ixx
        spectra/core/render/pathtracer/scene.ixx
        spectra/core/render/pathtracer.ixx
        spectra/core/render/render.ixx
        spectra/core/render/display.ixx
        spectra/core/render/composition/diagnostics.ixx
        spectra/core/render/composition/visualization.ixx
        spectra/core/render/composition/overlay.ixx
        spectra/core/render/composition/composition.ixx
        PUBLIC
        FILE_SET generated_render_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_BINARY_DIR}"
        FILES
        "${SPECTRA_RASTER_ABI_MODULE}"
        "${SPECTRA_PATH_TRACER_ABI_MODULE}"
        PRIVATE
        FILE_SET generated_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_BINARY_DIR}"
        FILES
        "${SPECTRA_PATH_TRACER_SHADER_ENTRIES_MODULE}"
        PRIVATE
        FILE_SET internal_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
        spectra/core/scene/asset_import.ixx
        PRIVATE
        spectra/core/runtime/runtime.cpp
        spectra/core/runtime/graphics.cpp
        spectra/core/runtime/resources.cpp
        spectra/core/runtime/frames.cpp
        spectra/core/util/math.cpp
        spectra/core/headless.cpp
        spectra/core/scene/scene.cpp
        spectra/core/scene/asset_import.cpp
        spectra/core/scene/format.cpp
        spectra/core/scene/document.cpp
        spectra/core/dynamics/runtime.cpp
        spectra/core/render/contract.cpp
        spectra/core/render/gpu_scene.cpp
        spectra/core/render/capture.cpp
        spectra/core/render/rasterizer.cpp
        spectra/core/render/pathtracer/resources.cpp
        spectra/core/render/pathtracer/scene.cpp
        spectra/core/render/pathtracer.cpp
        spectra/core/render/render.cpp
        spectra/core/render/display.cpp
        spectra/core/render/composition/visualization.cpp
        spectra/core/render/composition/diagnostics.cpp
        spectra/core/render/composition/overlay.cpp
        spectra/core/render/composition/composition.cpp
)
target_link_libraries(
        spectra_core
        PUBLIC
        spectra::vulkan
        PRIVATE
        spectra_ckdlpp
        spectra_lodepng
        spectra::tinyexr
        spectra::nanovdb
)
if (UNIX AND NOT APPLE)
    target_link_libraries(spectra_core PRIVATE dl)
endif ()
add_dependencies(spectra_core spectra_shaders)


# ============================================================================
# Spectra Editor
# ============================================================================
if (SPECTRA_BUILD_UI)
    add_library(spectra_editor STATIC)
    target_sources(
            spectra_editor
            PUBLIC
            FILE_SET cxx_modules TYPE CXX_MODULES
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
            FILES
            spectra/editor/editor.ixx
            PRIVATE
            FILE_SET editor_modules TYPE CXX_MODULES
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
            FILES
            spectra/editor/platform/window.ixx
            spectra/editor/platform/presentation.ixx
            spectra/editor/platform/dialogs.ixx
            spectra/editor/viewport/interaction.ixx
            spectra/editor/viewport/picker.ixx
            spectra/editor/ui/imgui.ixx
            spectra/editor/ui/ui.ixx
            PRIVATE
            spectra/editor/editor.cpp
            spectra/editor/platform/window.cpp
            spectra/editor/platform/presentation.cpp
            spectra/editor/platform/dialogs.cpp
            spectra/editor/viewport/interaction.cpp
            spectra/editor/viewport/picker.cpp
            spectra/editor/ui/imgui.cpp
            spectra/editor/ui/ui.cpp
    )
    target_compile_definitions(
            spectra_editor
            PRIVATE
            GLFW_INCLUDE_NONE
            GLFW_EXPOSE_NATIVE_WIN32
    )
    target_link_libraries(
            spectra_editor
            PRIVATE
            spectra_core
            spectra::imgui
            spectra::imguizmo
            glfw
            dwmapi
            ole32
    )
endif ()


# ============================================================================
# Spectra Executable
# ============================================================================
add_executable(spectra)
set_target_properties(
        spectra
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${SPECTRA_BUILD_BIN_DIR}>"
)
target_sources(spectra PRIVATE main.cpp)
if (WIN32)
    target_sources(spectra PRIVATE spectra.rc)
endif ()
target_compile_definitions(
        spectra
        PRIVATE
        SPECTRA_RESOURCE_DIRECTORY="${SPECTRA_RESOURCE_RELATIVE_DIR}"
)
target_link_libraries(spectra PRIVATE spectra_core xayah::util::xcli)
if (SPECTRA_BUILD_UI)
    target_compile_definitions(spectra PRIVATE SPECTRA_HAS_EDITOR)
    target_link_libraries(spectra PRIVATE spectra_editor)
endif ()


# ============================================================================
# Runtime Resources
# ============================================================================
set(
        SPECTRA_SPECTRAL_ASSETS
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/assets/srgb.rgb2spec"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/assets/rec2020.rgb2spec"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/assets/aces2065_1.rgb2spec"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/assets/cie1931.spectrum"
)
set(SPECTRA_SAMPLING_ASSET "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/assets/sampling.tables")
set(SPECTRA_RUNTIME_RESOURCE_STAMP "${CMAKE_CURRENT_BINARY_DIR}/spectra_runtime_resources.stamp")
add_custom_command(
        OUTPUT "${SPECTRA_RUNTIME_RESOURCE_STAMP}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SPECTRA_BUILD_RESOURCE_DIR}/shaders"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SPECTRA_BUILD_RESOURCE_DIR}/pathtracer/shaders"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SPECTRA_BUILD_RESOURCE_DIR}/pathtracer/spectral"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPECTRA_BUILD_RESOURCE_DIR}/pathtracer/spectral"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SPECTRA_RUNTIME_SHADER_OUTPUT_DIR}" "${SPECTRA_BUILD_RESOURCE_DIR}/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SPECTRA_PATH_TRACER_SHADER_OUTPUT_DIR}" "${SPECTRA_BUILD_RESOURCE_DIR}/pathtracer/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${SPECTRA_SPECTRAL_ASSETS} "${SPECTRA_BUILD_RESOURCE_DIR}/pathtracer/spectral"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SPECTRA_SAMPLING_ASSET}" "${SPECTRA_BUILD_RESOURCE_DIR}/pathtracer"
        COMMAND "${CMAKE_COMMAND}" -E touch "${SPECTRA_RUNTIME_RESOURCE_STAMP}"
        DEPENDS
        "${SPECTRA_SHADER_STAMP}"
        ${SPECTRA_SHADER_SPV_OUTPUTS}
        ${SPECTRA_SHADER_ENTRY_FILES}
        ${SPECTRA_SPECTRAL_ASSETS}
        "${SPECTRA_SAMPLING_ASSET}"
        VERBATIM
)
add_custom_target(spectra_runtime_resources ALL DEPENDS "${SPECTRA_RUNTIME_RESOURCE_STAMP}")
add_dependencies(spectra spectra_runtime_resources)


# ============================================================================
# Spectra SDK
# ============================================================================
if (SPECTRA_BUILD_SDK)
    add_subdirectory(sdk)
endif ()


# ============================================================================
# Installation
# ============================================================================
install(TARGETS spectra RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Spectra)
install(
        CODE
        "file(REMOVE_RECURSE \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/spectra/shaders\")
         file(REMOVE_RECURSE \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/spectra/pathtracer/shaders\")
         file(REMOVE_RECURSE \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/spectra/pathtracer/spectral\")"
        COMPONENT Spectra
)
install(
        DIRECTORY "${SPECTRA_RUNTIME_SHADER_OUTPUT_DIR}/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/spectra/shaders"
        COMPONENT Spectra
)
install(
        DIRECTORY "${SPECTRA_PATH_TRACER_SHADER_OUTPUT_DIR}/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/spectra/pathtracer/shaders"
        COMPONENT Spectra
)
install(
        FILES ${SPECTRA_SPECTRAL_ASSETS}
        DESTINATION "${CMAKE_INSTALL_DATADIR}/spectra/pathtracer/spectral"
        COMPONENT Spectra
)
install(
        FILES "${SPECTRA_SAMPLING_ASSET}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/spectra/pathtracer"
        COMPONENT Spectra
)
