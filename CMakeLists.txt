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


# ============================================================================
# Build Configuration
# ============================================================================
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type" FORCE)
endif ()

set(SPECTRA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SPECTRA_BINARY_ROOT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(SPECTRA_SCENES_ROOT "${SPECTRA_ROOT_DIR}/scenes")


# ============================================================================
# Dependencies
# ============================================================================
find_package(Vulkan 1.4 REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory(vendor)


# ============================================================================
# Spectra Core
# ============================================================================
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
target_link_libraries(spectra_core PUBLIC Vulkan::Vulkan PRIVATE glfw vendor::imgui)
target_include_directories(spectra_core PUBLIC ${SPECTRA_ROOT_DIR}/vendor/imgui PRIVATE ${SPECTRA_ROOT_DIR}/vendor)
set_property(TARGET spectra_core PROPERTY CXX_MODULE_STD ON)


# ============================================================================
# Scene
# ============================================================================
add_library(spectra_scene)
add_library(spectra::scene ALIAS spectra_scene)
target_sources(
        spectra_scene
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        scene/math.ixx
        scene/scene.ixx
        PRIVATE
        scene/scene.cpp
)
target_compile_definitions(spectra_scene PRIVATE "SPECTRA_SCENES_ROOT=\"${SPECTRA_SCENES_ROOT}\"")
target_link_libraries(spectra_scene PRIVATE ZLIB::ZLIB vendor::lodepng)
set_property(TARGET spectra_scene PROPERTY CXX_MODULE_STD ON)


# ============================================================================
# Pathtracer Configuration
# ============================================================================
option(SPECTRA_FLOAT_AS_DOUBLE "Use 64-bit floats for the Spectra pathtracer" OFF)
set(SPECTRA_OPTIX_PATH "" CACHE PATH "Path to OptiX SDK for the Spectra pathtracer")
set(SPECTRA_OPTIX_GPU_ARCH "" CACHE STRING "CUDA architecture used when compiling spectra OptiX PTX")
set(SPECTRA_OPTIX_CACHE_DIR "${SPECTRA_BINARY_ROOT_DIR}/optix-cache" CACHE PATH "OptiX disk cache directory for the Spectra pathtracer")
set(SPECTRA_OPTIX_CACHE_SIZE_MB 512 CACHE STRING "OptiX disk cache high-water size in MiB for the Spectra pathtracer")

find_package(CUDAToolkit REQUIRED)
if (CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 13.0)
    message(FATAL_ERROR "spectra requires CUDA compiler 13.0 or newer; found ${CMAKE_CUDA_COMPILER_VERSION}.")
endif ()
if (CUDAToolkit_VERSION VERSION_LESS 13.0)
    message(FATAL_ERROR "spectra requires CUDA Toolkit 13.0 or newer; found ${CUDAToolkit_VERSION}.")
endif ()

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
        set(SPECTRA_OPTIX_PATH "${SPECTRA_OPTIX_PATH}" CACHE PATH "Path to OptiX SDK for the Spectra pathtracer" FORCE)
    endif ()
endif ()
if (SPECTRA_OPTIX_PATH STREQUAL "")
    message(FATAL_ERROR "SPECTRA_OPTIX_PATH must point to an OptiX SDK root; no default installation was found.")
endif ()
if (SPECTRA_OPTIX_GPU_ARCH STREQUAL "")
    list(GET CMAKE_CUDA_ARCHITECTURES 0 SPECTRA_OPTIX_DERIVED_GPU_ARCH)
    if (SPECTRA_OPTIX_DERIVED_GPU_ARCH MATCHES "^([0-9]+)")
        set(SPECTRA_OPTIX_GPU_ARCH "sm_${CMAKE_MATCH_1}")
        if (CMAKE_MATCH_1 GREATER_EQUAL 100)
            set(SPECTRA_OPTIX_GPU_ARCH "sm_80")
        endif ()
    else ()
        message(FATAL_ERROR "Unable to derive SPECTRA_OPTIX_GPU_ARCH from CMAKE_CUDA_ARCHITECTURES='${CMAKE_CUDA_ARCHITECTURES}'.")
    endif ()
endif ()
message(STATUS "Spectra OptiX PTX CUDA Architecture: ${SPECTRA_OPTIX_GPU_ARCH}")
math(EXPR SPECTRA_OPTIX_CACHE_SIZE_BYTES "${SPECTRA_OPTIX_CACHE_SIZE_MB} * 1024 * 1024")

set(SPECTRA_PATHTRACER_DEFINITIONS
        PTEX_STATIC
)
if (SPECTRA_FLOAT_AS_DOUBLE)
    list(APPEND SPECTRA_PATHTRACER_DEFINITIONS SPECTRA_FLOAT_AS_DOUBLE)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if (NOT MSVC)
        message(FATAL_ERROR "Windows builds require MSVC.")
    endif ()
    list(APPEND SPECTRA_PATHTRACER_DEFINITIONS
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
    list(APPEND SPECTRA_PATHTRACER_DEFINITIONS
            SPECTRA_IS_LINUX
            "SPECTRA_NOINLINE=__attribute__((noinline))"
            "SPECTRA_RESTRICT=__restrict__"
            SPECTRA_HAVE_POSIX_MEMALIGN
    )
else ()
    message(FATAL_ERROR "Only Linux/WSL and Windows/MSVC are supported by the Spectra pathtracer.")
endif ()

set(SPECTRA_PATHTRACER_CUDA_WARNING_OPTIONS
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
set(SPECTRA_PATHTRACER_CUDA_COMPILE_OPTIONS
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:--forward-slash-prefix-opts>>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--extended-lambda>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--forward-unknown-to-host-compiler>"
)


# ============================================================================
# Pathtracer Generated Sources
# ============================================================================
get_filename_component(SPECTRA_PATHTRACER_CUDA_COMPILER_BIN "${CMAKE_CUDA_COMPILER}" DIRECTORY)
find_program(
        SPECTRA_PATHTRACER_BIN2C
        NAMES bin2c
        PATHS ${SPECTRA_PATHTRACER_CUDA_COMPILER_BIN} ${CUDAToolkit_BIN_DIR}
        NO_DEFAULT_PATH
)
if (NOT SPECTRA_PATHTRACER_BIN2C)
    message(FATAL_ERROR "CUDA bin2c was not found next to '${CMAKE_CUDA_COMPILER}'.")
endif ()

set(SPECTRA_PATHTRACER_SOURCE_DIR ${SPECTRA_ROOT_DIR}/pathtracer)
set(SPECTRA_PATHTRACER_GENERATED_DIR ${SPECTRA_BINARY_ROOT_DIR}/pathtracer/generated/spectra_pathtracer)
file(MAKE_DIRECTORY "${SPECTRA_PATHTRACER_GENERATED_DIR}")
file(MAKE_DIRECTORY "${SPECTRA_OPTIX_CACHE_DIR}")
string(REPLACE "\\" "/" SPECTRA_OPTIX_CACHE_DIR_CONFIG "${SPECTRA_OPTIX_CACHE_DIR}")
string(REPLACE "\"" "\\\"" SPECTRA_OPTIX_CACHE_DIR_CONFIG "${SPECTRA_OPTIX_CACHE_DIR_CONFIG}")
file(WRITE "${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_optix_config.h"
        "#pragma once\n"
        "\n"
        "#include <cstddef>\n"
        "\n"
        "namespace spectra::optix\n"
        "{\n"
        "    inline constexpr char SpectraOptiXCacheDir[] = \"${SPECTRA_OPTIX_CACHE_DIR_CONFIG}\";\n"
        "    inline constexpr std::size_t SpectraOptiXCacheSizeBytes = ${SPECTRA_OPTIX_CACHE_SIZE_BYTES}ull;\n"
        "} // namespace spectra::optix\n")

set(SPECTRA_PATHTRACER_INCLUDE_DIRS
        ${SPECTRA_ROOT_DIR}
        ${SPECTRA_ROOT_DIR}/vendor
        ${STB_INCLUDE}
        ${QOI_INCLUDE}
        ${ZLIB_INCLUDE_DIRS}
        ${PTEX_INCLUDE}
        ${NANOVDB_INCLUDE}
        ${SPECTRA_BINARY_ROOT_DIR}
        ${SPECTRA_PATHTRACER_GENERATED_DIR}
        ${SPECTRA_OPTIX_PATH}/include
        ${CUDAToolkit_INCLUDE_DIRS}
)

set(SPECTRA_PATHTRACER_BIN2C_SCRIPT ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_pathtracer_bin2c_embed.cmake)
file(WRITE "${SPECTRA_PATHTRACER_BIN2C_SCRIPT}" [=[
set(file_contents)
foreach (obj IN LISTS OBJECTS)
    get_filename_component(obj_ext "${obj}" EXT)
    get_filename_component(obj_dir "${obj}" DIRECTORY)
    if (obj_ext MATCHES "\\.(optixir|ptx)$")
        execute_process(
                COMMAND "${BIN_TO_C_COMMAND}" --name "${VAR_NAME}" "${obj}"
                WORKING_DIRECTORY "${obj_dir}"
                RESULT_VARIABLE result
                OUTPUT_VARIABLE output
                ERROR_VARIABLE error_var
        )
        if (NOT result EQUAL 0)
            message(FATAL_ERROR "bin2c failed for ${obj}: ${error_var}")
        endif ()
        file(SIZE "${obj}" obj_size)
        string(APPEND file_contents " \n${output}\nunsigned long long ${VAR_NAME}_SIZE = ${obj_size}ull;\n")
    endif ()
endforeach ()
if (file_contents STREQUAL "")
    message(FATAL_ERROR "No OptiX input was passed to bin2c: ${OBJECTS}")
endif ()
file(WRITE "${OUTPUT}" "${file_contents}")
]=])

add_executable(spectra_pathtracer_soac pathtracer/tools/soac.cpp)
target_compile_definitions(spectra_pathtracer_soac PRIVATE ${SPECTRA_PATHTRACER_DEFINITIONS})
target_include_directories(spectra_pathtracer_soac PRIVATE ${SPECTRA_PATHTRACER_INCLUDE_DIRS})

add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_soa.cuh
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND spectra_pathtracer_soac ${SPECTRA_PATHTRACER_SOURCE_DIR}/core/spectra.soa > ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_soa.cuh
        DEPENDS spectra_pathtracer_soac ${SPECTRA_PATHTRACER_SOURCE_DIR}/core/spectra.soa
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_wavefront_workitems_soa.cuh
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND spectra_pathtracer_soac ${SPECTRA_PATHTRACER_SOURCE_DIR}/wavefront/workitems.soa > ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_wavefront_workitems_soa.cuh
        DEPENDS spectra_pathtracer_soac ${SPECTRA_PATHTRACER_SOURCE_DIR}/wavefront/workitems.soa
        VERBATIM
)
set(SPECTRA_PATHTRACER_SOA_GENERATED
        ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_soa.cuh
        ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_wavefront_workitems_soa.cuh
)
add_custom_target(spectra_pathtracer_soa_generated DEPENDS ${SPECTRA_PATHTRACER_SOA_GENERATED})

add_executable(spectra_pathtracer_rgb2spec_opt pathtracer/tools/rgb2spec_opt.cpp)
target_compile_definitions(spectra_pathtracer_rgb2spec_opt PRIVATE ${SPECTRA_PATHTRACER_DEFINITIONS})
target_include_directories(spectra_pathtracer_rgb2spec_opt PRIVATE ${SPECTRA_PATHTRACER_INCLUDE_DIRS})
target_link_libraries(spectra_pathtracer_rgb2spec_opt PRIVATE Threads::Threads)

set(SPECTRA_PATHTRACER_RGB_TO_SPECTRUM_SOURCES
        ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_aces.cpp
        ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_dci_p3.cpp
        ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_rec2020.cpp
        ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_srgb.cpp
)
add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_aces.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND spectra_pathtracer_rgb2spec_opt 64 ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_aces.cpp ACES2065_1
        DEPENDS spectra_pathtracer_rgb2spec_opt
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_dci_p3.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND spectra_pathtracer_rgb2spec_opt 64 ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_dci_p3.cpp DCI_P3
        DEPENDS spectra_pathtracer_rgb2spec_opt
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_rec2020.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND spectra_pathtracer_rgb2spec_opt 64 ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_rec2020.cpp REC2020
        DEPENDS spectra_pathtracer_rgb2spec_opt
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_srgb.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND spectra_pathtracer_rgb2spec_opt 64 ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_srgb.cpp sRGB
        DEPENDS spectra_pathtracer_rgb2spec_opt
        VERBATIM
)


# ============================================================================
# Pathtracer Source Lists
# ============================================================================
set(SPECTRA_PATHTRACER_CORE_SOURCE
        pathtracer/core/bssrdf.cu
        pathtracer/core/bxdfs.cu
        pathtracer/core/cameras.cu
        pathtracer/core/film.cu
        pathtracer/core/filters.cu
        pathtracer/core/interaction.cu
        pathtracer/core/kernel_config.cu
        pathtracer/core/lights.cu
        pathtracer/core/lightsamplers.cu
        pathtracer/core/materials.cu
        pathtracer/core/media_host.cu
        pathtracer/core/media.cu
        pathtracer/core/paramdict.cu
        pathtracer/core/samplers.cu
        pathtracer/core/shapes.cu
        pathtracer/core/textures.cu
)
set(SPECTRA_PATHTRACER_UTIL_SOURCE
        pathtracer/util/bluenoise.cu
        pathtracer/util/color.cu
        pathtracer/util/colorspace.cu
        pathtracer/util/file.cpp
        pathtracer/util/image.cpp
        pathtracer/util/loopsubdiv.cu
        pathtracer/util/lowdiscrepancy.cu
        pathtracer/util/math.cu
        pathtracer/util/memory.cu
        pathtracer/util/mesh.cu
        pathtracer/util/mipmap.cu
        pathtracer/util/noise.cu
        pathtracer/util/parallel.cu
        pathtracer/util/pmj02tables.cu
        pathtracer/util/primes.cu
        pathtracer/util/pstd.cu
        pathtracer/util/sampling.cu
        pathtracer/util/scattering.cu
        pathtracer/util/sobolmatrices.cu
        pathtracer/util/spectrum.cu
        pathtracer/util/stbimage.cu
        pathtracer/util/transform.cu
        pathtracer/util/vecmath.cu
)
set(SPECTRA_PATHTRACER_DEVICE_SOURCE
        pathtracer/gpu/util.cu
        pathtracer/memory/memory.cu
)

add_library(spectra_pathtracer_optix_input OBJECT pathtracer/optix/optix.cu)
set_property(TARGET spectra_pathtracer_optix_input PROPERTY CUDA_PTX_COMPILATION ON)
set_property(TARGET spectra_pathtracer_optix_input PROPERTY CUDA_ARCHITECTURES OFF)
target_compile_definitions(spectra_pathtracer_optix_input PRIVATE ${SPECTRA_PATHTRACER_DEFINITIONS} CCCL_DISABLE_INT128_SUPPORT)
target_include_directories(spectra_pathtracer_optix_input PRIVATE ${SPECTRA_PATHTRACER_INCLUDE_DIRS})
target_compile_options(
        spectra_pathtracer_optix_input
        PRIVATE
        ${SPECTRA_PATHTRACER_CUDA_COMPILE_OPTIONS}
        ${SPECTRA_PATHTRACER_CUDA_WARNING_OPTIONS}
        "$<$<COMPILE_LANGUAGE:CUDA>:--gpu-architecture=${SPECTRA_OPTIX_GPU_ARCH}>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --display_error_number>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=20044>"
)
add_dependencies(spectra_pathtracer_optix_input spectra_pathtracer_soa_generated)


# ============================================================================
# Pathtracer Targets
# ============================================================================
set(SPECTRA_PATHTRACER_EMBEDDED_OPTIX_INPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/optix.cu.ptx_embedded.c)
add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_EMBEDDED_OPTIX_INPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND ${CMAKE_COMMAND}
        "-DBIN_TO_C_COMMAND=${SPECTRA_PATHTRACER_BIN2C}"
        "-DOBJECTS=$<TARGET_OBJECTS:spectra_pathtracer_optix_input>"
        "-DVAR_NAME=SPECTRA_EMBEDDED_OPTIX_INPUT"
        "-DOUTPUT=${SPECTRA_PATHTRACER_EMBEDDED_OPTIX_INPUT}"
        -P ${SPECTRA_PATHTRACER_BIN2C_SCRIPT}
        DEPENDS spectra_pathtracer_optix_input $<TARGET_OBJECTS:spectra_pathtracer_optix_input>
        VERBATIM
)
add_library(spectra_pathtracer_embedded_optix_input STATIC ${SPECTRA_PATHTRACER_EMBEDDED_OPTIX_INPUT})

add_library(spectra_pathtracer)
add_library(spectra::pathtracer ALIAS spectra_pathtracer)
target_sources(
        spectra_pathtracer
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        pathtracer/host.ixx
        pathtracer/renderer.ixx
        PRIVATE
        ${SPECTRA_PATHTRACER_RGB_TO_SPECTRUM_SOURCES}
        ${SPECTRA_PATHTRACER_SOA_GENERATED}
        ${SPECTRA_PATHTRACER_CORE_SOURCE}
        ${SPECTRA_PATHTRACER_UTIL_SOURCE}
        ${SPECTRA_PATHTRACER_DEVICE_SOURCE}
        pathtracer/renderer.cpp
        pathtracer/compiled_scene.cpp
        pathtracer/optix/aggregate.cu
        pathtracer/integrator.cu
)
add_dependencies(spectra_pathtracer spectra_pathtracer_soa_generated)
target_compile_definitions(spectra_pathtracer PUBLIC ${SPECTRA_PATHTRACER_DEFINITIONS})
target_include_directories(spectra_pathtracer PUBLIC ${SPECTRA_PATHTRACER_INCLUDE_DIRS})
target_compile_options(
        spectra_pathtracer
        PUBLIC
        "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-ffp-contract=off>"
        ${SPECTRA_PATHTRACER_CUDA_COMPILE_OPTIONS}
        ${SPECTRA_PATHTRACER_CUDA_WARNING_OPTIONS}
)
target_link_libraries(
        spectra_pathtracer
        PUBLIC
        Vulkan::Vulkan
        vendor::imgui
        spectra::scene
        PRIVATE
        vendor::lodepng
        vendor::rply
        OpenEXR::OpenEXR
        Ptex_static
        ZLIB::ZLIB
        CUDA::cudart
        CUDA::cuda_driver
        Threads::Threads
        spectra_pathtracer_embedded_optix_input
)
set_property(TARGET spectra_pathtracer PROPERTY CXX_MODULE_STD ON)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(spectra_pathtracer PRIVATE dbghelp shell32 wsock32 ws2_32)
endif ()
set_property(TARGET spectra_pathtracer PROPERTY CUDA_SEPARABLE_COMPILATION ON)
set_property(TARGET spectra_pathtracer PROPERTY CUDA_RESOLVE_DEVICE_SYMBOLS ON)

add_executable(spectra_pathtracer_cli pathtracer/pathtracer_cli.cpp)
add_executable(spectra::pathtracer_cli ALIAS spectra_pathtracer_cli)
target_link_libraries(spectra_pathtracer_cli PRIVATE spectra::pathtracer Threads::Threads)
set_property(TARGET spectra_pathtracer_cli PROPERTY CXX_MODULE_STD ON)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(spectra_pathtracer_cli PRIVATE -rdynamic "LINKER:--no-as-needed")
endif ()


# ============================================================================
# Rasterizer
# ============================================================================
add_library(spectra_rasterizer)
add_library(spectra::rasterizer ALIAS spectra_rasterizer)

find_program(SLANGC_EXECUTABLE NAMES slangc REQUIRED)
set(SPECTRA_RASTERIZER_SOURCE_DIR ${SPECTRA_ROOT_DIR}/rasterizer)
set(SPECTRA_RASTERIZER_GENERATED_DIR ${SPECTRA_BINARY_ROOT_DIR}/rasterizer/generated)

file(GLOB SPECTRA_RASTERIZER_SHADER_FILES CONFIGURE_DEPENDS "${SPECTRA_RASTERIZER_SOURCE_DIR}/shaders/*.slang")
if (NOT SPECTRA_RASTERIZER_SHADER_FILES)
    message(FATAL_ERROR "Spectra rasterizer requires at least one Slang shader in ${SPECTRA_RASTERIZER_SOURCE_DIR}/shaders")
endif ()

function(spectra_rasterizer_shader_stage output_variable shader_file shader_name stage)
    set(entry "${stage}_main")
    set(embed_name "spectra_rasterizer_${shader_name}_${stage}_spv")
    set(output_file "${SPECTRA_RASTERIZER_GENERATED_DIR}/spectra_rasterizer_${shader_name}_${stage}_spv.h")
    add_custom_command(
            OUTPUT "${output_file}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPECTRA_RASTERIZER_GENERATED_DIR}"
            COMMAND "${SLANGC_EXECUTABLE}" "${shader_file}" -target spirv -profile glsl_460 -entry "${entry}" -stage "${stage}" -matrix-layout-row-major -source-embed-style u32 -source-embed-name "${embed_name}" -o "${output_file}"
            DEPENDS "${shader_file}"
            VERBATIM
    )
    set("${output_variable}" "${output_file}" PARENT_SCOPE)
endfunction()

set(SPECTRA_RASTERIZER_SHADER_HEADERS)
set(SPECTRA_RASTERIZER_SHADER_AGGREGATE_CONTENT)
foreach (shader_file IN LISTS SPECTRA_RASTERIZER_SHADER_FILES)
    get_filename_component(shader_name "${shader_file}" NAME_WE)
    foreach (stage IN ITEMS vertex fragment)
        spectra_rasterizer_shader_stage(shader_header "${shader_file}" "${shader_name}" "${stage}")
        list(APPEND SPECTRA_RASTERIZER_SHADER_HEADERS "${shader_header}")
        get_filename_component(shader_header_name "${shader_header}" NAME)
        string(APPEND SPECTRA_RASTERIZER_SHADER_AGGREGATE_CONTENT "#include <${shader_header_name}>\n")
    endforeach ()
endforeach ()

set(SPECTRA_RASTERIZER_SHADER_AGGREGATE_HEADER "${SPECTRA_RASTERIZER_GENERATED_DIR}/spectra_rasterizer_shaders_spv.h")
file(MAKE_DIRECTORY "${SPECTRA_RASTERIZER_GENERATED_DIR}")
file(GENERATE OUTPUT "${SPECTRA_RASTERIZER_SHADER_AGGREGATE_HEADER}" CONTENT "${SPECTRA_RASTERIZER_SHADER_AGGREGATE_CONTENT}")
set_source_files_properties("${SPECTRA_RASTERIZER_SHADER_AGGREGATE_HEADER}" PROPERTIES GENERATED TRUE)

target_sources(
        spectra_rasterizer
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        rasterizer/host.ixx
        rasterizer/renderer.ixx
        rasterizer/scene_runtime.ixx
        PRIVATE
        FILE_SET private_cxx_modules TYPE CXX_MODULES FILES
        rasterizer/math.ixx
        PRIVATE
        rasterizer/renderer.cpp
        rasterizer/scene_runtime.cpp
        "${SPECTRA_RASTERIZER_SHADER_AGGREGATE_HEADER}"
        ${SPECTRA_RASTERIZER_SHADER_HEADERS}
)
target_link_libraries(spectra_rasterizer PUBLIC Vulkan::Vulkan vendor::imgui spectra::scene PRIVATE ${CMAKE_DL_LIBS})
target_include_directories(spectra_rasterizer PRIVATE ${SPECTRA_ROOT_DIR}/vendor "${SPECTRA_RASTERIZER_GENERATED_DIR}")
set_property(TARGET spectra_rasterizer PROPERTY CXX_MODULE_STD ON)


# ============================================================================
# GUI
# ============================================================================
add_executable(spectra_gui main.cpp)
add_executable(spectra::gui ALIAS spectra_gui)
if (WIN32)
    target_sources(spectra_gui PRIVATE spectra.rc)
endif ()
target_link_libraries(
        spectra_gui
        PRIVATE
        spectra::core
        spectra::scene
        spectra::pathtracer
        spectra::rasterizer
)
set_property(TARGET spectra_gui PROPERTY CXX_MODULE_STD ON)
