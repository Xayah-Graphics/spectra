cmake_minimum_required(VERSION 4.4.0)
set(SPECTRA_PLUGIN_SDK_VERSION 21.0.0)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SpectraPluginSDK VERSION ${SPECTRA_PLUGIN_SDK_VERSION} LANGUAGES NONE)
    if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Spectra Plugin SDK installation directory" FORCE)
    endif ()
endif ()
include(GNUInstallDirs)

set(SPECTRA_PLUGIN_SDK_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/SpectraPluginSDK")

add_library(spectra_plugin_contract INTERFACE)
add_library(spectra::plugin_contract ALIAS spectra_plugin_contract)
set_target_properties(spectra_plugin_contract PROPERTIES EXPORT_NAME contract)
target_include_directories(
        spectra_plugin_contract
        INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpectraPluginSDKConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/SpectraPluginSDKConfig.cmake"
        INSTALL_DESTINATION "${SPECTRA_PLUGIN_SDK_CMAKE_INSTALL_DIR}"
        NO_SET_AND_CHECK_MACRO
        NO_CHECK_REQUIRED_COMPONENTS_MACRO
)
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/SpectraPluginSDKConfigVersion.cmake"
        VERSION "${SPECTRA_PLUGIN_SDK_VERSION}"
        COMPATIBILITY SameMajorVersion
)
install(
        FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/include/spectra/plugin_api.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/spectra"
        COMPONENT PluginSDK
)
install(
        TARGETS spectra_plugin_contract
        EXPORT SpectraPluginSDKTargets
        COMPONENT PluginSDK
)
export(
        EXPORT SpectraPluginSDKTargets
        FILE "${CMAKE_CURRENT_BINARY_DIR}/SpectraPluginSDKTargets.cmake"
        NAMESPACE SpectraPluginSDK::
)
install(
        EXPORT SpectraPluginSDKTargets
        FILE SpectraPluginSDKTargets.cmake
        NAMESPACE SpectraPluginSDK::
        DESTINATION "${SPECTRA_PLUGIN_SDK_CMAKE_INSTALL_DIR}"
        COMPONENT PluginSDK
)
install(
        FILES
        "${CMAKE_CURRENT_BINARY_DIR}/SpectraPluginSDKConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/SpectraPluginSDKConfigVersion.cmake"
        DESTINATION "${SPECTRA_PLUGIN_SDK_CMAKE_INSTALL_DIR}"
        COMPONENT PluginSDK
)
