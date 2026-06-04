cmake_minimum_required(VERSION 4.3.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
project(spectra VERSION 1.0.0 LANGUAGES C CXX CUDA)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)
set(CMAKE_CUDA_ARCHITECTURES 120)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type" FORCE)
endif ()

set(SPECTRA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SPECTRA_BINARY_ROOT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(SPECTRA_PBRT_SCENE_ROOT "${SPECTRA_ROOT_DIR}/scene")

find_package(Vulkan 1.4 REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory(vendor/glfw)
add_subdirectory(vendor/pbrt)

add_library(vendor_imgui)
add_library(vendor::imgui ALIAS vendor_imgui)
target_sources(
        vendor_imgui
        PRIVATE
        vendor/imgui/imgui.cpp
        vendor/imgui/imgui_draw.cpp
        vendor/imgui/imgui_tables.cpp
        vendor/imgui/imgui_widgets.cpp
        vendor/imgui/backends/imgui_impl_glfw.cpp
        vendor/imgui/backends/imgui_impl_vulkan.cpp
)
target_link_libraries(vendor_imgui PUBLIC Vulkan::Vulkan PRIVATE glfw)
target_include_directories(vendor_imgui PUBLIC ${SPECTRA_ROOT_DIR}/vendor/imgui)

add_subdirectory(spectra)
add_subdirectory(pathtracer)
add_subdirectory(rasterizer)
add_subdirectory(projects)

add_executable(spectra_gui main.cpp)
add_executable(spectra::gui ALIAS spectra_gui)
target_link_libraries(
        spectra_gui
        PRIVATE
        spectra::core
        spectra::pathtracer
        spectra::rasterizer
        xayah::bouncing_ball
        xayah::sparkles
        xayah::cloth
        xayah::pyro
)
set_property(TARGET spectra_gui PROPERTY CXX_MODULE_STD ON)
