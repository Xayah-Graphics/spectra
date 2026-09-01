include_guard(GLOBAL)

spectra_require_dependency(onetbb)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_SHARED_LIBS ON)

set(PXR_BUILD_MONOLITHIC ON CACHE BOOL "" FORCE)
set(PXR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PXR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PXR_BUILD_TUTORIALS OFF CACHE BOOL "" FORCE)
set(PXR_BUILD_USD_TOOLS OFF CACHE BOOL "" FORCE)
set(PXR_BUILD_IMAGING OFF CACHE BOOL "" FORCE)
set(PXR_BUILD_USD_IMAGING OFF CACHE BOOL "" FORCE)
set(PXR_BUILD_USD_VALIDATION OFF CACHE BOOL "" FORCE)
set(PXR_BUILD_EXEC OFF CACHE BOOL "" FORCE)
set(PXR_BUILD_USDVIEW OFF CACHE BOOL "" FORCE)
set(PXR_ENABLE_PYTHON_SUPPORT OFF CACHE BOOL "" FORCE)
set(PXR_ENABLE_MATERIALX_SUPPORT OFF CACHE BOOL "" FORCE)
set(PXR_ENABLE_GL_SUPPORT OFF CACHE BOOL "" FORCE)
set(PXR_ENABLE_VULKAN_SUPPORT OFF CACHE BOOL "" FORCE)
set(PXR_ENABLE_OPENVDB_SUPPORT OFF CACHE BOOL "" FORCE)
set(PXR_ENABLE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(CMAKE_CXX_MODULE_STD OFF)
FetchContent_MakeAvailable(openusd)
set(CMAKE_CXX_MODULE_STD ON)
unset(BUILD_SHARED_LIBS)
unset(CMAKE_POLICY_VERSION_MINIMUM)

add_library(spectra_openusd INTERFACE)
add_library(spectra::openusd ALIAS spectra_openusd)
target_link_libraries(spectra_openusd INTERFACE usd_m)
target_include_directories(
        spectra_openusd
        SYSTEM INTERFACE
        "${openusd_SOURCE_DIR}"
        "${openusd_BINARY_DIR}/include"
)
if (LINUX)
    set_target_properties(usd_m tbb PROPERTIES INSTALL_RPATH "$ORIGIN")
endif ()

file(GLOB_RECURSE SPECTRA_OPENUSD_PLUGIN_INFO_FILES CONFIGURE_DEPENDS "${openusd_BINARY_DIR}/pxr/usd/*/plugInfo.json")
set(SPECTRA_OPENUSD_RUNTIME_ROOT "${SPECTRA_BUILD_BIN_DIR}/usd")
set(SPECTRA_OPENUSD_RUNTIME_COMMANDS
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPECTRA_OPENUSD_RUNTIME_ROOT}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/openusd-plugInfo.json" "${SPECTRA_OPENUSD_RUNTIME_ROOT}/plugInfo.json"
)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/openusd-plugInfo.json" "{\n    \"Includes\": [ \"*/resources/\" ]\n}\n")
foreach (plugin_info IN LISTS SPECTRA_OPENUSD_PLUGIN_INFO_FILES)
    cmake_path(GET plugin_info PARENT_PATH plugin_source_directory)
    cmake_path(GET plugin_source_directory FILENAME plugin_name)
    if (plugin_name STREQUAL "codegenTemplates")
        continue()
    endif ()
    list(
            APPEND SPECTRA_OPENUSD_RUNTIME_COMMANDS
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPECTRA_OPENUSD_RUNTIME_ROOT}/${plugin_name}/resources"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${plugin_info}" "${SPECTRA_OPENUSD_RUNTIME_ROOT}/${plugin_name}/resources/plugInfo.json"
    )
    if (EXISTS "${openusd_SOURCE_DIR}/pxr/usd/${plugin_name}/generatedSchema.usda")
        list(APPEND SPECTRA_OPENUSD_RUNTIME_COMMANDS COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${openusd_SOURCE_DIR}/pxr/usd/${plugin_name}/generatedSchema.usda" "${SPECTRA_OPENUSD_RUNTIME_ROOT}/${plugin_name}/resources/generatedSchema.usda")
    endif ()
endforeach ()
add_custom_target(
        spectra_openusd_runtime ALL
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPECTRA_BUILD_BIN_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:usd_m>" "${SPECTRA_BUILD_BIN_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:tbb>" "${SPECTRA_BUILD_BIN_DIR}"
        ${SPECTRA_OPENUSD_RUNTIME_COMMANDS}
        DEPENDS usd_m tbb
        VERBATIM
)
install(
        TARGETS usd_m tbb
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Spectra
        LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Spectra
)
install(
        DIRECTORY "${SPECTRA_OPENUSD_RUNTIME_ROOT}/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/usd"
        COMPONENT Spectra
)
