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

set(SPECTRA_PATHTRACER_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/spectra_pathtracer)
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
        ${SPECTRA_ROOT_DIR}/vendor/pbrt
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
foreach(obj IN LISTS OBJECTS)
    get_filename_component(obj_ext "${obj}" EXT)
    get_filename_component(obj_dir "${obj}" DIRECTORY)
    if(obj_ext MATCHES "\\.(optixir|ptx)$")
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
    message(FATAL_ERROR "No OptiX input was passed to bin2c: ${OBJECTS}")
endif()
file(WRITE "${OUTPUT}" "${file_contents}")
]=])

add_executable(spectra_pathtracer_soac tools/soac.cpp)
target_compile_definitions(spectra_pathtracer_soac PRIVATE ${SPECTRA_PATHTRACER_DEFINITIONS})
target_include_directories(spectra_pathtracer_soac PRIVATE ${SPECTRA_PATHTRACER_INCLUDE_DIRS})

add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_soa.cuh
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND spectra_pathtracer_soac ${CMAKE_CURRENT_SOURCE_DIR}/core/spectra.soa > ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_soa.cuh
        DEPENDS spectra_pathtracer_soac ${CMAKE_CURRENT_SOURCE_DIR}/core/spectra.soa
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_wavefront_workitems_soa.cuh
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND spectra_pathtracer_soac ${CMAKE_CURRENT_SOURCE_DIR}/wavefront/workitems.soa > ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_wavefront_workitems_soa.cuh
        DEPENDS spectra_pathtracer_soac ${CMAKE_CURRENT_SOURCE_DIR}/wavefront/workitems.soa
        VERBATIM
)
set(SPECTRA_PATHTRACER_SOA_GENERATED
        ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_soa.cuh
        ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_wavefront_workitems_soa.cuh
)
add_custom_target(spectra_pathtracer_soa_generated DEPENDS ${SPECTRA_PATHTRACER_SOA_GENERATED})

add_executable(spectra_pathtracer_rgb2spec_opt tools/rgb2spec_opt.cpp)
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

set(SPECTRA_PATHTRACER_CORE_SOURCE
        core/bssrdf.cu
        core/bxdfs.cu
        core/cameras.cu
        core/film.cu
        core/filters.cu
        core/interaction.cu
        core/kernel_config.cu
        core/lights.cu
        core/lightsamplers.cu
        core/materials.cu
        core/media_host.cu
        core/media.cu
        core/paramdict.cu
        core/samplers.cu
        core/shapes.cu
        core/textures.cu
)
set(SPECTRA_PATHTRACER_UTIL_SOURCE
        util/bluenoise.cu
        util/color.cu
        util/colorspace.cu
        util/file.cpp
        util/image.cpp
        util/loopsubdiv.cu
        util/lowdiscrepancy.cu
        util/math.cu
        util/memory.cu
        util/mesh.cu
        util/mipmap.cu
        util/noise.cu
        util/parallel.cu
        util/pmj02tables.cu
        util/primes.cu
        util/pstd.cu
        util/sampling.cu
        util/scattering.cu
        util/sobolmatrices.cu
        util/spectrum.cu
        util/stbimage.cu
        util/transform.cu
        util/vecmath.cu
)
set(SPECTRA_PATHTRACER_DEVICE_SOURCE
        gpu/util.cu
        memory/memory.cu
)

add_library(spectra_pathtracer_optix_input OBJECT optix/optix.cu)
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
        pathtracer.ixx
        pbrt_scene.ixx
        pbrt_scene_library.ixx
        PRIVATE
        ${SPECTRA_PATHTRACER_RGB_TO_SPECTRUM_SOURCES}
        ${SPECTRA_PATHTRACER_SOA_GENERATED}
        ${SPECTRA_PATHTRACER_CORE_SOURCE}
        ${SPECTRA_PATHTRACER_UTIL_SOURCE}
        ${SPECTRA_PATHTRACER_DEVICE_SOURCE}
        ${SPECTRA_ROOT_DIR}/vendor/pbrt/lodepng/lodepng.cpp
        ${SPECTRA_ROOT_DIR}/vendor/pbrt/rply/rply.cpp
        pathtracer.cpp
        pbrt_scene.cpp
        pbrt_scene_library.cpp
        compiled_scene.cpp
        optix/aggregate.cu
        integrator.cu
)
add_dependencies(spectra_pathtracer spectra_pathtracer_soa_generated)
target_compile_definitions(spectra_pathtracer PUBLIC ${SPECTRA_PATHTRACER_DEFINITIONS})
target_compile_definitions(spectra_pathtracer PRIVATE "SPECTRA_PBRT_SCENE_ROOT=\"${SPECTRA_PBRT_SCENE_ROOT}\"" IMGUI_DEFINE_MATH_OPERATORS)
target_include_directories(spectra_pathtracer PUBLIC ${SPECTRA_PATHTRACER_INCLUDE_DIRS} PRIVATE ${SPECTRA_ROOT_DIR}/vendor)
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
        PRIVATE
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

add_executable(spectra_pathtracer_cli pathtracer_cli.cpp)
add_executable(spectra::pathtracer_cli ALIAS spectra_pathtracer_cli)
target_link_libraries(spectra_pathtracer_cli PRIVATE spectra::pathtracer Threads::Threads)
set_property(TARGET spectra_pathtracer_cli PROPERTY CXX_MODULE_STD ON)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(spectra_pathtracer_cli PRIVATE -rdynamic "LINKER:--no-as-needed")
endif ()
