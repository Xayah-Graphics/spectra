add_executable(spectra_shader_compiler spectra/core/render/shaders/compiler.cpp)
target_compile_definitions(spectra_shader_compiler PRIVATE NOMINMAX)
target_link_libraries(spectra_shader_compiler PRIVATE spectra::slang)

set(SPECTRA_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
set(SPECTRA_RUNTIME_SHADER_OUTPUT_DIR "${SPECTRA_SHADER_OUTPUT_DIR}/runtime")
set(SPECTRA_PATH_TRACER_SHADER_OUTPUT_DIR "${SPECTRA_SHADER_OUTPUT_DIR}/pathtracer")
set(SPECTRA_SHADER_STAMP "${SPECTRA_SHADER_OUTPUT_DIR}/build.stamp")
set(SPECTRA_PATH_TRACER_ABI_MODULE "${CMAKE_CURRENT_BINARY_DIR}/generated/pathtracer/abi.ixx")
set(SPECTRA_RASTER_ABI_MODULE "${CMAKE_CURRENT_BINARY_DIR}/generated/rasterizer/abi.ixx")
set(SPECTRA_PLUGIN_ABI_MODULE "${CMAKE_CURRENT_BINARY_DIR}/generated/plugin/abi.ixx")
set(SPECTRA_PATH_TRACER_SHADER_ENTRIES_MODULE "${CMAKE_CURRENT_BINARY_DIR}/generated/pathtracer/shader_entries.ixx")

set(SPECTRA_SHADER_RUNTIME_ENTRIES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/entries_runtime.txt")
set(SPECTRA_SHADER_PATH_TRACER_ENTRIES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/entries_pathtracer.txt")
set(SPECTRA_SHADER_EDITOR_ENTRIES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/editor/shaders/entries_editor.txt")
set(SPECTRA_SHADER_ENTRY_FILES "${SPECTRA_SHADER_RUNTIME_ENTRIES}" "${SPECTRA_SHADER_PATH_TRACER_ENTRIES}")

file(
        GLOB
        SPECTRA_SHADER_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/*.slang"
)
list(APPEND SPECTRA_SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/shader_semantics.h")
set(SPECTRA_SHADER_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders")
if (SPECTRA_BUILD_UI)
    file(
            GLOB
            SPECTRA_EDITOR_SHADER_SOURCES
            CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/spectra/editor/shaders/*.slang"
    )
    list(APPEND SPECTRA_SHADER_ENTRY_FILES "${SPECTRA_SHADER_EDITOR_ENTRIES}")
    list(APPEND SPECTRA_SHADER_SOURCES ${SPECTRA_EDITOR_SHADER_SOURCES})
    list(PREPEND SPECTRA_SHADER_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/editor/shaders")
endif ()

add_custom_command(
        OUTPUT "${SPECTRA_SHADER_STAMP}"
        BYPRODUCTS
        "${SPECTRA_PATH_TRACER_ABI_MODULE}"
        "${SPECTRA_RASTER_ABI_MODULE}"
        "${SPECTRA_PLUGIN_ABI_MODULE}"
        "${SPECTRA_PATH_TRACER_SHADER_ENTRIES_MODULE}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SPECTRA_SHADER_OUTPUT_DIR}"
        COMMAND
        "${CMAKE_COMMAND}" -E env --modify "PATH=path_list_prepend:$<TARGET_FILE_DIR:spectra::slang>"
        "$<TARGET_FILE:spectra_shader_compiler>"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/abi.types"
        "${SPECTRA_PATH_TRACER_ABI_MODULE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/raster_abi.types"
        "${SPECTRA_RASTER_ABI_MODULE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/plugin_abi.types"
        "${SPECTRA_PLUGIN_ABI_MODULE}"
        "${SPECTRA_PATH_TRACER_SHADER_ENTRIES_MODULE}"
        "${SPECTRA_SHADER_OUTPUT_DIR}"
        "${SPECTRA_SHADER_RUNTIME_ENTRIES}"
        "${SPECTRA_SHADER_PATH_TRACER_ENTRIES}"
        "${SPECTRA_SHADER_EDITOR_ENTRIES}"
        "${SPECTRA_BUILD_UI}"
        ${SPECTRA_SHADER_INCLUDE_DIRECTORIES}
        "${SPECTRA_NANOVDB_INCLUDE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${SPECTRA_SHADER_STAMP}"
        DEPENDS
        spectra_shader_compiler
        "$<TARGET_FILE:spectra::slang>"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/abi.types"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/raster_abi.types"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/plugin_abi.types"
        ${SPECTRA_SHADER_ENTRY_FILES}
        "${SPECTRA_NANOVDB_INCLUDE_DIR}/nanovdb/PNanoVDB.h"
        ${SPECTRA_SHADER_SOURCES}
        VERBATIM
)
add_custom_target(spectra_shaders DEPENDS "${SPECTRA_SHADER_STAMP}")
set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${SPECTRA_SHADER_OUTPUT_DIR}")
