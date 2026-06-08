add_library(spectra_rasterizer)
add_library(spectra::rasterizer ALIAS spectra_rasterizer)

find_program(SLANGC_EXECUTABLE NAMES slangc REQUIRED)
set(SPECTRA_RASTERIZER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
function(spectra_rasterizer_shader output_variable shader_name stage entry embed_name)
        set(shader_file "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader_name}.slang")
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

spectra_rasterizer_shader(SPECTRA_RASTERIZER_MESH_VERTEX_HEADER mesh vertex vertex_main spectra_rasterizer_mesh_vertex_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_MESH_FRAGMENT_HEADER mesh fragment fragment_main spectra_rasterizer_mesh_fragment_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_MESH_PICK_VERTEX_HEADER mesh_pick vertex vertex_main spectra_rasterizer_mesh_pick_vertex_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_MESH_PICK_FRAGMENT_HEADER mesh_pick fragment fragment_main spectra_rasterizer_mesh_pick_fragment_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_MESH_SELECTION_MASK_VERTEX_HEADER mesh_selection_mask vertex vertex_main spectra_rasterizer_mesh_selection_mask_vertex_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_MESH_SELECTION_MASK_FRAGMENT_HEADER mesh_selection_mask fragment fragment_main spectra_rasterizer_mesh_selection_mask_fragment_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_PARTICLE_VERTEX_HEADER particle vertex vertex_main spectra_rasterizer_particle_vertex_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_PARTICLE_FRAGMENT_HEADER particle fragment fragment_main spectra_rasterizer_particle_fragment_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_PARTICLE_PICK_VERTEX_HEADER particle_pick vertex vertex_main spectra_rasterizer_particle_pick_vertex_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_PARTICLE_PICK_FRAGMENT_HEADER particle_pick fragment fragment_main spectra_rasterizer_particle_pick_fragment_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_PARTICLE_SELECTION_MASK_VERTEX_HEADER particle_selection_mask vertex vertex_main spectra_rasterizer_particle_selection_mask_vertex_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_PARTICLE_SELECTION_MASK_FRAGMENT_HEADER particle_selection_mask fragment fragment_main spectra_rasterizer_particle_selection_mask_fragment_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_VOLUME_VERTEX_HEADER volume vertex vertex_main spectra_rasterizer_volume_vertex_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_VOLUME_FRAGMENT_HEADER volume fragment fragment_main spectra_rasterizer_volume_fragment_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_VOLUME_PICK_VERTEX_HEADER volume_pick vertex vertex_main spectra_rasterizer_volume_pick_vertex_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_VOLUME_PICK_FRAGMENT_HEADER volume_pick fragment fragment_main spectra_rasterizer_volume_pick_fragment_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_VOLUME_SELECTION_MASK_VERTEX_HEADER volume_selection_mask vertex vertex_main spectra_rasterizer_volume_selection_mask_vertex_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_VOLUME_SELECTION_MASK_FRAGMENT_HEADER volume_selection_mask fragment fragment_main spectra_rasterizer_volume_selection_mask_fragment_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_VIEWPORT_GRID_VERTEX_HEADER viewport_grid vertex vertex_main spectra_rasterizer_viewport_grid_vertex_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_VIEWPORT_GRID_FRAGMENT_HEADER viewport_grid fragment fragment_main spectra_rasterizer_viewport_grid_fragment_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_SELECTION_OUTLINE_VERTEX_HEADER selection_outline vertex vertex_main spectra_rasterizer_selection_outline_vertex_spv)
spectra_rasterizer_shader(SPECTRA_RASTERIZER_SELECTION_OUTLINE_FRAGMENT_HEADER selection_outline fragment fragment_main spectra_rasterizer_selection_outline_fragment_spv)
target_sources(
        spectra_rasterizer
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        scene.ixx
        host.ixx
        renderer.ixx
        PRIVATE
        FILE_SET private_cxx_modules TYPE CXX_MODULES FILES
        math.ixx
        PRIVATE
        scene.cpp
        renderer.cpp
        "${SPECTRA_RASTERIZER_MESH_VERTEX_HEADER}"
        "${SPECTRA_RASTERIZER_MESH_FRAGMENT_HEADER}"
        "${SPECTRA_RASTERIZER_MESH_PICK_VERTEX_HEADER}"
        "${SPECTRA_RASTERIZER_MESH_PICK_FRAGMENT_HEADER}"
        "${SPECTRA_RASTERIZER_MESH_SELECTION_MASK_VERTEX_HEADER}"
        "${SPECTRA_RASTERIZER_MESH_SELECTION_MASK_FRAGMENT_HEADER}"
        "${SPECTRA_RASTERIZER_PARTICLE_VERTEX_HEADER}"
        "${SPECTRA_RASTERIZER_PARTICLE_FRAGMENT_HEADER}"
        "${SPECTRA_RASTERIZER_PARTICLE_PICK_VERTEX_HEADER}"
        "${SPECTRA_RASTERIZER_PARTICLE_PICK_FRAGMENT_HEADER}"
        "${SPECTRA_RASTERIZER_PARTICLE_SELECTION_MASK_VERTEX_HEADER}"
        "${SPECTRA_RASTERIZER_PARTICLE_SELECTION_MASK_FRAGMENT_HEADER}"
        "${SPECTRA_RASTERIZER_VOLUME_VERTEX_HEADER}"
        "${SPECTRA_RASTERIZER_VOLUME_FRAGMENT_HEADER}"
        "${SPECTRA_RASTERIZER_VOLUME_PICK_VERTEX_HEADER}"
        "${SPECTRA_RASTERIZER_VOLUME_PICK_FRAGMENT_HEADER}"
        "${SPECTRA_RASTERIZER_VOLUME_SELECTION_MASK_VERTEX_HEADER}"
        "${SPECTRA_RASTERIZER_VOLUME_SELECTION_MASK_FRAGMENT_HEADER}"
        "${SPECTRA_RASTERIZER_VIEWPORT_GRID_VERTEX_HEADER}"
        "${SPECTRA_RASTERIZER_VIEWPORT_GRID_FRAGMENT_HEADER}"
        "${SPECTRA_RASTERIZER_SELECTION_OUTLINE_VERTEX_HEADER}"
        "${SPECTRA_RASTERIZER_SELECTION_OUTLINE_FRAGMENT_HEADER}"
)
target_link_libraries(spectra_rasterizer PUBLIC Vulkan::Vulkan vendor::imgui)
set_property(TARGET spectra_rasterizer PROPERTY CXX_MODULE_STD ON)
target_include_directories(spectra_rasterizer PRIVATE ${SPECTRA_ROOT_DIR}/vendor "${SPECTRA_RASTERIZER_GENERATED_DIR}")
