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
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt/CMakeLists.txt")
    message(FATAL_ERROR "Expected local PBRT dependencies at '${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt'.")
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
set(SPECTRA_OPTIX_PATH "${PBRT_OPTIX_PATH}" CACHE PATH "Path to OptiX 9.1 SDK for spectra_gpu")
set(SPECTRA_OPTIX_SHADER_MODEL "sm_80" CACHE STRING "CUDA architecture used when compiling spectra OptiX PTX")
if (SPECTRA_OPTIX_SHADER_MODEL STREQUAL "")
    set(SPECTRA_OPTIX_SHADER_MODEL "sm_80" CACHE STRING "CUDA architecture used when compiling spectra OptiX PTX" FORCE)
endif ()

if (SPECTRA_OPTIX_PATH STREQUAL "")
    message(FATAL_ERROR "SPECTRA_OPTIX_PATH must point to the OptiX 9.1 SDK root for spectra_gpu.")
endif ()
if (NOT EXISTS "${SPECTRA_OPTIX_PATH}/include/optix.h")
    message(FATAL_ERROR "SPECTRA_OPTIX_PATH='${SPECTRA_OPTIX_PATH}' does not contain include/optix.h.")
endif ()
if (NOT SPECTRA_OPTIX_SHADER_MODEL MATCHES "^sm_[0-9]+$")
    message(FATAL_ERROR "SPECTRA_OPTIX_SHADER_MODEL='${SPECTRA_OPTIX_SHADER_MODEL}' must use the form sm_XX.")
endif ()

set(SPECTRA_GPU_DEFINITIONS PTEX_STATIC "$<$<COMPILE_LANGUAGE:CXX>:CUDA_NO_HALF>" "$<$<CONFIG:Debug>:PBRT_DEBUG_BUILD>")
if (SPECTRA_FLOAT_AS_DOUBLE)
    list(APPEND SPECTRA_GPU_DEFINITIONS PBRT_FLOAT_AS_DOUBLE)
endif ()
if (SPECTRA_NVTX)
    list(APPEND SPECTRA_GPU_DEFINITIONS NVTX)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if (NOT MSVC)
        message(FATAL_ERROR "Windows builds require MSVC.")
    endif ()
    list(APPEND SPECTRA_GPU_DEFINITIONS
            PBRT_IS_WINDOWS
            PBRT_IS_MSVC
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
            _ENABLE_EXTENDED_ALIGNED_STORAGE
            PBRT_HAS_INTRIN_H
            "PBRT_NOINLINE=__declspec(noinline)"
            "PBRT_RESTRICT=__restrict"
            PBRT_HAVE__ALIGNED_MALLOC
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SPECTRA_GPU_DEFINITIONS
            PBRT_IS_LINUX
            PBRT_HAVE_MMAP
            "PBRT_NOINLINE=__attribute__((noinline))"
            "PBRT_RESTRICT=__restrict__"
            PBRT_HAVE_POSIX_MEMALIGN
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
        "$<$<COMPILE_LANGUAGE:CUDA>:$<IF:$<AND:$<CONFIG:Debug>,$<NOT:$<BOOL:$<TARGET_PROPERTY:CUDA_PTX_COMPILATION>>>>,-G;-g,-lineinfo;-maxrregcount;128>>"
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
file(MAKE_DIRECTORY "${SPECTRA_GPU_GENERATED_DIR}")
file(WRITE "${SPECTRA_GPU_BIN2C_SCRIPT}" [=[
set(file_contents)
foreach(obj IN LISTS OBJECTS)
    get_filename_component(obj_ext "${obj}" EXT)
    get_filename_component(obj_dir "${obj}" DIRECTORY)
    if(obj_ext MATCHES "\\.ptx$")
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
        string(APPEND file_contents " \n${output}")
    endif()
endforeach()
if(file_contents STREQUAL "")
    message(FATAL_ERROR "No PTX input was passed to bin2c: ${OBJECTS}")
endif()
file(WRITE "${OUTPUT}" "${file_contents}")
]=])

add_executable(spectra_gpu_soac src/tools/soac.cpp)
target_compile_definitions(spectra_gpu_soac PRIVATE ${SPECTRA_GPU_DEFINITIONS})
target_include_directories(spectra_gpu_soac PRIVATE ${SPECTRA_GPU_INCLUDE_DIRS})

add_custom_command(
        OUTPUT ${SPECTRA_GPU_GENERATED_DIR}/spectra_soa.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_GPU_GENERATED_DIR}
        COMMAND spectra_gpu_soac ${CMAKE_CURRENT_SOURCE_DIR}/src/core/spectra.soa > ${SPECTRA_GPU_GENERATED_DIR}/spectra_soa.h
        DEPENDS spectra_gpu_soac ${CMAKE_CURRENT_SOURCE_DIR}/src/core/spectra.soa
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_GPU_GENERATED_DIR}/spectra_wavefront_workitems_soa.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_GPU_GENERATED_DIR}
        COMMAND spectra_gpu_soac ${CMAKE_CURRENT_SOURCE_DIR}/src/wavefront/workitems.soa > ${SPECTRA_GPU_GENERATED_DIR}/spectra_wavefront_workitems_soa.h
        DEPENDS spectra_gpu_soac ${CMAKE_CURRENT_SOURCE_DIR}/src/wavefront/workitems.soa
        VERBATIM
)
set(SPECTRA_GPU_SOA_GENERATED
        ${SPECTRA_GPU_GENERATED_DIR}/spectra_soa.h
        ${SPECTRA_GPU_GENERATED_DIR}/spectra_wavefront_workitems_soa.h
)
add_custom_target(spectra_gpu_soa_generated DEPENDS ${SPECTRA_GPU_SOA_GENERATED})

add_executable(spectra_gpu_rgb2spec_opt src/tools/rgb2spec_opt.cpp)
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
        src/core/bssrdf.cu
        src/core/bxdfs.cu
        src/core/cameras.cu
        src/core/film.cu
        src/core/filters.cu
        src/core/interaction.cu
        src/core/lights.cu
        src/core/lightsamplers.cu
        src/core/materials.cu
        src/core/media.cpp
        src/core/options.cu
        src/core/paramdict.cpp
        src/core/samplers.cu
        src/core/shapes.cu
        src/core/textures.cu
)
set(SPECTRA_GPU_UTIL_SOURCE
        src/util/args.cpp
        src/util/bluenoise.cu
        src/util/buffercache.cpp
        src/util/check.cu
        src/util/color.cu
        src/util/colorspace.cu
        src/util/error.cu
        src/util/file.cpp
        src/util/image.cpp
        src/util/loopsubdiv.cpp
        src/util/lowdiscrepancy.cu
        src/util/math.cu
        src/util/memory.cpp
        src/util/mesh.cu
        src/util/mipmap.cpp
        src/util/noise.cu
        src/util/parallel.cpp
        src/util/pmj02tables.cu
        src/util/primes.cu
        src/util/print.cpp
        src/util/progressreporter.cpp
        src/util/pstd.cu
        src/util/sampling.cu
        src/util/scattering.cu
        src/util/sobolmatrices.cu
        src/util/spectrum.cu
        src/util/stbimage.cpp
        src/util/string.cpp
        src/util/transform.cu
        src/util/vecmath.cu
)
set(SPECTRA_GPU_DEVICE_SOURCE
        src/gpu/memory.cu
        src/gpu/util.cu
)

add_library(spectra_gpu_optix_ptx OBJECT src/optix/optix.cu)
set_property(TARGET spectra_gpu_optix_ptx PROPERTY CUDA_PTX_COMPILATION ON)
set_property(TARGET spectra_gpu_optix_ptx PROPERTY CUDA_ARCHITECTURES OFF)
target_compile_definitions(spectra_gpu_optix_ptx PRIVATE ${SPECTRA_GPU_DEFINITIONS})
target_include_directories(spectra_gpu_optix_ptx PRIVATE ${SPECTRA_GPU_INCLUDE_DIRS})
target_compile_options(
        spectra_gpu_optix_ptx
        PRIVATE
        ${SPECTRA_GPU_CUDA_COMPILE_OPTIONS}
        ${SPECTRA_GPU_CUDA_WARNING_OPTIONS}
        "$<$<COMPILE_LANGUAGE:CUDA>:--gpu-architecture=${SPECTRA_OPTIX_SHADER_MODEL}>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --display_error_number>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=20044>"
)
add_dependencies(spectra_gpu_optix_ptx spectra_gpu_soa_generated)

set(SPECTRA_GPU_EMBEDDED_PTX ${SPECTRA_GPU_GENERATED_DIR}/optix.cu.ptx_embedded.c)
add_custom_command(
        OUTPUT ${SPECTRA_GPU_EMBEDDED_PTX}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_GPU_GENERATED_DIR}
        COMMAND ${CMAKE_COMMAND}
        "-DBIN_TO_C_COMMAND=${SPECTRA_GPU_BIN2C}"
        "-DOBJECTS=$<TARGET_OBJECTS:spectra_gpu_optix_ptx>"
        "-DVAR_NAME=SPECTRA_EMBEDDED_PTX"
        "-DOUTPUT=${SPECTRA_GPU_EMBEDDED_PTX}"
        -P ${SPECTRA_GPU_BIN2C_SCRIPT}
        DEPENDS spectra_gpu_optix_ptx $<TARGET_OBJECTS:spectra_gpu_optix_ptx>
        VERBATIM
)
add_library(spectra_gpu_embedded_ptx STATIC ${SPECTRA_GPU_EMBEDDED_PTX})

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
        src/scene.cpp
        src/optix/aggregate.cu
        src/runtime.cpp
        src/runtime.cu
        src/pathtracer.cpp
        src/pathtracer.cu
)
add_library(spectra::gpu_lib ALIAS spectra_gpu_lib)
add_dependencies(spectra_gpu_lib spectra_gpu_soa_generated)
target_compile_definitions(spectra_gpu_lib PUBLIC ${SPECTRA_GPU_DEFINITIONS})
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
        double-conversion
        utf8proc
        CUDA::cudart
        CUDA::cuda_driver
        Threads::Threads
        spectra_gpu_embedded_ptx
)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(spectra_gpu_lib PRIVATE dbghelp shell32 wsock32 ws2_32)
endif ()
set_property(TARGET spectra_gpu_lib PROPERTY CUDA_SEPARABLE_COMPILATION ON)
set_property(TARGET spectra_gpu_lib PROPERTY CUDA_RESOLVE_DEVICE_SYMBOLS ON)

add_executable(spectra_gpu_exe src/cmd/spectra_gpu.cpp)
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
        modules/spectra_gpu.cpp
        modules/spectra.cpp

        vendor/imgui/imgui.cpp
        vendor/imgui/imgui_draw.cpp
        vendor/imgui/imgui_tables.cpp
        vendor/imgui/imgui_widgets.cpp
        vendor/imgui/backends/imgui_impl_glfw.cpp
        vendor/imgui/backends/imgui_impl_vulkan.cpp
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        modules/spectra_gpu.ixx
        modules/spectra.ixx
)
target_link_libraries(spectra_core PRIVATE Vulkan::Vulkan glfw CUDA::cudart spectra::gpu_lib)
target_compile_definitions(spectra_core PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_include_directories(
        spectra_core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw/include
)
set_property(TARGET spectra_core PROPERTY CXX_MODULE_STD ON)


# ============================================================================
# Application
# ============================================================================
add_executable(spectra main.cpp)
target_link_libraries(spectra PRIVATE spectra_core spectra::gpu_lib)
set_property(TARGET spectra PROPERTY CXX_MODULE_STD ON)
