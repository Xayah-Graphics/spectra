cmake_minimum_required(VERSION 4.3.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
project(xayah_cloth LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)
set(CMAKE_CUDA_ARCHITECTURES 120)

find_package(CUDAToolkit REQUIRED)

add_library(xayah_cloth)
add_library(xayah::cloth ALIAS xayah_cloth)
target_sources(
        xayah_cloth
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        cloth.ixx
        PRIVATE
        cloth.cpp
        cloth.cu
)
target_include_directories(xayah_cloth PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xayah_cloth PRIVATE CUDA::cudart)
set_property(TARGET xayah_cloth PROPERTY CXX_MODULE_STD ON)

add_executable(xayah_cloth_cli main.cpp)
target_link_libraries(xayah_cloth_cli PRIVATE xayah::cloth)
set_property(TARGET xayah_cloth_cli PROPERTY CXX_MODULE_STD ON)

add_library(xayah_cloth_plugin MODULE plugin.cpp)
target_link_libraries(xayah_cloth_plugin PRIVATE xayah::cloth)
set_property(TARGET xayah_cloth_plugin PROPERTY CXX_MODULE_STD ON)
set_target_properties(
        xayah_cloth_plugin
        PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${SPECTRA_BINARY_ROOT_DIR}/plugins"
        LIBRARY_OUTPUT_DIRECTORY "${SPECTRA_BINARY_ROOT_DIR}/plugins"
)
