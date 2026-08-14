cmake_minimum_required(VERSION 4.4.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
set(SPECTRA_SDK_VERSION 1.0.0)
project(SpectraSDK VERSION ${SPECTRA_SDK_VERSION} LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "Spectra SDK CUDA Providers are supported only on Windows")
endif ()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_MODULE_STD ON)
if (MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>")
endif ()
if (MSVC AND PROJECT_IS_TOP_LEVEL)
    add_link_options("$<$<LINK_LANGUAGE:CXX>:${CMAKE_CURRENT_BINARY_DIR}/@cmake_cxx_std.lib>")
endif ()
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Spectra SDK installation directory" FORCE)
endif ()

include(GNUInstallDirs)
find_package(CUDAToolkit 13 REQUIRED)

add_library(spectra_sdk STATIC)
add_library(SpectraSDK::sdk ALIAS spectra_sdk)
set_target_properties(spectra_sdk PROPERTIES EXPORT_NAME sdk POSITION_INDEPENDENT_CODE ON)
target_sources(
        spectra_sdk
        PUBLIC
        FILE_SET public_headers TYPE HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/spectra/sdk/cuda_types.h"
        PUBLIC
        FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/modules"
        FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/sdk.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/cuda.ixx"
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda.cpp"
)
target_include_directories(spectra_sdk PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/internal")
target_link_libraries(spectra_sdk PRIVATE CUDA::cudart)

set(SPECTRA_SDK_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/SpectraSDK")
set(SPECTRA_SDK_INTERNAL_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/spectra-sdk/internal")

include(CMakePackageConfigHelpers)
configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpectraSDKConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/SpectraSDKConfig.cmake"
        INSTALL_DESTINATION "${SPECTRA_SDK_CMAKE_INSTALL_DIR}"
)
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/SpectraSDKConfigVersion.cmake"
        VERSION "${SPECTRA_SDK_VERSION}"
        COMPATIBILITY SameMajorVersion
)

install(
        TARGETS spectra_sdk
        EXPORT SpectraSDKTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT SDK
        FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        COMPONENT SDK
        FILE_SET public_modules DESTINATION "${CMAKE_INSTALL_DATADIR}/spectra-sdk/modules"
        COMPONENT SDK
)
install(
        FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/internal/abi.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/internal/bridge.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/internal/provider_entry.cpp.in"
        DESTINATION "${SPECTRA_SDK_INTERNAL_INSTALL_DIR}"
        COMPONENT SDK
)
install(
        EXPORT SpectraSDKTargets
        FILE SpectraSDKTargets.cmake
        NAMESPACE SpectraSDK::
        DESTINATION "${SPECTRA_SDK_CMAKE_INSTALL_DIR}"
        COMPONENT SDK
)
install(
        FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpectraSDKProvider.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/SpectraSDKConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/SpectraSDKConfigVersion.cmake"
        DESTINATION "${SPECTRA_SDK_CMAKE_INSTALL_DIR}"
        COMPONENT SDK
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/GUIDE.md" DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/SpectraSDK" COMPONENT SDK)
install(
        FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/example/CMakeLists.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/example/provider.ixx"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/SpectraSDK/example"
        COMPONENT SDK
)

set(SpectraSDK_INTERNAL_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/internal")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpectraSDKProvider.cmake")

option(SPECTRA_SDK_BUILD_EXAMPLE "Build the Spectra SDK minimal Provider" OFF)
if (SPECTRA_SDK_BUILD_EXAMPLE)
    add_subdirectory(example)
endif ()
