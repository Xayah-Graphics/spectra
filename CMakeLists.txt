# ============================================================================
# Project
# ============================================================================
cmake_minimum_required(VERSION 4.4)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" SPECTRA_VERSION LIMIT_COUNT 1)
project(spectra VERSION "${SPECTRA_VERSION}" LANGUAGES C CXX)

# ============================================================================
# Project Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_MODULE_STD ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (WIN32)
    set(SPECTRA_BUILD_UI_DEFAULT ON)
else ()
    set(SPECTRA_BUILD_UI_DEFAULT OFF)
endif ()
option(SPECTRA_BUILD_UI "Build the Windows visualization UI into the spectra executable" ${SPECTRA_BUILD_UI_DEFAULT})
option(SPECTRA_BUILD_SDK "Build and install the Spectra SDK" OFF)
if (SPECTRA_BUILD_UI AND NOT WIN32)
    message(FATAL_ERROR "SPECTRA_BUILD_UI is currently supported only on Windows")
endif ()

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CACHE{CMAKE_INSTALL_PREFIX}
            TYPE PATH
            HELP "Spectra installation directory"
            FORCE
            VALUE "${CMAKE_SOURCE_DIR}/install"
    )
endif ()
include(GNUInstallDirs)

set(SPECTRA_BUILD_BIN_DIR "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
set(SPECTRA_BUILD_RESOURCE_DIR "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/spectra")
cmake_path(
        RELATIVE_PATH SPECTRA_BUILD_RESOURCE_DIR
        BASE_DIRECTORY "${SPECTRA_BUILD_BIN_DIR}"
        OUTPUT_VARIABLE SPECTRA_RESOURCE_RELATIVE_DIR
)

# ============================================================================
# Compiler Configuration
# ============================================================================
if (MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>")
endif ()

# ============================================================================
# Dependencies
# ============================================================================
include(cmake/Dependencies.cmake)
spectra_require_dependency(slang)
spectra_require_dependency(vulkan)
spectra_require_dependency(lodepng)
spectra_require_dependency(tinyexr)
spectra_require_dependency(openvdb)
spectra_require_dependency(openusd)
if (SPECTRA_BUILD_UI)
    spectra_require_dependency(glfw)
    spectra_require_dependency(imgui)
    spectra_require_dependency(imguizmo)
endif ()
if (MSVC)
    # MSVC interprets a bare @cmake_cxx_std.lib as a response file.
    add_link_options("$<$<LINK_LANGUAGE:CXX>:${PROJECT_BINARY_DIR}/@cmake_cxx_std.lib>")
endif ()
include(cmake/Shaders.cmake)

# ============================================================================
# Core
# ============================================================================
add_library(spectra_core STATIC)
target_sources(
        spectra_core
        PRIVATE
        spectra/core/runtime/runtime.cpp
        spectra/core/runtime/device.cpp
        spectra/core/runtime/resources.cpp
        spectra/core/runtime/frames.cpp
        spectra/core/math/math.cpp
        spectra/core/headless.cpp
        spectra/core/scene/scene.cpp
        spectra/core/scene/assets.cpp
        spectra/core/scene/usd.cpp
        spectra/core/scene/io.cpp
        spectra/core/simulation/provider.cpp
        spectra/core/simulation/runtime.cpp
        spectra/core/render/gpu_scene.cpp
        spectra/core/headless/output.cpp
        spectra/core/render/rasterizer/compiler.cpp
        spectra/core/render/rasterizer/rasterizer.cpp
        spectra/core/render/pathtracer/resources.cpp
        spectra/core/render/pathtracer/compiler.cpp
        spectra/core/render/pathtracer.cpp
        spectra/core/render/render.cpp
        spectra/core/render/display/neural_field.cpp
        spectra/core/render/display/visualization.cpp
        spectra/core/render/display/diagnostics.cpp
        spectra/core/render/display/overlay.cpp
        spectra/core/render/display/display.cpp
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        spectra/core/runtime/device.ixx
        spectra/core/runtime/resources.ixx
        spectra/core/runtime/frames.ixx
        spectra/core/runtime/runtime.ixx
        spectra/core/math/math.ixx
        spectra/core/headless.ixx
        spectra/core/scene/scene.ixx
        spectra/core/scene/io.ixx
        spectra/core/simulation/simulation.ixx
        spectra/core/simulation/frame.ixx
        spectra/core/simulation/provider.ixx
        spectra/core/simulation/runtime.ixx
        spectra/core/render/types.ixx
        spectra/core/render/gpu_scene.ixx
        spectra/core/headless/output.ixx
        spectra/core/render/rasterizer/compiler.ixx
        spectra/core/render/rasterizer/rasterizer.ixx
        spectra/core/render/pathtracer/resources.ixx
        spectra/core/render/pathtracer/compiler.ixx
        spectra/core/render/pathtracer.ixx
        spectra/core/render/render.ixx
        spectra/core/render/display/types.ixx
        spectra/core/render/display/neural_field.ixx
        spectra/core/render/display/diagnostics.ixx
        spectra/core/render/display/visualization.ixx
        spectra/core/render/display/overlay.ixx
        spectra/core/render/display/display.ixx
        PUBLIC
        FILE_SET generated_render_modules TYPE CXX_MODULES
        BASE_DIRS "${PROJECT_BINARY_DIR}"
        FILES
        "${SPECTRA_RASTER_ABI_MODULE}"
        "${SPECTRA_PATH_TRACER_ABI_MODULE}"
        "${SPECTRA_RENDER_ABI_MODULE}"
        PRIVATE
        FILE_SET internal_modules TYPE CXX_MODULES FILES
        spectra/core/scene/assets.ixx
        spectra/core/scene/usd.ixx
        PRIVATE
        FILE_SET generated_modules TYPE CXX_MODULES
        BASE_DIRS "${PROJECT_BINARY_DIR}"
        FILES "${SPECTRA_PATH_TRACER_SHADER_ENTRIES_MODULE}"
)
target_include_directories(
        spectra_core
        PRIVATE
        "${PROJECT_SOURCE_DIR}/sdk/include"
        "${PROJECT_SOURCE_DIR}/sdk/internal"
)
target_link_libraries(
        spectra_core
        PUBLIC
        spectra::vulkan
        PRIVATE
        spectra::lodepng
        spectra::tinyexr
        spectra::openvdb
        spectra::openusd
)
add_dependencies(spectra_core spectra_shaders spectra_openvdb_runtime spectra_openusd_runtime)

# ============================================================================
# Editor
# ============================================================================
if (SPECTRA_BUILD_UI)
    add_library(spectra_editor STATIC)
    target_sources(
            spectra_editor
            PRIVATE
            spectra/editor/editor.cpp
            spectra/editor/document/document.cpp
            spectra/editor/platform/window.cpp
            spectra/editor/platform/presentation.cpp
            spectra/editor/platform/dialogs.cpp
            spectra/editor/viewport/viewport.cpp
            spectra/editor/viewport/picker.cpp
            spectra/editor/ui/imgui.cpp
            spectra/editor/ui/inspector.cpp
            spectra/editor/ui/simulation.cpp
            spectra/editor/ui/ui.cpp
            PUBLIC
            FILE_SET cxx_modules TYPE CXX_MODULES FILES
            spectra/editor/editor.ixx
            PRIVATE
            FILE_SET editor_modules TYPE CXX_MODULES FILES
            spectra/editor/platform/window.ixx
            spectra/editor/document/document.ixx
            spectra/editor/platform/presentation.ixx
            spectra/editor/platform/dialogs.ixx
            spectra/editor/viewport/viewport.ixx
            spectra/editor/viewport/picker.ixx
            spectra/editor/ui/imgui.ixx
            spectra/editor/ui/inspector.ixx
            spectra/editor/ui/simulation.ixx
            spectra/editor/ui/ui.ixx
            PRIVATE
            FILE_SET generated_editor_modules TYPE CXX_MODULES
            BASE_DIRS "${PROJECT_BINARY_DIR}"
            FILES "${SPECTRA_EDITOR_ABI_MODULE}"
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
# Executable
# ============================================================================
add_executable(
        spectra
        main.cpp
        spectra/command_line.cpp
)
target_sources(
        spectra
        PRIVATE
        FILE_SET command_line_module TYPE CXX_MODULES FILES
        spectra/command_line.ixx
)
set_target_properties(
        spectra
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${SPECTRA_BUILD_BIN_DIR}>"
)
if (LINUX)
    set_target_properties(spectra PROPERTIES INSTALL_RPATH "$ORIGIN")
endif ()
if (WIN32)
    target_sources(spectra PRIVATE spectra.rc)
endif ()
target_compile_definitions(
        spectra
        PRIVATE
        SPECTRA_VERSION="${PROJECT_VERSION}"
        SPECTRA_RESOURCE_DIRECTORY="${SPECTRA_RESOURCE_RELATIVE_DIR}"
)
target_link_libraries(spectra PRIVATE spectra_core)
if (SPECTRA_BUILD_UI)
    target_compile_definitions(spectra PRIVATE SPECTRA_HAS_EDITOR)
    target_link_libraries(spectra PRIVATE spectra_editor)
endif ()

# ============================================================================
# Runtime Resources
# ============================================================================
set(
        SPECTRA_SPECTRAL_ASSETS
        "${PROJECT_SOURCE_DIR}/spectra/core/render/assets/srgb.rgb2spec"
        "${PROJECT_SOURCE_DIR}/spectra/core/render/assets/rec2020.rgb2spec"
        "${PROJECT_SOURCE_DIR}/spectra/core/render/assets/aces2065_1.rgb2spec"
        "${PROJECT_SOURCE_DIR}/spectra/core/render/assets/cie1931.spectrum"
)
set(SPECTRA_SAMPLING_ASSET "${PROJECT_SOURCE_DIR}/spectra/core/render/assets/sampling.tables")
set(SPECTRA_RUNTIME_RESOURCE_STAMP "${PROJECT_BINARY_DIR}/spectra_runtime_resources.stamp")
add_custom_command(
        OUTPUT "${SPECTRA_RUNTIME_RESOURCE_STAMP}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SPECTRA_BUILD_RESOURCE_DIR}/shaders"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SPECTRA_BUILD_RESOURCE_DIR}/pathtracer/shaders"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SPECTRA_BUILD_RESOURCE_DIR}/pathtracer/spectral"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPECTRA_BUILD_RESOURCE_DIR}/pathtracer/spectral"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SPECTRA_RENDER_SHADER_OUTPUT_DIR}" "${SPECTRA_BUILD_RESOURCE_DIR}/shaders"
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
# SDK
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
        DIRECTORY "${SPECTRA_RENDER_SHADER_OUTPUT_DIR}/"
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
