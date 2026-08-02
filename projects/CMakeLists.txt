cmake_minimum_required(VERSION 4.4.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
project(spectra_projects VERSION 1.0.0 LANGUAGES CXX CUDA)


# ============================================================================
# Toolchain
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)
set(CMAKE_CUDA_ARCHITECTURES 120)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if (MSVC)
    add_compile_options(
            "$<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>"
            "$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/Zc:preprocessor>"
    )
endif ()

find_package(CUDAToolkit 13.3 EXACT REQUIRED)
find_package(SpectraPluginSDK 11 CONFIG REQUIRED)

set(CMAKE_CXX_MODULE_STD ON)
add_link_options("$<$<LINK_LANGUAGE:CXX>:${CMAKE_CURRENT_BINARY_DIR}/@cmake_cxx_std.lib>")


# ============================================================================
# Dynamic Scene Bundles
# ============================================================================
function(spectra_add_provider target provider scene_directory)
    cmake_path(GET scene_directory FILENAME scene_name)
    set(bundle_directory "${PROJECT_BINARY_DIR}/bundles/${scene_name}")
    set(bundle_stamp "${PROJECT_BINARY_DIR}/bundles/.stamps/${scene_name}.stamp")
    set_target_properties(
            ${target}
            PROPERTIES
            PREFIX ""
            OUTPUT_NAME "${provider}.spectra-plugin"
            RUNTIME_OUTPUT_DIRECTORY "${bundle_directory}"
            LIBRARY_OUTPUT_DIRECTORY "${bundle_directory}"
    )
    string(MAKE_C_IDENTIFIER "spectra_${scene_name}_bundle" bundle_target)
    if (NOT TARGET ${bundle_target})
        file(GLOB_RECURSE scene_files CONFIGURE_DEPENDS "${scene_directory}/*")
        add_custom_command(
                OUTPUT "${bundle_stamp}"
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${scene_directory}"
                "${bundle_directory}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_BINARY_DIR}/bundles/.stamps"
                COMMAND "${CMAKE_COMMAND}" -E touch "${bundle_stamp}"
                DEPENDS ${scene_files}
                VERBATIM
        )
        add_custom_target(${bundle_target} ALL DEPENDS "${bundle_stamp}")
    endif ()
    add_dependencies(${bundle_target} ${target})
endfunction()

add_library(spectra_cuda_plugin_interop STATIC common/cuda_interop.cu)
target_include_directories(spectra_cuda_plugin_interop PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/common")
target_link_libraries(spectra_cuda_plugin_interop PUBLIC CUDA::cudart)

add_subdirectory(bouncing_ball)
add_subdirectory(cloth)
add_subdirectory(pyro)
add_subdirectory(sparkles)
