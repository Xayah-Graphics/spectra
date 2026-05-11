cmake_minimum_required(VERSION 4.3.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
project(spectra VERSION 1.0.0 LANGUAGES CXX CUDA)


# ============================================================================
# C++ Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)


# ============================================================================
# Dependencies
# ============================================================================
find_package(Vulkan 1.4 REQUIRED)
add_subdirectory(vendor/glfw)


# ============================================================================
# Libraries
# ============================================================================
add_library(spectra)
target_sources(
        spectra
        PRIVATE
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
target_link_libraries(spectra PRIVATE Vulkan::Vulkan glfw)
target_include_directories(spectra PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImNodeFlow ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw/include)
set_property(TARGET spectra PROPERTY CXX_MODULE_STD ON)

add_executable(test main.cpp)
target_link_libraries(test PRIVATE spectra)
set_property(TARGET test PROPERTY CXX_MODULE_STD ON)
