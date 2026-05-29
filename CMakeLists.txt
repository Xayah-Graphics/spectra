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
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pbrt_gpu/pbrt.cu")
    message(FATAL_ERROR "Expected local pbrt_gpu source at '${CMAKE_CURRENT_SOURCE_DIR}/pbrt_gpu'.")
endif ()
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
# PBRT GPU
# ============================================================================
add_subdirectory(pbrt_gpu)


# ============================================================================
# Spectra
# ============================================================================
add_library(spectra_core)
target_sources(
        spectra_core
        PRIVATE
        modules/pbrt.cpp
        modules/spectra.cpp

        vendor/imgui/imgui.cpp
        vendor/imgui/imgui_draw.cpp
        vendor/imgui/imgui_tables.cpp
        vendor/imgui/imgui_widgets.cpp
        vendor/imgui/backends/imgui_impl_glfw.cpp
        vendor/imgui/backends/imgui_impl_vulkan.cpp
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        modules/pbrt.ixx
        modules/spectra.ixx
)
target_link_libraries(spectra_core PRIVATE Vulkan::Vulkan glfw pbrt_gpu_lib CUDA::cudart)
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
target_link_libraries(spectra PRIVATE spectra_core)
set_property(TARGET spectra PROPERTY CXX_MODULE_STD ON)


# ============================================================================
# Spectra GPU
# ============================================================================
get_target_property(SPECTRA_GPU_PBRT_SOURCE pbrt_gpu_lib SOURCES)
if (NOT SPECTRA_GPU_PBRT_SOURCE)
    message(FATAL_ERROR "pbrt_gpu_lib did not expose its source list.")
endif ()

set(SPECTRA_GPU_RUNTIME_SOURCE)
foreach (source IN LISTS SPECTRA_GPU_PBRT_SOURCE)
    if (source MATCHES "(^|/)wavefront/")
        continue ()
    endif ()
    if (IS_ABSOLUTE "${source}")
        list(APPEND SPECTRA_GPU_RUNTIME_SOURCE "${source}")
    else ()
        list(APPEND SPECTRA_GPU_RUNTIME_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/pbrt_gpu/${source}")
    endif ()
endforeach ()

get_target_property(SPECTRA_GPU_PBRT_DEFINITIONS pbrt_gpu_lib COMPILE_DEFINITIONS)
get_target_property(SPECTRA_GPU_PBRT_INCLUDES pbrt_gpu_lib INCLUDE_DIRECTORIES)
get_target_property(SPECTRA_GPU_PBRT_OPTIONS pbrt_gpu_lib COMPILE_OPTIONS)
get_target_property(SPECTRA_GPU_PBRT_LIBRARIES pbrt_gpu_lib LINK_LIBRARIES)
if (SPECTRA_GPU_PBRT_DEFINITIONS STREQUAL "SPECTRA_GPU_PBRT_DEFINITIONS-NOTFOUND")
    message(FATAL_ERROR "pbrt_gpu_lib did not expose compile definitions.")
endif ()
if (SPECTRA_GPU_PBRT_INCLUDES STREQUAL "SPECTRA_GPU_PBRT_INCLUDES-NOTFOUND")
    message(FATAL_ERROR "pbrt_gpu_lib did not expose include directories.")
endif ()
if (SPECTRA_GPU_PBRT_OPTIONS STREQUAL "SPECTRA_GPU_PBRT_OPTIONS-NOTFOUND")
    message(FATAL_ERROR "pbrt_gpu_lib did not expose compile options.")
endif ()
if (SPECTRA_GPU_PBRT_LIBRARIES STREQUAL "SPECTRA_GPU_PBRT_LIBRARIES-NOTFOUND")
    message(FATAL_ERROR "pbrt_gpu_lib did not expose link libraries.")
endif ()

add_library(
        spectra_gpu_lib
        STATIC
        ${SPECTRA_GPU_RUNTIME_SOURCE}
        src/pathtracer.cpp
        src/pathtracer.cu
)
add_library(spectra::gpu_lib ALIAS spectra_gpu_lib)
add_dependencies(spectra_gpu_lib pbrt_gpu_soa_generated)
target_compile_definitions(spectra_gpu_lib PUBLIC ${SPECTRA_GPU_PBRT_DEFINITIONS})
target_include_directories(spectra_gpu_lib PUBLIC ${SPECTRA_GPU_PBRT_INCLUDES})
target_compile_options(spectra_gpu_lib PUBLIC ${SPECTRA_GPU_PBRT_OPTIONS})
target_link_libraries(spectra_gpu_lib PRIVATE ${SPECTRA_GPU_PBRT_LIBRARIES})
set_property(TARGET spectra_gpu_lib PROPERTY CUDA_SEPARABLE_COMPILATION ON)
set_property(TARGET spectra_gpu_lib PROPERTY CUDA_RESOLVE_DEVICE_SYMBOLS ON)

add_executable(spectra_gpu_exe src/spectra_gpu.cpp)
add_executable(spectra::gpu ALIAS spectra_gpu_exe)
target_link_libraries(spectra_gpu_exe PRIVATE spectra_gpu_lib Threads::Threads)
set_target_properties(spectra_gpu_exe PROPERTIES OUTPUT_NAME spectra_gpu)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(spectra_gpu_exe PRIVATE -rdynamic "LINKER:--no-as-needed")
endif ()
