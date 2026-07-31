cmake_minimum_required(VERSION 4.3.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
project(xayah_sparkles LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

add_library(xayah_sparkles)
add_library(xayah::sparkles ALIAS xayah_sparkles)
target_sources(
        xayah_sparkles
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        sparkles.ixx
        PRIVATE
        sparkles.cpp
)
set_property(TARGET xayah_sparkles PROPERTY CXX_MODULE_STD ON)

add_executable(xayah_sparkles_cli main.cpp)
target_link_libraries(xayah_sparkles_cli PRIVATE xayah::sparkles)
set_property(TARGET xayah_sparkles_cli PROPERTY CXX_MODULE_STD ON)

add_library(xayah_sparkles_plugin MODULE plugin.cpp)
target_link_libraries(xayah_sparkles_plugin PRIVATE xayah::sparkles)
set_property(TARGET xayah_sparkles_plugin PROPERTY CXX_MODULE_STD ON)
set_target_properties(
        xayah_sparkles_plugin
        PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${SPECTRA_BINARY_ROOT_DIR}/plugins"
        LIBRARY_OUTPUT_DIRECTORY "${SPECTRA_BINARY_ROOT_DIR}/plugins"
)
