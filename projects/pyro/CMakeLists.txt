cmake_minimum_required(VERSION 4.3.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
project(xayah_pyro LANGUAGES CXX CUDA)

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

add_library(xayah_pyro)
add_library(xayah::pyro ALIAS xayah_pyro)
target_sources(
        xayah_pyro
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        pyro.ixx
        PRIVATE
        pyro.cpp
        pyro.cu
)
target_include_directories(xayah_pyro PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xayah_pyro PRIVATE CUDA::cudart CUDA::cublas CUDA::cusparse)
set_property(TARGET xayah_pyro PROPERTY CXX_MODULE_STD ON)

add_executable(xayah_pyro_cli main.cpp)
target_link_libraries(xayah_pyro_cli PRIVATE xayah::pyro)
set_property(TARGET xayah_pyro_cli PROPERTY CXX_MODULE_STD ON)

add_library(xayah_pyro_plugin MODULE plugin.cpp)
target_link_libraries(xayah_pyro_plugin PRIVATE xayah::pyro)
set_property(TARGET xayah_pyro_plugin PROPERTY CXX_MODULE_STD ON)
set_target_properties(
        xayah_pyro_plugin
        PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${SPECTRA_BINARY_ROOT_DIR}/plugins"
        LIBRARY_OUTPUT_DIRECTORY "${SPECTRA_BINARY_ROOT_DIR}/plugins"
)
