add_library(spectra_rasterizer)
add_library(spectra::rasterizer ALIAS spectra_rasterizer)

find_program(SLANGC_EXECUTABLE NAMES slangc REQUIRED)
set(SPECTRA_RASTERIZER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(GLOB SPECTRA_RASTERIZER_SHADER_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.slang")
if(NOT SPECTRA_RASTERIZER_SHADER_FILES)
        message(FATAL_ERROR "Spectra rasterizer requires at least one Slang shader in ${CMAKE_CURRENT_SOURCE_DIR}/shaders")
endif()

function(spectra_rasterizer_shader_stage output_variable shader_file shader_name stage)
        set(entry "${stage}_main")
        set(embed_name "spectra_rasterizer_${shader_name}_${stage}_spv")
        set(output_file "${SPECTRA_RASTERIZER_GENERATED_DIR}/spectra_rasterizer_${shader_name}_${stage}_spv.h")
        add_custom_command(
                OUTPUT "${output_file}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPECTRA_RASTERIZER_GENERATED_DIR}"
                COMMAND "${SLANGC_EXECUTABLE}" "${shader_file}" -target spirv -profile glsl_460 -entry "${entry}" -stage "${stage}" -matrix-layout-row-major -source-embed-style u32 -source-embed-name "${embed_name}" -o "${output_file}"
                DEPENDS "${shader_file}"
                VERBATIM
        )
        set("${output_variable}" "${output_file}" PARENT_SCOPE)
endfunction()

set(SPECTRA_RASTERIZER_SHADER_HEADERS)
set(SPECTRA_RASTERIZER_SHADER_AGGREGATE_CONTENT)
foreach(shader_file IN LISTS SPECTRA_RASTERIZER_SHADER_FILES)
        get_filename_component(shader_name "${shader_file}" NAME_WE)
        foreach(stage IN ITEMS vertex fragment)
                spectra_rasterizer_shader_stage(shader_header "${shader_file}" "${shader_name}" "${stage}")
                list(APPEND SPECTRA_RASTERIZER_SHADER_HEADERS "${shader_header}")
                get_filename_component(shader_header_name "${shader_header}" NAME)
                string(APPEND SPECTRA_RASTERIZER_SHADER_AGGREGATE_CONTENT "#include <${shader_header_name}>\n")
        endforeach()
endforeach()

set(SPECTRA_RASTERIZER_SHADER_AGGREGATE_HEADER "${SPECTRA_RASTERIZER_GENERATED_DIR}/spectra_rasterizer_shaders_spv.h")
file(MAKE_DIRECTORY "${SPECTRA_RASTERIZER_GENERATED_DIR}")
file(GENERATE OUTPUT "${SPECTRA_RASTERIZER_SHADER_AGGREGATE_HEADER}" CONTENT "${SPECTRA_RASTERIZER_SHADER_AGGREGATE_CONTENT}")
set_source_files_properties("${SPECTRA_RASTERIZER_SHADER_AGGREGATE_HEADER}" PROPERTIES GENERATED TRUE)

target_sources(
        spectra_rasterizer
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        host.ixx
        renderer.ixx
        scene_runtime.ixx
        PRIVATE
        FILE_SET private_cxx_modules TYPE CXX_MODULES FILES
        math.ixx
        PRIVATE
        renderer.cpp
        scene_runtime.cpp
        "${SPECTRA_RASTERIZER_SHADER_AGGREGATE_HEADER}"
        ${SPECTRA_RASTERIZER_SHADER_HEADERS}
)
target_link_libraries(spectra_rasterizer PUBLIC Vulkan::Vulkan vendor::imgui spectra::scene PRIVATE ${CMAKE_DL_LIBS})
set_property(TARGET spectra_rasterizer PROPERTY CXX_MODULE_STD ON)
target_include_directories(spectra_rasterizer PRIVATE ${SPECTRA_ROOT_DIR}/vendor "${SPECTRA_RASTERIZER_GENERATED_DIR}")
