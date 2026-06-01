cmake_minimum_required(VERSION 4.3.0)
project(spectra VERSION 1.0.0 LANGUAGES C CXX CUDA)


# ============================================================================
# C++ Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


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
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt/CMakeLists.txt")
    message(FATAL_ERROR "Expected local renderer dependencies at '${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt'.")
endif ()

find_package(Vulkan 1.4 REQUIRED)
find_package(Threads REQUIRED)
find_package(CUDAToolkit REQUIRED)
if (CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 13.0)
    message(FATAL_ERROR "spectra requires CUDA compiler 13.0 or newer; found ${CMAKE_CUDA_COMPILER_VERSION}.")
endif ()
if (CUDAToolkit_VERSION VERSION_LESS 13.0)
    message(FATAL_ERROR "spectra requires CUDA Toolkit 13.0 or newer; found ${CUDAToolkit_VERSION}.")
endif ()

set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/glfw)
if (NOT TARGET glfw)
    message(FATAL_ERROR "Top-level GLFW dependency did not define the glfw target.")
endif ()

add_subdirectory(vendor/pbrt)


# ============================================================================
# Spectra GPU
# ============================================================================
option(SPECTRA_FLOAT_AS_DOUBLE "Use 64-bit floats for spectra_gpu" OFF)
option(SPECTRA_NVTX "Insert NVTX annotations for spectra_gpu" OFF)
set(SPECTRA_OPTIX_PATH "" CACHE PATH "Path to OptiX 9.1 SDK for spectra_gpu")
set(SPECTRA_OPTIX_GPU_ARCH "sm_80" CACHE STRING "CUDA architecture used when compiling spectra OptiX-IR")
set(SPECTRA_OPTIX_CACHE_DIR "${CMAKE_CURRENT_BINARY_DIR}/optix-cache" CACHE PATH "OptiX disk cache directory for spectra_gpu")
set(SPECTRA_OPTIX_CACHE_SIZE_MB 512 CACHE STRING "OptiX disk cache high-water size in MiB for spectra_gpu")

if (SPECTRA_OPTIX_PATH STREQUAL "")
    message(FATAL_ERROR "SPECTRA_OPTIX_PATH must point to the OptiX 9.1 SDK root for spectra_gpu.")
endif ()
if (NOT EXISTS "${SPECTRA_OPTIX_PATH}/include/optix.h")
    message(FATAL_ERROR "SPECTRA_OPTIX_PATH='${SPECTRA_OPTIX_PATH}' does not contain include/optix.h.")
endif ()
if (NOT SPECTRA_OPTIX_GPU_ARCH MATCHES "^sm_[0-9]+$")
    message(FATAL_ERROR "SPECTRA_OPTIX_GPU_ARCH='${SPECTRA_OPTIX_GPU_ARCH}' must use the form sm_XX.")
endif ()
if (NOT SPECTRA_OPTIX_CACHE_SIZE_MB MATCHES "^[0-9]+$")
    message(FATAL_ERROR "SPECTRA_OPTIX_CACHE_SIZE_MB='${SPECTRA_OPTIX_CACHE_SIZE_MB}' must be a positive integer.")
endif ()
if (SPECTRA_OPTIX_CACHE_SIZE_MB LESS 1)
    message(FATAL_ERROR "SPECTRA_OPTIX_CACHE_SIZE_MB must be greater than zero.")
endif ()
file(READ "${SPECTRA_OPTIX_PATH}/include/optix.h" SPECTRA_OPTIX_HEADER)
string(REGEX MATCH "#define[ \t]+OPTIX_VERSION[ \t]+([0-9]+)" SPECTRA_OPTIX_VERSION_MATCH "${SPECTRA_OPTIX_HEADER}")
if (NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "Could not read OPTIX_VERSION from '${SPECTRA_OPTIX_PATH}/include/optix.h'.")
endif ()
set(SPECTRA_OPTIX_VERSION "${CMAKE_MATCH_1}")
if (SPECTRA_OPTIX_VERSION LESS 90100)
    message(FATAL_ERROR "spectra_gpu requires OptiX SDK 9.1.0 or newer; found OPTIX_VERSION=${SPECTRA_OPTIX_VERSION}.")
endif ()
math(EXPR SPECTRA_OPTIX_CACHE_SIZE_BYTES "${SPECTRA_OPTIX_CACHE_SIZE_MB} * 1024 * 1024")

set(SPECTRA_PROJECT_SCENE_ROOT "${PROJECT_SOURCE_DIR}/scene")
if (NOT EXISTS "${SPECTRA_PROJECT_SCENE_ROOT}/pbrt-book/book.pbrt")
    message(FATAL_ERROR "Default Spectra book scene is missing: ${SPECTRA_PROJECT_SCENE_ROOT}/pbrt-book/book.pbrt")
endif ()
if (NOT EXISTS "${SPECTRA_PROJECT_SCENE_ROOT}/explosion/explosion.pbrt")
    message(FATAL_ERROR "Default Spectra explosion scene is missing: ${SPECTRA_PROJECT_SCENE_ROOT}/explosion/explosion.pbrt")
endif ()
if (NOT EXISTS "${SPECTRA_PROJECT_SCENE_ROOT}/explosion/fire.nvdb")
    message(FATAL_ERROR "Default Spectra explosion volume is missing: ${SPECTRA_PROJECT_SCENE_ROOT}/explosion/fire.nvdb")
endif ()
if (NOT EXISTS "${SPECTRA_PROJECT_SCENE_ROOT}/explosion/textures/sky.exr")
    message(FATAL_ERROR "Default Spectra explosion sky texture is missing: ${SPECTRA_PROJECT_SCENE_ROOT}/explosion/textures/sky.exr")
endif ()
set(SPECTRA_GPU_DEFINITIONS
        PTEX_STATIC
        "$<$<COMPILE_LANGUAGE:CXX>:CUDA_NO_HALF>"
        "$<$<CONFIG:Debug>:SPECTRA_DEBUG_BUILD>"
)
if (SPECTRA_FLOAT_AS_DOUBLE)
    list(APPEND SPECTRA_GPU_DEFINITIONS SPECTRA_FLOAT_AS_DOUBLE)
endif ()
if (SPECTRA_NVTX)
    list(APPEND SPECTRA_GPU_DEFINITIONS NVTX)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if (NOT MSVC)
        message(FATAL_ERROR "Windows builds require MSVC.")
    endif ()
    list(APPEND SPECTRA_GPU_DEFINITIONS
            SPECTRA_IS_WINDOWS
            SPECTRA_IS_MSVC
            UNICODE
            _UNICODE
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
        "$<$<AND:$<COMPILE_LANGUAGE:CUDA>,$<CONFIG:Debug>,$<NOT:$<BOOL:$<TARGET_PROPERTY:CUDA_PTX_COMPILATION>>>,$<NOT:$<BOOL:$<TARGET_PROPERTY:CUDA_OPTIX_COMPILATION>>>>:-G;-g;-lineinfo;-maxrregcount;128>"
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
        "    inline constexpr unsigned int SpectraOptiXSdkVersion = ${SPECTRA_OPTIX_VERSION};\n"
        "    inline constexpr char SpectraOptiXCacheDir[] = \"${SPECTRA_OPTIX_CACHE_DIR_CONFIG}\";\n"
        "    inline constexpr std::size_t SpectraOptiXCacheSizeBytes = ${SPECTRA_OPTIX_CACHE_SIZE_BYTES}ull;\n"
        "} // namespace spectra::optix\n")
set(SPECTRA_GPU_INCLUDE_DIRS
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/vendor/pbrt
        ${STB_INCLUDE}
        ${QOI_INCLUDE}
        ${ZLIB_INCLUDE_DIRS}
        ${LIBDEFLATE_INCLUDE_DIRS}
        ${PTEX_INCLUDE}
        ${DOUBLE_CONVERSION_INCLUDE}
        ${NANOVDB_INCLUDE}
        ${PROJECT_BINARY_DIR}
        ${SPECTRA_GPU_GENERATED_DIR}
        ${SPECTRA_OPTIX_PATH}/include
        ${CUDAToolkit_INCLUDE_DIRS}
        ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
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

add_executable(spectra_gpu_soac spectra/pathtracer/cmd/soac.cpp)
target_compile_definitions(spectra_gpu_soac PRIVATE ${SPECTRA_GPU_DEFINITIONS})
target_include_directories(spectra_gpu_soac PRIVATE ${SPECTRA_GPU_INCLUDE_DIRS})

add_custom_command(
        OUTPUT ${SPECTRA_GPU_GENERATED_DIR}/spectra_soa.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_GPU_GENERATED_DIR}
        COMMAND spectra_gpu_soac ${CMAKE_CURRENT_SOURCE_DIR}/spectra/pathtracer/core/spectra.soa > ${SPECTRA_GPU_GENERATED_DIR}/spectra_soa.h
        DEPENDS spectra_gpu_soac ${CMAKE_CURRENT_SOURCE_DIR}/spectra/pathtracer/core/spectra.soa
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_GPU_GENERATED_DIR}/spectra_wavefront_workitems_soa.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_GPU_GENERATED_DIR}
        COMMAND spectra_gpu_soac ${CMAKE_CURRENT_SOURCE_DIR}/spectra/pathtracer/wavefront/workitems.soa > ${SPECTRA_GPU_GENERATED_DIR}/spectra_wavefront_workitems_soa.h
        DEPENDS spectra_gpu_soac ${CMAKE_CURRENT_SOURCE_DIR}/spectra/pathtracer/wavefront/workitems.soa
        VERBATIM
)
set(SPECTRA_GPU_SOA_GENERATED
        ${SPECTRA_GPU_GENERATED_DIR}/spectra_soa.h
        ${SPECTRA_GPU_GENERATED_DIR}/spectra_wavefront_workitems_soa.h
)
add_custom_target(spectra_gpu_soa_generated DEPENDS ${SPECTRA_GPU_SOA_GENERATED})

add_executable(spectra_gpu_rgb2spec_opt spectra/pathtracer/cmd/rgb2spec_opt.cpp)
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
        spectra/pathtracer/core/lights.cu
        spectra/pathtracer/core/lightsamplers.cu
        spectra/pathtracer/core/materials.cu
        spectra/pathtracer/core/media.cpp
        spectra/pathtracer/core/media.cu
        spectra/pathtracer/core/options.cu
        spectra/pathtracer/core/paramdict.cpp
        spectra/pathtracer/core/samplers.cu
        spectra/pathtracer/core/shapes.cu
        spectra/pathtracer/core/textures.cu
)
set(SPECTRA_GPU_UTIL_SOURCE
        spectra/pathtracer/util/bluenoise.cu
        spectra/pathtracer/util/buffercache.cpp
        spectra/pathtracer/util/color.cu
        spectra/pathtracer/util/colorspace.cu
        spectra/pathtracer/util/file.cpp
        spectra/pathtracer/util/image.cpp
        spectra/pathtracer/util/loopsubdiv.cpp
        spectra/pathtracer/util/lowdiscrepancy.cu
        spectra/pathtracer/util/math.cu
        spectra/pathtracer/util/memory.cpp
        spectra/pathtracer/util/mesh.cu
        spectra/pathtracer/util/mipmap.cpp
        spectra/pathtracer/util/noise.cu
        spectra/pathtracer/util/parallel.cpp
        spectra/pathtracer/util/pmj02tables.cu
        spectra/pathtracer/util/primes.cu
        spectra/pathtracer/util/pstd.cu
        spectra/pathtracer/util/sampling.cu
        spectra/pathtracer/util/scattering.cu
        spectra/pathtracer/util/sobolmatrices.cu
        spectra/pathtracer/util/spectrum.cu
        spectra/pathtracer/util/stbimage.cpp
        spectra/pathtracer/util/string.cpp
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
        spectra/scene.cpp
        spectra/pathtracer/wavefront_scene.cpp
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
        PRIVATE
        OpenEXR::OpenEXR
        Ptex_static
        ZLIB::ZLIB
        deflate::deflate
        utf8proc
        CUDA::cudart
        CUDA::cuda_driver
        Threads::Threads
        spectra_gpu_embedded_optix_input
)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(spectra_gpu_lib PRIVATE dbghelp shell32 wsock32 ws2_32)
endif ()
set_property(TARGET spectra_gpu_lib PROPERTY CUDA_SEPARABLE_COMPILATION ON)
set_property(TARGET spectra_gpu_lib PROPERTY CUDA_RESOLVE_DEVICE_SYMBOLS ON)

add_executable(spectra_gpu_exe spectra/pathtracer/cmd/spectra_gpu.cpp)
add_executable(spectra::gpu ALIAS spectra_gpu_exe)
target_link_libraries(spectra_gpu_exe PRIVATE spectra_gpu_lib Threads::Threads)
set_target_properties(spectra_gpu_exe PROPERTIES OUTPUT_NAME spectra_gpu)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(spectra_gpu_exe PRIVATE -rdynamic "LINKER:--no-as-needed")
endif ()


# ============================================================================
# Spectra
# ============================================================================
add_library(spectra_core)
target_sources(
        spectra_core
        PRIVATE
        spectra/pathtracer.h
        spectra/pathtracer.cpp
        spectra/pathtracer/integrator.h
        spectra/spectra.h
        spectra/spectra.cpp

        vendor/imgui/imgui.cpp
        vendor/imgui/imgui_draw.cpp
        vendor/imgui/imgui_tables.cpp
        vendor/imgui/imgui_widgets.cpp
        vendor/imgui/backends/imgui_impl_glfw.cpp
        vendor/imgui/backends/imgui_impl_vulkan.cpp
)
target_link_libraries(spectra_core PUBLIC Vulkan::Vulkan PRIVATE glfw CUDA::cudart spectra::gpu_lib)
target_compile_definitions(
        spectra_core
        PRIVATE
        IMGUI_DEFINE_MATH_OPERATORS
)
target_include_directories(
        spectra_core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw/include
)


# ============================================================================
# Application
# ============================================================================
add_executable(spectra main.cpp)
target_link_libraries(spectra PRIVATE spectra_core spectra::gpu_lib)
