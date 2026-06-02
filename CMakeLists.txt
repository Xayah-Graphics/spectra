cmake_minimum_required(VERSION 4.3.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
project(spectra VERSION 1.0.0 LANGUAGES C CXX CUDA)


# ============================================================================
# C++ Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)


# ============================================================================
# CUDA Configuration
# ============================================================================
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)
set(CMAKE_CUDA_ARCHITECTURES 120)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type" FORCE)
endif ()


# ============================================================================
# Dependencies
# ============================================================================
find_package(Vulkan 1.4 REQUIRED)
find_package(Threads REQUIRED)
find_package(CUDAToolkit REQUIRED)
if (CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 13.0)
    message(FATAL_ERROR "spectra requires CUDA compiler 13.0 or newer; found ${CMAKE_CUDA_COMPILER_VERSION}.")
endif ()
if (CUDAToolkit_VERSION VERSION_LESS 13.0)
    message(FATAL_ERROR "spectra requires CUDA Toolkit 13.0 or newer; found ${CUDAToolkit_VERSION}.")
endif ()

add_subdirectory(vendor/glfw)
add_subdirectory(vendor/pbrt)


# ============================================================================
# Spectra GPU
# ============================================================================
option(SPECTRA_FLOAT_AS_DOUBLE "Use 64-bit floats for spectra_gpu" OFF)
set(SPECTRA_OPTIX_PATH "" CACHE PATH "Path to OptiX SDK for spectra_gpu")
set(SPECTRA_OPTIX_GPU_ARCH "sm_120" CACHE STRING "CUDA architecture used when compiling spectra OptiX-IR")
set(SPECTRA_OPTIX_CACHE_DIR "${CMAKE_CURRENT_BINARY_DIR}/optix-cache" CACHE PATH "OptiX disk cache directory for spectra_gpu")
set(SPECTRA_OPTIX_CACHE_SIZE_MB 512 CACHE STRING "OptiX disk cache high-water size in MiB for spectra_gpu")

if (SPECTRA_OPTIX_PATH STREQUAL "")
    file(GLOB SPECTRA_OPTIX_DEFAULT_PATHS LIST_DIRECTORIES true
            "C:/ProgramData/NVIDIA Corporation/OptiX SDK *"
            "/mnt/c/ProgramData/NVIDIA Corporation/OptiX SDK *"
            "/opt/NVIDIA-OptiX-SDK-*"
            "/opt/OptiX SDK *"
            "/opt/optix*"
            "/usr/local/NVIDIA-OptiX-SDK-*"
            "/usr/local/OptiX SDK *"
            "/usr/local/optix*"
    )
    foreach (SPECTRA_OPTIX_DEFAULT_PATH IN LISTS SPECTRA_OPTIX_DEFAULT_PATHS)
        if (EXISTS "${SPECTRA_OPTIX_DEFAULT_PATH}/include/optix.h")
            list(APPEND SPECTRA_OPTIX_CANDIDATE_PATHS "${SPECTRA_OPTIX_DEFAULT_PATH}")
        endif ()
    endforeach ()
    if (SPECTRA_OPTIX_CANDIDATE_PATHS)
        list(SORT SPECTRA_OPTIX_CANDIDATE_PATHS COMPARE NATURAL ORDER DESCENDING)
        list(GET SPECTRA_OPTIX_CANDIDATE_PATHS 0 SPECTRA_OPTIX_PATH)
        set(SPECTRA_OPTIX_PATH "${SPECTRA_OPTIX_PATH}" CACHE PATH "Path to OptiX SDK for spectra_gpu" FORCE)
    endif ()
endif ()
if (SPECTRA_OPTIX_PATH STREQUAL "")
    message(FATAL_ERROR "SPECTRA_OPTIX_PATH must point to an OptiX SDK root; no default installation was found.")
endif ()
math(EXPR SPECTRA_OPTIX_CACHE_SIZE_BYTES "${SPECTRA_OPTIX_CACHE_SIZE_MB} * 1024 * 1024")

set(SPECTRA_PROJECT_SCENE_ROOT "${PROJECT_SOURCE_DIR}/scene")
set(SPECTRA_GPU_DEFINITIONS
        PTEX_STATIC
)
if (SPECTRA_FLOAT_AS_DOUBLE)
    list(APPEND SPECTRA_GPU_DEFINITIONS SPECTRA_FLOAT_AS_DOUBLE)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if (NOT MSVC)
        message(FATAL_ERROR "Windows builds require MSVC.")
    endif ()
    list(APPEND SPECTRA_GPU_DEFINITIONS
            SPECTRA_IS_WINDOWS
            SPECTRA_IS_MSVC
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
            _ENABLE_EXTENDED_ALIGNED_STORAGE
            SPECTRA_HAS_INTRIN_H
            "SPECTRA_NOINLINE=__declspec(noinline)"
            "SPECTRA_RESTRICT=__restrict"
            SPECTRA_HAVE__ALIGNED_MALLOC
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SPECTRA_GPU_DEFINITIONS
            SPECTRA_IS_LINUX
            "SPECTRA_NOINLINE=__attribute__((noinline))"
            "SPECTRA_RESTRICT=__restrict__"
            SPECTRA_HAVE_POSIX_MEMALIGN
    )
else ()
    message(FATAL_ERROR "Only Linux/WSL and Windows/MSVC are supported by spectra_gpu.")
endif ()

set(SPECTRA_GPU_CUDA_WARNING_OPTIONS
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/Zc:preprocessor>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4244>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4267>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4305>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4552>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4838>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4843>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd26451>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd26495>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4334>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4146>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=partial_override>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=virtual_function_decl_hidden>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=integer_sign_change>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=declared_but_not_referenced>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=implicit_return_from_non_void_function>"
)
set(SPECTRA_GPU_CUDA_COMPILE_OPTIONS
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:--forward-slash-prefix-opts>>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--extended-lambda>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--forward-unknown-to-host-compiler>"
)

get_filename_component(SPECTRA_GPU_CUDA_COMPILER_BIN "${CMAKE_CUDA_COMPILER}" DIRECTORY)
find_program(
        SPECTRA_GPU_BIN2C
        NAMES bin2c
        PATHS ${SPECTRA_GPU_CUDA_COMPILER_BIN} ${CUDAToolkit_BIN_DIR}
        NO_DEFAULT_PATH
)
if (NOT SPECTRA_GPU_BIN2C)
    message(FATAL_ERROR "CUDA bin2c was not found next to '${CMAKE_CUDA_COMPILER}'.")
endif ()

set(SPECTRA_GPU_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/spectra_gpu)
file(MAKE_DIRECTORY "${SPECTRA_GPU_GENERATED_DIR}")
file(MAKE_DIRECTORY "${SPECTRA_OPTIX_CACHE_DIR}")
string(REPLACE "\\" "/" SPECTRA_OPTIX_CACHE_DIR_CONFIG "${SPECTRA_OPTIX_CACHE_DIR}")
string(REPLACE "\"" "\\\"" SPECTRA_OPTIX_CACHE_DIR_CONFIG "${SPECTRA_OPTIX_CACHE_DIR_CONFIG}")
file(WRITE "${SPECTRA_GPU_GENERATED_DIR}/spectra_optix_config.h"
        "#pragma once\n"
        "\n"
        "#include <cstddef>\n"
        "\n"
        "namespace spectra::optix\n"
        "{\n"
        "    inline constexpr char SpectraOptiXCacheDir[] = \"${SPECTRA_OPTIX_CACHE_DIR_CONFIG}\";\n"
        "    inline constexpr std::size_t SpectraOptiXCacheSizeBytes = ${SPECTRA_OPTIX_CACHE_SIZE_BYTES}ull;\n"
        "} // namespace spectra::optix\n")
set(SPECTRA_GPU_INCLUDE_DIRS
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/vendor/pbrt
        ${STB_INCLUDE}
        ${QOI_INCLUDE}
        ${ZLIB_INCLUDE_DIRS}
        ${PTEX_INCLUDE}
        ${NANOVDB_INCLUDE}
        ${PROJECT_BINARY_DIR}
        ${SPECTRA_GPU_GENERATED_DIR}
        ${SPECTRA_OPTIX_PATH}/include
        ${CUDAToolkit_INCLUDE_DIRS}
)
set(SPECTRA_GPU_BIN2C_SCRIPT ${SPECTRA_GPU_GENERATED_DIR}/spectra_gpu_bin2c_embed.cmake)
file(WRITE "${SPECTRA_GPU_BIN2C_SCRIPT}" [=[
set(file_contents)
foreach(obj IN LISTS OBJECTS)
    get_filename_component(obj_ext "${obj}" EXT)
    get_filename_component(obj_dir "${obj}" DIRECTORY)
    if(obj_ext MATCHES "\\.optixir$")
        execute_process(
                COMMAND "${BIN_TO_C_COMMAND}" --name "${VAR_NAME}" "${obj}"
                WORKING_DIRECTORY "${obj_dir}"
                RESULT_VARIABLE result
                OUTPUT_VARIABLE output
                ERROR_VARIABLE error_var
        )
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "bin2c failed for ${obj}: ${error_var}")
        endif()
        file(SIZE "${obj}" obj_size)
        string(APPEND file_contents " \n${output}\nunsigned long long ${VAR_NAME}_SIZE = ${obj_size}ull;\n")
    endif()
endforeach()
if(file_contents STREQUAL "")
    message(FATAL_ERROR "No OptiX-IR input was passed to bin2c: ${OBJECTS}")
endif()
file(WRITE "${OUTPUT}" "${file_contents}")
]=])

add_executable(spectra_gpu_soac soac.cpp)
target_compile_definitions(spectra_gpu_soac PRIVATE ${SPECTRA_GPU_DEFINITIONS})
target_include_directories(spectra_gpu_soac PRIVATE ${SPECTRA_GPU_INCLUDE_DIRS})

add_custom_command(
        OUTPUT ${SPECTRA_GPU_GENERATED_DIR}/spectra_soa.cuh
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_GPU_GENERATED_DIR}
        COMMAND spectra_gpu_soac ${CMAKE_CURRENT_SOURCE_DIR}/spectra/pathtracer/core/spectra.soa > ${SPECTRA_GPU_GENERATED_DIR}/spectra_soa.cuh
        DEPENDS spectra_gpu_soac ${CMAKE_CURRENT_SOURCE_DIR}/spectra/pathtracer/core/spectra.soa
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_GPU_GENERATED_DIR}/spectra_wavefront_workitems_soa.cuh
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_GPU_GENERATED_DIR}
        COMMAND spectra_gpu_soac ${CMAKE_CURRENT_SOURCE_DIR}/spectra/pathtracer/wavefront/workitems.soa > ${SPECTRA_GPU_GENERATED_DIR}/spectra_wavefront_workitems_soa.cuh
        DEPENDS spectra_gpu_soac ${CMAKE_CURRENT_SOURCE_DIR}/spectra/pathtracer/wavefront/workitems.soa
        VERBATIM
)
set(SPECTRA_GPU_SOA_GENERATED
        ${SPECTRA_GPU_GENERATED_DIR}/spectra_soa.cuh
        ${SPECTRA_GPU_GENERATED_DIR}/spectra_wavefront_workitems_soa.cuh
)
add_custom_target(spectra_gpu_soa_generated DEPENDS ${SPECTRA_GPU_SOA_GENERATED})

add_executable(spectra_gpu_rgb2spec_opt rgb2spec_opt.cpp)
target_compile_definitions(spectra_gpu_rgb2spec_opt PRIVATE ${SPECTRA_GPU_DEFINITIONS})
target_include_directories(spectra_gpu_rgb2spec_opt PRIVATE ${SPECTRA_GPU_INCLUDE_DIRS})
target_link_libraries(spectra_gpu_rgb2spec_opt PRIVATE Threads::Threads)

set(SPECTRA_GPU_RGB_TO_SPECTRUM_SOURCES
        ${SPECTRA_GPU_GENERATED_DIR}/rgbspectrum_aces.cpp
        ${SPECTRA_GPU_GENERATED_DIR}/rgbspectrum_dci_p3.cpp
        ${SPECTRA_GPU_GENERATED_DIR}/rgbspectrum_rec2020.cpp
        ${SPECTRA_GPU_GENERATED_DIR}/rgbspectrum_srgb.cpp
)
add_custom_command(
        OUTPUT ${SPECTRA_GPU_GENERATED_DIR}/rgbspectrum_aces.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_GPU_GENERATED_DIR}
        COMMAND spectra_gpu_rgb2spec_opt 64 ${SPECTRA_GPU_GENERATED_DIR}/rgbspectrum_aces.cpp ACES2065_1
        DEPENDS spectra_gpu_rgb2spec_opt
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_GPU_GENERATED_DIR}/rgbspectrum_dci_p3.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_GPU_GENERATED_DIR}
        COMMAND spectra_gpu_rgb2spec_opt 64 ${SPECTRA_GPU_GENERATED_DIR}/rgbspectrum_dci_p3.cpp DCI_P3
        DEPENDS spectra_gpu_rgb2spec_opt
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_GPU_GENERATED_DIR}/rgbspectrum_rec2020.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_GPU_GENERATED_DIR}
        COMMAND spectra_gpu_rgb2spec_opt 64 ${SPECTRA_GPU_GENERATED_DIR}/rgbspectrum_rec2020.cpp REC2020
        DEPENDS spectra_gpu_rgb2spec_opt
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_GPU_GENERATED_DIR}/rgbspectrum_srgb.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_GPU_GENERATED_DIR}
        COMMAND spectra_gpu_rgb2spec_opt 64 ${SPECTRA_GPU_GENERATED_DIR}/rgbspectrum_srgb.cpp sRGB
        DEPENDS spectra_gpu_rgb2spec_opt
        VERBATIM
)

set(SPECTRA_GPU_CORE_SOURCE
        spectra/pathtracer/core/bssrdf.cu
        spectra/pathtracer/core/bxdfs.cu
        spectra/pathtracer/core/cameras.cu
        spectra/pathtracer/core/film.cu
        spectra/pathtracer/core/filters.cu
        spectra/pathtracer/core/interaction.cu
        spectra/pathtracer/core/kernel_config.cu
        spectra/pathtracer/core/lights.cu
        spectra/pathtracer/core/lightsamplers.cu
        spectra/pathtracer/core/materials.cu
        spectra/pathtracer/core/media_host.cu
        spectra/pathtracer/core/media.cu
        spectra/pathtracer/core/paramdict.cu
        spectra/pathtracer/core/samplers.cu
        spectra/pathtracer/core/shapes.cu
        spectra/pathtracer/core/textures.cu
)
set(SPECTRA_GPU_UTIL_SOURCE
        spectra/pathtracer/util/bluenoise.cu
        spectra/pathtracer/util/color.cu
        spectra/pathtracer/util/colorspace.cu
        spectra/pathtracer/util/file.cpp
        spectra/pathtracer/util/image.cpp
        spectra/pathtracer/util/loopsubdiv.cu
        spectra/pathtracer/util/lowdiscrepancy.cu
        spectra/pathtracer/util/math.cu
        spectra/pathtracer/util/memory.cu
        spectra/pathtracer/util/mesh.cu
        spectra/pathtracer/util/mipmap.cu
        spectra/pathtracer/util/noise.cu
        spectra/pathtracer/util/parallel.cu
        spectra/pathtracer/util/pmj02tables.cu
        spectra/pathtracer/util/primes.cu
        spectra/pathtracer/util/pstd.cu
        spectra/pathtracer/util/sampling.cu
        spectra/pathtracer/util/scattering.cu
        spectra/pathtracer/util/sobolmatrices.cu
        spectra/pathtracer/util/spectrum.cu
        spectra/pathtracer/util/stbimage.cu
        spectra/pathtracer/util/transform.cu
        spectra/pathtracer/util/vecmath.cu
)
set(SPECTRA_GPU_DEVICE_SOURCE
        spectra/pathtracer/gpu/memory.cu
        spectra/pathtracer/gpu/util.cu
)

add_library(spectra_gpu_optix_input OBJECT spectra/pathtracer/optix/optix.cu)
set_property(TARGET spectra_gpu_optix_input PROPERTY CUDA_OPTIX_COMPILATION ON)
set_property(TARGET spectra_gpu_optix_input PROPERTY CUDA_ARCHITECTURES OFF)
target_compile_definitions(spectra_gpu_optix_input PRIVATE ${SPECTRA_GPU_DEFINITIONS} CCCL_DISABLE_INT128_SUPPORT)
target_include_directories(spectra_gpu_optix_input PRIVATE ${SPECTRA_GPU_INCLUDE_DIRS})
target_compile_options(
        spectra_gpu_optix_input
        PRIVATE
        ${SPECTRA_GPU_CUDA_COMPILE_OPTIONS}
        ${SPECTRA_GPU_CUDA_WARNING_OPTIONS}
        "$<$<COMPILE_LANGUAGE:CUDA>:--gpu-architecture=${SPECTRA_OPTIX_GPU_ARCH}>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --display_error_number>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=20044>"
)
add_dependencies(spectra_gpu_optix_input spectra_gpu_soa_generated)

set(SPECTRA_GPU_EMBEDDED_OPTIX_INPUT ${SPECTRA_GPU_GENERATED_DIR}/optix.cu.optixir_embedded.c)
add_custom_command(
        OUTPUT ${SPECTRA_GPU_EMBEDDED_OPTIX_INPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_GPU_GENERATED_DIR}
        COMMAND ${CMAKE_COMMAND}
        "-DBIN_TO_C_COMMAND=${SPECTRA_GPU_BIN2C}"
        "-DOBJECTS=$<TARGET_OBJECTS:spectra_gpu_optix_input>"
        "-DVAR_NAME=SPECTRA_EMBEDDED_OPTIX_INPUT"
        "-DOUTPUT=${SPECTRA_GPU_EMBEDDED_OPTIX_INPUT}"
        -P ${SPECTRA_GPU_BIN2C_SCRIPT}
        DEPENDS spectra_gpu_optix_input $<TARGET_OBJECTS:spectra_gpu_optix_input>
        VERBATIM
)
add_library(spectra_gpu_embedded_optix_input STATIC ${SPECTRA_GPU_EMBEDDED_OPTIX_INPUT})

add_library(spectra_util)
add_library(spectra::util ALIAS spectra_util)
target_sources(
        spectra_util
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        spectra/util/math.ixx
        PRIVATE
        spectra/util/math.cpp
)
set_property(TARGET spectra_util PROPERTY CXX_MODULE_STD ON)

add_library(spectra_scene)
add_library(spectra::scene ALIAS spectra_scene)
target_sources(
        spectra_scene
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        spectra/scene.ixx
        PRIVATE
        spectra/scene.cpp
)
target_link_libraries(spectra_scene PUBLIC spectra::util)
set_property(TARGET spectra_scene PROPERTY CXX_MODULE_STD ON)
target_compile_definitions(spectra_scene PRIVATE "SPECTRA_PROJECT_SCENE_ROOT=\"${SPECTRA_PROJECT_SCENE_ROOT}\"")

add_library(
        spectra_gpu_lib
        STATIC
        ${SPECTRA_GPU_RGB_TO_SPECTRUM_SOURCES}
        ${SPECTRA_GPU_SOA_GENERATED}
        ${SPECTRA_GPU_CORE_SOURCE}
        ${SPECTRA_GPU_UTIL_SOURCE}
        ${SPECTRA_GPU_DEVICE_SOURCE}
        ${PROJECT_SOURCE_DIR}/vendor/pbrt/lodepng/lodepng.cpp
        ${PROJECT_SOURCE_DIR}/vendor/pbrt/rply/rply.cpp
        spectra/pathtracer/compiled_scene.cpp
        spectra/pathtracer/optix/aggregate.cu
        spectra/pathtracer/integrator.cu
)
add_library(spectra::gpu_lib ALIAS spectra_gpu_lib)
add_dependencies(spectra_gpu_lib spectra_gpu_soa_generated)
target_compile_definitions(spectra_gpu_lib PUBLIC ${SPECTRA_GPU_DEFINITIONS})
target_compile_definitions(spectra_gpu_lib PRIVATE "SPECTRA_PROJECT_SCENE_ROOT=\"${SPECTRA_PROJECT_SCENE_ROOT}\"")
target_include_directories(spectra_gpu_lib PUBLIC ${SPECTRA_GPU_INCLUDE_DIRS})
target_compile_options(
        spectra_gpu_lib
        PUBLIC
        "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-ffp-contract=off>"
        ${SPECTRA_GPU_CUDA_COMPILE_OPTIONS}
        ${SPECTRA_GPU_CUDA_WARNING_OPTIONS}
)
target_link_libraries(
        spectra_gpu_lib
        PUBLIC
        spectra::scene
        PRIVATE
        OpenEXR::OpenEXR
        Ptex_static
        ZLIB::ZLIB
        CUDA::cudart
        CUDA::cuda_driver
        Threads::Threads
        spectra_gpu_embedded_optix_input
)
set_property(TARGET spectra_gpu_lib PROPERTY CXX_MODULE_STD ON)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(spectra_gpu_lib PRIVATE dbghelp shell32 wsock32 ws2_32)
endif ()
set_property(TARGET spectra_gpu_lib PROPERTY CUDA_SEPARABLE_COMPILATION ON)
set_property(TARGET spectra_gpu_lib PROPERTY CUDA_RESOLVE_DEVICE_SYMBOLS ON)

add_executable(spectra_gpu_exe spectra_gpu.cpp)
add_executable(spectra::gpu ALIAS spectra_gpu_exe)
target_link_libraries(spectra_gpu_exe PRIVATE spectra::scene spectra_gpu_lib Threads::Threads)
set_property(TARGET spectra_gpu_exe PROPERTY CXX_MODULE_STD ON)
set_target_properties(spectra_gpu_exe PROPERTIES OUTPUT_NAME spectra_gpu)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(spectra_gpu_exe PRIVATE -rdynamic "LINKER:--no-as-needed")
endif ()


# ============================================================================
# Spectra
# ============================================================================
add_library(spectra_imgui)
add_library(spectra::imgui ALIAS spectra_imgui)
target_sources(
        spectra_imgui
        PRIVATE
        vendor/imgui/imgui.cpp
        vendor/imgui/imgui_draw.cpp
        vendor/imgui/imgui_tables.cpp
        vendor/imgui/imgui_widgets.cpp
        vendor/imgui/backends/imgui_impl_glfw.cpp
        vendor/imgui/backends/imgui_impl_vulkan.cpp
)
target_link_libraries(spectra_imgui PUBLIC Vulkan::Vulkan PRIVATE glfw)
target_include_directories(spectra_imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui)

add_library(spectra_core)
add_library(spectra::core ALIAS spectra_core)
target_sources(
        spectra_core
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        spectra/spectra.ixx
        PRIVATE
        spectra/spectra.cpp
)
target_link_libraries(spectra_core PUBLIC Vulkan::Vulkan PRIVATE glfw spectra::imgui)
set_property(TARGET spectra_core PROPERTY CXX_MODULE_STD ON)
target_compile_definitions(spectra_core PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_include_directories(spectra_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

add_library(spectra_pathtracer)
add_library(spectra::pathtracer ALIAS spectra_pathtracer)
target_sources(
        spectra_pathtracer
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        spectra/pathtracer.ixx
        PRIVATE
        spectra/pathtracer.cpp
)
target_link_libraries(spectra_pathtracer PUBLIC Vulkan::Vulkan PRIVATE CUDA::cudart spectra::scene spectra::gpu_lib spectra::imgui)
set_property(TARGET spectra_pathtracer PROPERTY CXX_MODULE_STD ON)
target_compile_definitions(spectra_pathtracer PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_include_directories(spectra_pathtracer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor)


# ============================================================================
# Application
# ============================================================================
add_executable(spectra main.cpp)
target_link_libraries(spectra PRIVATE spectra::core spectra::pathtracer)
set_property(TARGET spectra PROPERTY CXX_MODULE_STD ON)
