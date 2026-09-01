add_executable(spectra_shader_compiler spectra/core/render/shaders/compiler.cpp)
target_compile_definitions(spectra_shader_compiler PRIVATE NOMINMAX)
target_link_libraries(spectra_shader_compiler PRIVATE spectra::slang)

set(SPECTRA_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
set(SPECTRA_RENDER_SHADER_OUTPUT_DIR "${SPECTRA_SHADER_OUTPUT_DIR}/render")
set(SPECTRA_PATH_TRACER_SHADER_OUTPUT_DIR "${SPECTRA_SHADER_OUTPUT_DIR}/pathtracer")
set(SPECTRA_SHADER_STAMP "${SPECTRA_SHADER_OUTPUT_DIR}/build.stamp")
set(SPECTRA_PATH_TRACER_ABI_MODULE "${CMAKE_CURRENT_BINARY_DIR}/generated/pathtracer/abi.ixx")
set(SPECTRA_RASTER_ABI_MODULE "${CMAKE_CURRENT_BINARY_DIR}/generated/rasterizer/abi.ixx")
set(SPECTRA_RENDER_ABI_MODULE "${CMAKE_CURRENT_BINARY_DIR}/generated/render/abi.ixx")
set(SPECTRA_EDITOR_ABI_MODULE "${CMAKE_CURRENT_BINARY_DIR}/generated/editor/abi.ixx")
set(SPECTRA_PATH_TRACER_SHADER_ENTRIES_MODULE "${CMAKE_CURRENT_BINARY_DIR}/generated/pathtracer/shader_entries.ixx")

set(SPECTRA_SHADER_RENDER_ENTRIES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/entries_render.txt")
set(SPECTRA_SHADER_PATH_TRACER_ENTRIES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/entries_pathtracer.txt")
set(SPECTRA_SHADER_EDITOR_ENTRIES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/editor/shaders/entries_editor.txt")
set(SPECTRA_SHADER_ENTRY_FILES "${SPECTRA_SHADER_RENDER_ENTRIES}" "${SPECTRA_SHADER_PATH_TRACER_ENTRIES}")

function(spectra_collect_shader_spv_outputs entry_file output_variable)
    file(STRINGS "${entry_file}" shader_entries)
    set(shader_outputs)
    foreach (shader_entry IN LISTS shader_entries)
        string(STRIP "${shader_entry}" shader_entry)
        if (shader_entry STREQUAL "" OR shader_entry MATCHES "^#")
            continue()
        endif ()
        separate_arguments(shader_entry_fields UNIX_COMMAND "${shader_entry}")
        list(GET shader_entry_fields 0 shader_output_name)
        list(GET shader_entry_fields 4 shader_category)
        if (shader_category MATCHES "^path-")
            list(APPEND shader_outputs "${SPECTRA_PATH_TRACER_SHADER_OUTPUT_DIR}/${shader_output_name}.spv")
        elseif (shader_category STREQUAL "render" OR shader_category STREQUAL "editor")
            list(APPEND shader_outputs "${SPECTRA_RENDER_SHADER_OUTPUT_DIR}/${shader_output_name}.spv")
        else ()
            message(FATAL_ERROR "Unknown shader category '${shader_category}' in ${entry_file}")
        endif ()
    endforeach ()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${entry_file}")
    set(${output_variable} ${shader_outputs} PARENT_SCOPE)
endfunction()

spectra_collect_shader_spv_outputs("${SPECTRA_SHADER_RENDER_ENTRIES}" SPECTRA_RENDER_SHADER_SPV_OUTPUTS)
spectra_collect_shader_spv_outputs("${SPECTRA_SHADER_PATH_TRACER_ENTRIES}" SPECTRA_PATH_TRACER_SHADER_SPV_OUTPUTS)
set(SPECTRA_SHADER_SPV_OUTPUTS ${SPECTRA_RENDER_SHADER_SPV_OUTPUTS} ${SPECTRA_PATH_TRACER_SHADER_SPV_OUTPUTS})
set(SPECTRA_SHADER_ABI_OUTPUTS "${SPECTRA_PATH_TRACER_ABI_MODULE}" "${SPECTRA_RASTER_ABI_MODULE}" "${SPECTRA_RENDER_ABI_MODULE}")
set(SPECTRA_SHADER_ABI_TYPES
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/abi.types"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/raster_abi.types"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/render_abi.types"
)

file(
        GLOB
        SPECTRA_SHADER_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/*.slang"
)
list(APPEND SPECTRA_SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/shader_semantics.h")
list(APPEND SPECTRA_SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/sdk/include/spectra/sdk/neural_field_layout.h")
set(SPECTRA_SHADER_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders" "${CMAKE_CURRENT_SOURCE_DIR}/sdk/include" "${openvdb_SOURCE_DIR}/nanovdb")
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
    spectra_collect_shader_spv_outputs("${SPECTRA_SHADER_EDITOR_ENTRIES}" SPECTRA_EDITOR_SHADER_SPV_OUTPUTS)
    list(APPEND SPECTRA_SHADER_SPV_OUTPUTS ${SPECTRA_EDITOR_SHADER_SPV_OUTPUTS})
    list(APPEND SPECTRA_SHADER_ABI_OUTPUTS "${SPECTRA_EDITOR_ABI_MODULE}")
    list(APPEND SPECTRA_SHADER_ABI_TYPES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/editor/shaders/shader_abi.types")
endif ()

set(SPECTRA_SHADER_INCLUDE_ARGUMENTS)
foreach (shader_include_directory IN LISTS SPECTRA_SHADER_INCLUDE_DIRECTORIES)
    list(APPEND SPECTRA_SHADER_INCLUDE_ARGUMENTS --include "${shader_include_directory}")
endforeach ()

add_custom_command(
        OUTPUT "${SPECTRA_SHADER_STAMP}"
        BYPRODUCTS
        ${SPECTRA_SHADER_ABI_OUTPUTS}
        "${SPECTRA_PATH_TRACER_SHADER_ENTRIES_MODULE}"
        ${SPECTRA_SHADER_SPV_OUTPUTS}
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SPECTRA_SHADER_OUTPUT_DIR}"
        COMMAND
        "${CMAKE_COMMAND}" -E env --modify "PATH=path_list_prepend:$<TARGET_FILE_DIR:spectra::slang>"
        "$<TARGET_FILE:spectra_shader_compiler>"
        --path-abi-types "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/abi.types"
        --path-abi-module "${SPECTRA_PATH_TRACER_ABI_MODULE}"
        --raster-abi-types "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/raster_abi.types"
        --raster-abi-module "${SPECTRA_RASTER_ABI_MODULE}"
        --render-abi-types "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/render_abi.types"
        --render-abi-module "${SPECTRA_RENDER_ABI_MODULE}"
        --editor-abi-types "${CMAKE_CURRENT_SOURCE_DIR}/spectra/editor/shaders/shader_abi.types"
        --editor-abi-module "${SPECTRA_EDITOR_ABI_MODULE}"
        --path-entries-module "${SPECTRA_PATH_TRACER_SHADER_ENTRIES_MODULE}"
        --output-directory "${SPECTRA_SHADER_OUTPUT_DIR}"
        --render-entries "${SPECTRA_SHADER_RENDER_ENTRIES}"
        --path-entries "${SPECTRA_SHADER_PATH_TRACER_ENTRIES}"
        --editor-entries "${SPECTRA_SHADER_EDITOR_ENTRIES}"
        --build-editor "${SPECTRA_BUILD_UI}"
        ${SPECTRA_SHADER_INCLUDE_ARGUMENTS}
        COMMAND "${CMAKE_COMMAND}" -E touch "${SPECTRA_SHADER_STAMP}"
        DEPENDS
        spectra_shader_compiler
        "$<TARGET_FILE:spectra::slang>"
        ${SPECTRA_SHADER_ABI_TYPES}
        ${SPECTRA_SHADER_ENTRY_FILES}
        ${SPECTRA_SHADER_SOURCES}
        VERBATIM
)
add_custom_target(spectra_shaders DEPENDS "${SPECTRA_SHADER_STAMP}")
set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${SPECTRA_SHADER_OUTPUT_DIR}")
