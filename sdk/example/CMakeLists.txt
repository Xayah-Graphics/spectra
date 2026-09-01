cmake_minimum_required(VERSION 4.4.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SpectraSDKExample LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_CXX_MODULE_STD ON)
    if (MSVC)
        add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>")
        add_link_options("$<$<LINK_LANGUAGE:CXX>:${CMAKE_CURRENT_BINARY_DIR}/@cmake_cxx_std.lib>")
    endif ()
    find_package(SpectraSDK 2.1.0 CONFIG REQUIRED)
endif ()

spectra_add_provider(
        spectra-sdk-example
        MODULE spectra.sdk.example.module
        TYPE spectra::sdk::example::Module
)
target_sources(
        spectra-sdk-example
        PRIVATE
        FILE_SET provider_module TYPE CXX_MODULES
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/module.ixx"
)
