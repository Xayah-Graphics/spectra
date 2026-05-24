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
# Dependencies
# ============================================================================
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pbrt/pbrt.cpp")
    message(FATAL_ERROR "Expected local PBRT source at '${CMAKE_CURRENT_SOURCE_DIR}/pbrt'.")
endif ()
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt/CMakeLists.txt")
    message(FATAL_ERROR "Expected local PBRT dependencies at '${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt'.")
endif ()

find_package(Vulkan 1.4 REQUIRED)
find_package(Threads REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_program(SLANGC_EXECUTABLE NAMES slangc REQUIRED)

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
# PBRT
# ============================================================================
option(PBRT_FLOAT_AS_DOUBLE "Use 64-bit floats" OFF)
option(PBRT_DBG_LOGGING "Enable verbose debug logging" OFF)
option(PBRT_NVTX "Insert NVTX annotations for NVIDIA profiling tools" OFF)
option(PBRT_NVML "Use NVML for GPU performance measurement" OFF)
set(PBRT_OPTIX_PATH "$ENV{PBRT_OPTIX_PATH}" CACHE PATH "Path to OptiX 9.1 SDK")
set(PBRT_OPTIX_SHADER_MODEL "sm_80" CACHE STRING "CUDA architecture used when compiling OptiX PTX")

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type" FORCE)
endif ()

if (PBRT_OPTIX_PATH STREQUAL "")
    message(FATAL_ERROR "PBRT_OPTIX_PATH must point to the OptiX 9.1 SDK root for the full GPU build.")
endif ()
if (NOT EXISTS "${PBRT_OPTIX_PATH}/include/optix.h")
    message(FATAL_ERROR "PBRT_OPTIX_PATH='${PBRT_OPTIX_PATH}' does not contain include/optix.h.")
endif ()

get_filename_component(PBRT_CUDA_COMPILER_BIN "${CMAKE_CUDA_COMPILER}" DIRECTORY)
find_program(
        PBRT_BIN2C
        NAMES bin2c
        PATHS ${PBRT_CUDA_COMPILER_BIN} ${CUDAToolkit_BIN_DIR}
        NO_DEFAULT_PATH
)
if (NOT PBRT_BIN2C)
    message(FATAL_ERROR "CUDA bin2c was not found next to '${CMAKE_CUDA_COMPILER}'.")
endif ()

set(PBRT_DEFINITIONS PBRT_BUILD_GPU_RENDERER PTEX_STATIC "$<$<COMPILE_LANGUAGE:CXX>:CUDA_NO_HALF>" "$<$<CONFIG:Debug>:PBRT_DEBUG_BUILD>")
if (PBRT_FLOAT_AS_DOUBLE)
    list(APPEND PBRT_DEFINITIONS PBRT_FLOAT_AS_DOUBLE)
endif ()
if (PBRT_DBG_LOGGING)
    list(APPEND PBRT_DEFINITIONS PBRT_DBG_LOGGING)
endif ()
if (PBRT_NVTX)
    list(APPEND PBRT_DEFINITIONS NVTX)
endif ()
if (PBRT_NVML)
    list(APPEND PBRT_DEFINITIONS PBRT_USE_NVML)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if (NOT MSVC)
        message(FATAL_ERROR "Windows builds require MSVC.")
    endif ()
    list(APPEND PBRT_DEFINITIONS
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
    list(APPEND PBRT_DEFINITIONS
            PBRT_IS_LINUX
            PBRT_HAVE_MMAP
            "PBRT_NOINLINE=__attribute__((noinline))"
            "PBRT_RESTRICT=__restrict__"
            PBRT_HAVE_POSIX_MEMALIGN
    )
else ()
    message(FATAL_ERROR "Only Linux/WSL and Windows/MSVC are supported by this modernized build baseline.")
endif ()

set(PBRT_CUDA_WARNING_OPTIONS
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
set(PBRT_CUDA_COMPILE_OPTIONS
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:--forward-slash-prefix-opts>>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--extended-lambda>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--forward-unknown-to-host-compiler>"
        "$<$<COMPILE_LANGUAGE:CUDA>:$<IF:$<AND:$<CONFIG:Debug>,$<NOT:$<BOOL:$<TARGET_PROPERTY:CUDA_PTX_COMPILATION>>>>,-G;-g,-lineinfo;-maxrregcount;128>>"
)
string(APPEND CMAKE_CUDA_FLAGS " -Xnvlink -suppress-stack-size-warning")

set(PBRT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/pbrt-generated)
set(PBRT_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt
        ${STB_INCLUDE}
        ${QOI_INCLUDE}
        ${ZLIB_INCLUDE_DIRS}
        ${LIBDEFLATE_INCLUDE_DIRS}
        ${PTEX_INCLUDE}
        ${DOUBLE_CONVERSION_INCLUDE}
        ${NANOVDB_INCLUDE}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${PBRT_GENERATED_DIR}
        ${PBRT_OPTIX_PATH}/include
        ${CUDAToolkit_INCLUDE_DIRS}
        ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES}
)

set(PBRT_BIN2C_SCRIPT ${PBRT_GENERATED_DIR}/pbrt_bin2c_embed.cmake)
file(MAKE_DIRECTORY "${PBRT_GENERATED_DIR}")
file(WRITE "${PBRT_BIN2C_SCRIPT}" [=[
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

add_executable(soac pbrt/cmd/soac.cpp)
add_executable(pbrt::soac ALIAS soac)
target_compile_definitions(soac PRIVATE ${PBRT_DEFINITIONS})
target_include_directories(soac PRIVATE ${PBRT_INCLUDE_DIRS})

add_custom_command(
        OUTPUT ${PBRT_GENERATED_DIR}/pbrt_soa.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PBRT_GENERATED_DIR}
        COMMAND soac ${CMAKE_CURRENT_SOURCE_DIR}/pbrt/pbrt.soa > ${PBRT_GENERATED_DIR}/pbrt_soa.h
        DEPENDS soac ${CMAKE_CURRENT_SOURCE_DIR}/pbrt/pbrt.soa
        VERBATIM
)
add_custom_command(
        OUTPUT ${PBRT_GENERATED_DIR}/wavefront_workitems_soa.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PBRT_GENERATED_DIR}
        COMMAND soac ${CMAKE_CURRENT_SOURCE_DIR}/pbrt/wavefront/workitems.soa > ${PBRT_GENERATED_DIR}/wavefront_workitems_soa.h
        DEPENDS soac ${CMAKE_CURRENT_SOURCE_DIR}/pbrt/wavefront/workitems.soa
        VERBATIM
)
set(PBRT_SOA_GENERATED
        ${PBRT_GENERATED_DIR}/pbrt_soa.h
        ${PBRT_GENERATED_DIR}/wavefront_workitems_soa.h
)
add_custom_target(pbrt_soa_generated DEPENDS ${PBRT_SOA_GENERATED})

add_executable(rgb2spec_opt pbrt/cmd/rgb2spec_opt.cpp)
add_executable(pbrt::rgb2spec_opt ALIAS rgb2spec_opt)
target_compile_definitions(rgb2spec_opt PRIVATE ${PBRT_DEFINITIONS})
target_include_directories(rgb2spec_opt PRIVATE ${PBRT_INCLUDE_DIRS})
target_link_libraries(rgb2spec_opt PRIVATE Threads::Threads)

set(PBRT_RGB_TO_SPECTRUM_SOURCES
        ${PBRT_GENERATED_DIR}/rgbspectrum_aces.cpp
        ${PBRT_GENERATED_DIR}/rgbspectrum_dci_p3.cpp
        ${PBRT_GENERATED_DIR}/rgbspectrum_rec2020.cpp
        ${PBRT_GENERATED_DIR}/rgbspectrum_srgb.cpp
)
add_custom_command(
        OUTPUT ${PBRT_GENERATED_DIR}/rgbspectrum_aces.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PBRT_GENERATED_DIR}
        COMMAND rgb2spec_opt 64 ${PBRT_GENERATED_DIR}/rgbspectrum_aces.cpp ACES2065_1
        DEPENDS rgb2spec_opt
        VERBATIM
)
add_custom_command(
        OUTPUT ${PBRT_GENERATED_DIR}/rgbspectrum_dci_p3.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PBRT_GENERATED_DIR}
        COMMAND rgb2spec_opt 64 ${PBRT_GENERATED_DIR}/rgbspectrum_dci_p3.cpp DCI_P3
        DEPENDS rgb2spec_opt
        VERBATIM
)
add_custom_command(
        OUTPUT ${PBRT_GENERATED_DIR}/rgbspectrum_rec2020.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PBRT_GENERATED_DIR}
        COMMAND rgb2spec_opt 64 ${PBRT_GENERATED_DIR}/rgbspectrum_rec2020.cpp REC2020
        DEPENDS rgb2spec_opt
        VERBATIM
)
add_custom_command(
        OUTPUT ${PBRT_GENERATED_DIR}/rgbspectrum_srgb.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PBRT_GENERATED_DIR}
        COMMAND rgb2spec_opt 64 ${PBRT_GENERATED_DIR}/rgbspectrum_srgb.cpp sRGB
        DEPENDS rgb2spec_opt
        VERBATIM
)

set(PBRT_SOURCE
        pbrt/bsdf.cpp
        pbrt/bssrdf.cpp
        pbrt/bxdfs.cpp
        pbrt/cameras.cpp
        pbrt/film.cpp
        pbrt/filters.cpp
        pbrt/interaction.cpp
        pbrt/lights.cpp
        pbrt/lightsamplers.cpp
        pbrt/materials.cpp
        pbrt/media.cpp
        pbrt/options.cpp
        pbrt/paramdict.cpp
        pbrt/parser.cpp
        pbrt/pbrt.cpp
        pbrt/ray.cpp
        pbrt/samplers.cpp
        pbrt/scene.cpp
        pbrt/shapes.cpp
        pbrt/textures.cpp
        pbrt/cmd/pspec_gpu.cpp
)
set(PBRT_CPU_SOURCE
        pbrt/cpu/aggregates.cpp
        pbrt/cpu/integrators.cpp
        pbrt/cpu/primitive.cpp
        pbrt/cpu/render.cpp
)
set(PBRT_WAVEFRONT_SOURCE
        pbrt/wavefront/aggregate.cpp
        pbrt/wavefront/camera.cpp
        pbrt/wavefront/film.cpp
        pbrt/wavefront/integrator.cpp
        pbrt/wavefront/media.cpp
        pbrt/wavefront/samples.cpp
        pbrt/wavefront/surfscatter.cpp
        pbrt/wavefront/subsurface.cpp
        pbrt/wavefront/wavefront.cpp
)
set(PBRT_UTIL_SOURCE
        pbrt/util/args.cpp
        pbrt/util/bluenoise.cpp
        pbrt/util/buffercache.cpp
        pbrt/util/check.cpp
        pbrt/util/color.cpp
        pbrt/util/colorspace.cpp
        pbrt/util/error.cpp
        pbrt/util/file.cpp
        pbrt/util/float.cpp
        pbrt/util/image.cpp
        pbrt/util/log.cpp
        pbrt/util/loopsubdiv.cpp
        pbrt/util/lowdiscrepancy.cpp
        pbrt/util/math.cpp
        pbrt/util/memory.cpp
        pbrt/util/mesh.cpp
        pbrt/util/mipmap.cpp
        pbrt/util/noise.cpp
        pbrt/util/parallel.cpp
        pbrt/util/pmj02tables.cpp
        pbrt/util/primes.cpp
        pbrt/util/print.cpp
        pbrt/util/progressreporter.cpp
        pbrt/util/pstd.cpp
        pbrt/util/rng.cpp
        pbrt/util/sampling.cpp
        pbrt/util/scattering.cpp
        pbrt/util/sobolmatrices.cpp
        pbrt/util/spectrum.cpp
        pbrt/util/stats.cpp
        pbrt/util/stbimage.cpp
        pbrt/util/string.cpp
        pbrt/util/transform.cpp
        pbrt/util/vecmath.cpp
)
set(PBRT_GPU_SOURCE
        pbrt/gpu/optix/aggregate.cpp
        pbrt/gpu/optix/denoiser.cpp
        pbrt/gpu/optix/scaler.cpp
        pbrt/gpu/memory.cpp
        pbrt/gpu/util.cpp
)
set_source_files_properties(
        pbrt/bsdf.cpp
        pbrt/bssrdf.cpp
        pbrt/bxdfs.cpp
        pbrt/cameras.cpp
        pbrt/film.cpp
        pbrt/filters.cpp
        pbrt/interaction.cpp
        pbrt/lights.cpp
        pbrt/lightsamplers.cpp
        pbrt/materials.cpp
        pbrt/options.cpp
        pbrt/pbrt.cpp
        pbrt/samplers.cpp
        pbrt/shapes.cpp
        pbrt/textures.cpp
        pbrt/util/bluenoise.cpp
        pbrt/util/check.cpp
        pbrt/util/color.cpp
        pbrt/util/colorspace.cpp
        pbrt/util/error.cpp
        pbrt/util/log.cpp
        pbrt/util/lowdiscrepancy.cpp
        pbrt/util/math.cpp
        pbrt/util/mesh.cpp
        pbrt/util/noise.cpp
        pbrt/util/pmj02tables.cpp
        pbrt/util/primes.cpp
        pbrt/util/pstd.cpp
        pbrt/util/rng.cpp
        pbrt/util/sampling.cpp
        pbrt/util/scattering.cpp
        pbrt/util/sobolmatrices.cpp
        pbrt/util/spectrum.cpp
        pbrt/util/stats.cpp
        pbrt/util/transform.cpp
        pbrt/util/vecmath.cpp
        ${PBRT_WAVEFRONT_SOURCE}
        ${PBRT_GPU_SOURCE}
        pbrt/cmd/pspec_gpu.cpp
        PROPERTIES LANGUAGE CUDA
)

add_library(pbrt_optix_ptx OBJECT pbrt/gpu/optix/optix.cu)
set_property(TARGET pbrt_optix_ptx PROPERTY CUDA_PTX_COMPILATION ON)
set_property(TARGET pbrt_optix_ptx PROPERTY CUDA_ARCHITECTURES OFF)
target_compile_definitions(pbrt_optix_ptx PRIVATE ${PBRT_DEFINITIONS})
target_compile_options(
        pbrt_optix_ptx
        PRIVATE
        ${PBRT_CUDA_COMPILE_OPTIONS}
        ${PBRT_CUDA_WARNING_OPTIONS}
        "$<$<COMPILE_LANGUAGE:CUDA>:--gpu-architecture=${PBRT_OPTIX_SHADER_MODEL}>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --display_error_number>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=20044>"
)
target_include_directories(pbrt_optix_ptx PRIVATE ${PBRT_INCLUDE_DIRS})
add_dependencies(pbrt_optix_ptx pbrt_soa_generated)

set(PBRT_EMBEDDED_PTX ${PBRT_GENERATED_DIR}/optix.cu.ptx_embedded.c)
add_custom_command(
        OUTPUT ${PBRT_EMBEDDED_PTX}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PBRT_GENERATED_DIR}
        COMMAND ${CMAKE_COMMAND}
        "-DBIN_TO_C_COMMAND=${PBRT_BIN2C}"
        "-DOBJECTS=$<TARGET_OBJECTS:pbrt_optix_ptx>"
        "-DVAR_NAME=PBRT_EMBEDDED_PTX"
        "-DOUTPUT=${PBRT_EMBEDDED_PTX}"
        -P ${PBRT_BIN2C_SCRIPT}
        DEPENDS pbrt_optix_ptx $<TARGET_OBJECTS:pbrt_optix_ptx>
        VERBATIM
)
add_library(pbrt_embedded_ptx STATIC ${PBRT_EMBEDDED_PTX})

add_library(pbrt_lib STATIC
        ${PBRT_RGB_TO_SPECTRUM_SOURCES}
        ${PBRT_SOA_GENERATED}
        ${PBRT_SOURCE}
        ${PBRT_CPU_SOURCE}
        ${PBRT_UTIL_SOURCE}
        ${PBRT_WAVEFRONT_SOURCE}
        ${PBRT_GPU_SOURCE}
        vendor/pbrt/lodepng/lodepng.cpp
        vendor/pbrt/rply/rply.cpp
)
add_library(pbrt::pbrt_lib ALIAS pbrt_lib)
add_dependencies(pbrt_lib pbrt_soa_generated)
target_compile_definitions(pbrt_lib PUBLIC ${PBRT_DEFINITIONS})
target_include_directories(pbrt_lib PUBLIC ${PBRT_INCLUDE_DIRS})
target_compile_options(
        pbrt_lib
        PUBLIC
        "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-ffp-contract=off>"
        ${PBRT_CUDA_COMPILE_OPTIONS}
        ${PBRT_CUDA_WARNING_OPTIONS}
)
target_link_libraries(
        pbrt_lib
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
        pbrt_embedded_ptx
)
if (PBRT_NVML)
    target_link_libraries(pbrt_lib PRIVATE nvidia-ml)
endif ()
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(pbrt_lib PRIVATE dbghelp wsock32 ws2_32)
endif ()
set_property(TARGET pbrt_lib PROPERTY CUDA_SEPARABLE_COMPILATION ON)
set_property(TARGET pbrt_lib PROPERTY CUDA_RESOLVE_DEVICE_SYMBOLS ON)

add_executable(pbrt_exe pbrt/cmd/pbrt.cpp)
add_executable(pbrt::pbrt_exe ALIAS pbrt_exe)
target_compile_definitions(pbrt_exe PRIVATE ${PBRT_DEFINITIONS})
target_include_directories(pbrt_exe PRIVATE ${PBRT_INCLUDE_DIRS})
target_link_libraries(pbrt_exe PRIVATE pbrt_lib Threads::Threads)
set_target_properties(pbrt_exe PROPERTIES OUTPUT_NAME pbrt)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(pbrt_exe PRIVATE -rdynamic "LINKER:--no-as-needed")
endif ()


# ============================================================================
# Spectra
# ============================================================================
set(SPECTRA_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/spectra-generated)
set(SPECTRA_RASTER_VERTEX_SPIRV ${SPECTRA_GENERATED_DIR}/spectra_raster.vert.spv)
set(SPECTRA_RASTER_FRAGMENT_SPIRV ${SPECTRA_GENERATED_DIR}/spectra_raster.frag.spv)
set(SPECTRA_RASTER_SPIRV_HEADER ${SPECTRA_GENERATED_DIR}/spectra_raster_spirv.h)
add_custom_command(
        OUTPUT ${SPECTRA_RASTER_VERTEX_SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_GENERATED_DIR}
        COMMAND ${SLANGC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spectra_raster.slang -target spirv -profile spirv_1_6 -entry vertex_main -stage vertex -matrix-layout-row-major -o ${SPECTRA_RASTER_VERTEX_SPIRV}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spectra_raster.slang
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_RASTER_FRAGMENT_SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_GENERATED_DIR}
        COMMAND ${SLANGC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spectra_raster.slang -target spirv -profile spirv_1_6 -entry fragment_main -stage fragment -matrix-layout-row-major -o ${SPECTRA_RASTER_FRAGMENT_SPIRV}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spectra_raster.slang
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_RASTER_SPIRV_HEADER}
        COMMAND ${CMAKE_COMMAND} -DVERTEX_INPUT=${SPECTRA_RASTER_VERTEX_SPIRV} -DFRAGMENT_INPUT=${SPECTRA_RASTER_FRAGMENT_SPIRV} -DOUTPUT=${SPECTRA_RASTER_SPIRV_HEADER} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake
        DEPENDS ${SPECTRA_RASTER_VERTEX_SPIRV} ${SPECTRA_RASTER_FRAGMENT_SPIRV} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake
        VERBATIM
)

add_library(spectra_core)
target_sources(
        spectra_core
        PRIVATE
        ${SPECTRA_RASTER_SPIRV_HEADER}
        modules/spectra.cpp

        vendor/imgui/imgui.cpp
        vendor/imgui/imgui_draw.cpp
        vendor/imgui/imgui_tables.cpp
        vendor/imgui/imgui_widgets.cpp
        vendor/imgui/backends/imgui_impl_glfw.cpp
        vendor/imgui/backends/imgui_impl_vulkan.cpp
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        modules/spectra.ixx
)
target_link_libraries(spectra_core PRIVATE Vulkan::Vulkan glfw pbrt_lib CUDA::cudart)
target_compile_definitions(spectra_core PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_include_directories(
        spectra_core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw/include
        ${SPECTRA_GENERATED_DIR}
)
set_property(TARGET spectra_core PROPERTY CXX_MODULE_STD ON)


# ============================================================================
# Application
# ============================================================================
add_executable(spectra main.cpp)
target_link_libraries(spectra PRIVATE spectra_core)
set_property(TARGET spectra PROPERTY CXX_MODULE_STD ON)
