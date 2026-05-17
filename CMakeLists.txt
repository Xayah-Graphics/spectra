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
find_package(Vulkan 1.4 REQUIRED)
find_package(Threads REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_program(SLANGC_EXECUTABLE slangc REQUIRED)
add_subdirectory(vendor/glfw)


# ============================================================================
# Shaders
# ============================================================================
set(SPECTRA_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(SPECTRA_GRID_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo_grid.slang)
set(SPECTRA_VOLUME_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volume.slang)
set(SPECTRA_MESH_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.slang)
set(SPECTRA_PARTICLES_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/particles.slang)
set(SPECTRA_BOUNDING_BOX_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bounding_box.slang)
set(
        SPECTRA_SHADER_OUTPUTS
        ${SPECTRA_SHADER_OUTPUT_DIR}/gizmo_grid.vert.spv
        ${SPECTRA_SHADER_OUTPUT_DIR}/gizmo_grid.frag.spv
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
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_GRID_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry vertex_main -stage vertex -o ${SPECTRA_SHADER_OUTPUT_DIR}/gizmo_grid.vert.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_GRID_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry fragment_main -stage fragment -o ${SPECTRA_SHADER_OUTPUT_DIR}/gizmo_grid.frag.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_VOLUME_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry vertex_main -stage vertex -o ${SPECTRA_SHADER_OUTPUT_DIR}/volume.vert.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_VOLUME_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry fragment_main -stage fragment -o ${SPECTRA_SHADER_OUTPUT_DIR}/volume.frag.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_MESH_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry vertex_main -stage vertex -o ${SPECTRA_SHADER_OUTPUT_DIR}/mesh.vert.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_MESH_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry fragment_main -stage fragment -o ${SPECTRA_SHADER_OUTPUT_DIR}/mesh.frag.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_PARTICLES_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry vertex_main -stage vertex -o ${SPECTRA_SHADER_OUTPUT_DIR}/particles.vert.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_PARTICLES_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry fragment_main -stage fragment -o ${SPECTRA_SHADER_OUTPUT_DIR}/particles.frag.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_BOUNDING_BOX_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry vertex_main -stage vertex -o ${SPECTRA_SHADER_OUTPUT_DIR}/bounding_box.vert.spv
        COMMAND ${SLANGC_EXECUTABLE} ${SPECTRA_BOUNDING_BOX_SHADER} -target spirv -profile spirv_1_6 -matrix-layout-column-major -entry fragment_main -stage fragment -o ${SPECTRA_SHADER_OUTPUT_DIR}/bounding_box.frag.spv
        DEPENDS ${SPECTRA_GRID_SHADER} ${SPECTRA_VOLUME_SHADER} ${SPECTRA_MESH_SHADER} ${SPECTRA_PARTICLES_SHADER} ${SPECTRA_BOUNDING_BOX_SHADER}
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
target_include_directories(spectra PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGuizmo ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImNodeFlow ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw/include)
set_property(TARGET spectra PROPERTY CXX_MODULE_STD ON)

# ============================================================================
# PBRT
# ============================================================================
set(PBRT_OPTIX_PATH "" CACHE PATH "Path to OptiX SDK")

if (PBRT_OPTIX_PATH STREQUAL "")
    message(FATAL_ERROR "PBRT_OPTIX_PATH must point to the OptiX SDK root.")
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

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/pbrt/zlib ${CMAKE_CURRENT_BINARY_DIR}/vendor/pbrt/zlib)
if (NOT TARGET ZLIB::ZLIB AND TARGET zlibstatic)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif ()

set(DEFLATE_BUILD_TEST_PROGRAMS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/pbrt/libdeflate ${CMAKE_CURRENT_BINARY_DIR}/vendor/pbrt/libdeflate)

set(IMATH_INSTALL OFF CACHE BOOL "" FORCE)
set(IMATH_INSTALL_PKG_CONFIG OFF CACHE BOOL "" FORCE)
set(IMATH_USE_CLANG_TIDY OFF CACHE BOOL "" FORCE)
set(PYTHON OFF CACHE BOOL "" FORCE)
set(BUILD_WEBSITE OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/pbrt/imath ${CMAKE_CURRENT_BINARY_DIR}/vendor/pbrt/imath)

set(OPENEXR_INSTALL OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_PKG_CONFIG OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(OPENEXR_TEST_LIBRARIES OFF CACHE BOOL "" FORCE)
set(OPENEXR_TEST_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_TEST_PYTHON OFF CACHE BOOL "" FORCE)
set(OPENEXR_FORCE_INTERNAL_DEFLATE OFF CACHE BOOL "" FORCE)
set(OPENEXR_FORCE_INTERNAL_IMATH OFF CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_libdeflate ON CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig ON CACHE BOOL "" FORCE)
set(EXR_DEFLATE_LIB deflate::deflate CACHE STRING "" FORCE)
set(EXR_DEFLATE_VERSION 1.0 CACHE STRING "" FORCE)
add_subdirectory(vendor/pbrt/openexr ${CMAKE_CURRENT_BINARY_DIR}/vendor/pbrt/openexr)
set(CMAKE_DISABLE_FIND_PACKAGE_libdeflate OFF CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig OFF CACHE BOOL "" FORCE)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/pbrt/double-conversion ${CMAKE_CURRENT_BINARY_DIR}/vendor/pbrt/double-conversion)

set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/pbrt/utf8proc ${CMAKE_CURRENT_BINARY_DIR}/vendor/pbrt/utf8proc)

add_library(Ptex_static STATIC
        vendor/pbrt/ptex/src/ptex/PtexCache.cpp
        vendor/pbrt/ptex/src/ptex/PtexFilters.cpp
        vendor/pbrt/ptex/src/ptex/PtexHalf.cpp
        vendor/pbrt/ptex/src/ptex/PtexReader.cpp
        vendor/pbrt/ptex/src/ptex/PtexSeparableFilter.cpp
        vendor/pbrt/ptex/src/ptex/PtexSeparableKernel.cpp
        vendor/pbrt/ptex/src/ptex/PtexTriangleFilter.cpp
        vendor/pbrt/ptex/src/ptex/PtexTriangleKernel.cpp
        vendor/pbrt/ptex/src/ptex/PtexUtils.cpp
        vendor/pbrt/ptex/src/ptex/PtexWriter.cpp
)
target_compile_definitions(Ptex_static PUBLIC PTEX_STATIC)
target_include_directories(Ptex_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt/ptex/src/ptex)
target_link_libraries(Ptex_static PUBLIC Threads::Threads ZLIB::ZLIB)

set(PBRT_DEFINITIONS PBRT_BUILD_GPU_RENDERER PTEX_STATIC "$<$<CONFIG:Debug>:PBRT_DEBUG_BUILD>")
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
    message(FATAL_ERROR "Only Windows/MSVC and Linux/Arch WSL builds are supported.")
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
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt/stb
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt/qoi
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt/zlib
        ${CMAKE_CURRENT_BINARY_DIR}/vendor/pbrt/zlib
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt/libdeflate
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt/filesystem
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt/ptex/src/ptex
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt/double-conversion
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/pbrt/openvdb/nanovdb
        ${PBRT_GENERATED_DIR}
        ${PBRT_OPTIX_PATH}/include
        ${CUDAToolkit_INCLUDE_DIRS}
)

add_executable(pbrt_soac pbrt/cmd/soac.cpp)
target_compile_definitions(pbrt_soac PRIVATE ${PBRT_DEFINITIONS})
target_include_directories(pbrt_soac PRIVATE ${PBRT_INCLUDE_DIRS})

add_custom_command(
        OUTPUT ${PBRT_GENERATED_DIR}/pbrt_soa.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PBRT_GENERATED_DIR}
        COMMAND pbrt_soac ${CMAKE_CURRENT_SOURCE_DIR}/pbrt/pbrt.soa > ${PBRT_GENERATED_DIR}/pbrt_soa.h
        DEPENDS pbrt_soac ${CMAKE_CURRENT_SOURCE_DIR}/pbrt/pbrt.soa
        VERBATIM
)
add_custom_command(
        OUTPUT ${PBRT_GENERATED_DIR}/wavefront_workitems_soa.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PBRT_GENERATED_DIR}
        COMMAND pbrt_soac ${CMAKE_CURRENT_SOURCE_DIR}/pbrt/wavefront/workitems.soa > ${PBRT_GENERATED_DIR}/wavefront_workitems_soa.h
        DEPENDS pbrt_soac ${CMAKE_CURRENT_SOURCE_DIR}/pbrt/wavefront/workitems.soa
        VERBATIM
)
set(PBRT_SOA_GENERATED
        ${PBRT_GENERATED_DIR}/pbrt_soa.h
        ${PBRT_GENERATED_DIR}/wavefront_workitems_soa.h
)
add_custom_target(pbrt_soa_generated DEPENDS ${PBRT_SOA_GENERATED})

set(PBRT_RGB_TO_SPECTRUM_SOURCES
        pbrt/rgb2spectrum/rgbspectrum_aces.cpp
        pbrt/rgb2spectrum/rgbspectrum_dci_p3.cpp
        pbrt/rgb2spectrum/rgbspectrum_rec2020.cpp
        pbrt/rgb2spectrum/rgbspectrum_srgb.cpp
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
        pbrt/util/display.cpp
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
        "$<$<COMPILE_LANGUAGE:CUDA>:--gpu-architecture=sm_80>"
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
        -P ${CMAKE_CURRENT_SOURCE_DIR}/pbrt/cmake/bin2c_wrapper.cmake
        DEPENDS pbrt_optix_ptx $<TARGET_OBJECTS:pbrt_optix_ptx>
        VERBATIM
)
add_library(pbrt_embedded_ptx STATIC ${PBRT_EMBEDDED_PTX})

add_library(pbrt_core STATIC
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
add_library(pbrt::core ALIAS pbrt_core)
add_dependencies(pbrt_core pbrt_soa_generated)
target_compile_definitions(pbrt_core PRIVATE ${PBRT_DEFINITIONS})
target_include_directories(pbrt_core PUBLIC ${PBRT_INCLUDE_DIRS})
target_compile_options(
        pbrt_core
        PUBLIC
        "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-ffp-contract=off>"
        ${PBRT_CUDA_COMPILE_OPTIONS}
        ${PBRT_CUDA_WARNING_OPTIONS}
)
target_link_libraries(
        pbrt_core
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
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(pbrt_core PRIVATE dbghelp wsock32 ws2_32)
endif ()
set_property(TARGET pbrt_core PROPERTY CUDA_SEPARABLE_COMPILATION ON)
set_property(TARGET pbrt_core PROPERTY CUDA_RESOLVE_DEVICE_SYMBOLS ON)

add_executable(pbrt_exe pbrt/cmd/pbrt.cpp)
add_executable(pbrt::exe ALIAS pbrt_exe)
target_compile_definitions(pbrt_exe PRIVATE ${PBRT_DEFINITIONS})
target_include_directories(pbrt_exe PRIVATE ${PBRT_INCLUDE_DIRS})
target_link_libraries(pbrt_exe PRIVATE pbrt_core)
set_target_properties(pbrt_exe PROPERTIES OUTPUT_NAME pbrt)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(pbrt_exe PRIVATE -rdynamic "LINKER:--no-as-needed")
endif ()

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
set_property(TARGET bouncingball PROPERTY CXX_MODULE_STD ON)

add_library(cloth)
target_sources(
        cloth
        PRIVATE
        ext/cloth.cpp
        ext/cloth.cu
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        ext/cloth.ixx
)
target_link_libraries(cloth PRIVATE CUDA::cudart)
set_property(TARGET cloth PROPERTY CXX_MODULE_STD ON)

add_library(sparkles)
target_sources(
        sparkles
        PRIVATE
        ext/sparkles.cpp
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        ext/sparkles.ixx
)
set_property(TARGET sparkles PROPERTY CXX_MODULE_STD ON)

add_executable(test main.cpp)
target_link_libraries(test PRIVATE spectra pyro bouncingball cloth sparkles)
set_property(TARGET test PROPERTY CXX_MODULE_STD ON)
