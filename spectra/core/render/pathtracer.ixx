export module spectra.render.pathtracer;

import spectra.render.types;
import spectra.render.gpu_scene;
import spectra.render.pathtracer.resources;
import spectra.render.pathtracer.compiler;
import spectra.runtime;
import spectra.scene;

import std;
import vulkan;

export namespace spectra::render {
    struct PathTracer {
        PathTracer(runtime::VulkanRuntime& runtime, GpuScene& gpu_scene, PathTracerResources& pathtracer, scene::ResolvedSceneView scene);
        ~PathTracer();

        PathTracer(const PathTracer&)            = delete;
        PathTracer(PathTracer&&)                 = delete;
        PathTracer& operator=(const PathTracer&) = delete;
        PathTracer& operator=(PathTracer&&)      = delete;

        [[nodiscard]] bool complete_preparation(scene::ResolvedSceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void wait_for_preparation();
        [[nodiscard]] PathTracerPreparationProgress preparation_progress() const;
        void invalidate(scene::SceneChange changes, GpuSceneUpdate gpu_update) noexcept;
        void prepare(scene::ResolvedSceneView scene, const RenderView& view, const vk::raii::CommandBuffer& command_buffer);
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        [[nodiscard]] RenderProgress progress() const noexcept;
        void set_paused(bool paused) noexcept;
        void reset() noexcept;
        [[nodiscard]] RenderGBufferReadback readback();
        [[nodiscard]] RenderOutput output() const noexcept;
        [[nodiscard]] DepthBufferView depth_buffer() noexcept;

    private:
        struct {
            runtime::VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            PathTracerResources& pathtracer;
        } context;

        struct {
            PathBufferSlice primitives{};
            std::array<PathBufferSlice, static_cast<std::size_t>(PathMaterialTable::Count)> materials{};
            std::array<PathBufferSlice, static_cast<std::size_t>(PathTextureTable::Count)> textures{};
            PathBufferSlice light_table{};
            PathBufferSlice light_shapes{};
            PathBufferSlice light_distribution{};
            PathBufferSlice light_distribution_data{};
            PathBufferSlice portals{};
            PathBufferSlice light_bvh_nodes{};
            PathBufferSlice light_bvh_bit_trails{};
            runtime::GpuBuffer light_bvh_counters{};
            runtime::DescriptorLease light_bvh_counters_descriptor{};
            PathBufferSlice face_materials{};
            PathBufferSlice media{};
            PathBufferSlice volumes{};
            std::vector<PathVolumeResources> volume_resources{};
            PathBufferSlice spectra{};
            PathBufferSlice piecewise_spectra{};
            runtime::GpuBuffer arena{};
            GpuBufferBinding bindings{};
            PathFilterResources filter{};
            PathSamplerResources sampler{};
            std::uint32_t light_count{};
            std::uint32_t light_bvh_node_count{};
            std::uint32_t texture_stack_size{1};
            std::uint32_t material_texture_value_count{1};
            std::uint32_t camera_medium_index{std::numeric_limits<std::uint32_t>::max()};
            scene::Camera camera{};
            scene::TransportSettings transport_settings{};
            math::Bounds3 compiled_bounds{};
            std::vector<math::Transform> compiled_instance_transforms{};
            scene::SceneRevision compiled_revision{};
            std::uint64_t compiled_gpu_structure_revision{};
            std::vector<PathDynamicAreaLightRange> dynamic_area_lights{};
            bool initialized{};
        } scene;

        struct {
            vk::Extent2D render_extent{};
            std::uint64_t pixel_capacity{};
            runtime::GpuImage output_image{};
            runtime::DescriptorLease output_descriptor{};
            runtime::DescriptorLease sampled_output_descriptor{};
            vk::ImageLayout output_layout{vk::ImageLayout::eUndefined};
            runtime::GpuImage depth_image{};
            runtime::DescriptorLease storage_depth_descriptor{};
            runtime::DescriptorLease sampled_depth_descriptor{};
            vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
            PathBufferSlice queue_counts{};
            PathBufferSlice ray_queue_0{};
            PathBufferSlice ray_queue_1{};
            PathBufferSlice ray_origins{};
            PathBufferSlice ray_directions{};
            PathBufferSlice ray_origin_dx{};
            PathBufferSlice ray_origin_dy{};
            PathBufferSlice ray_direction_dx{};
            PathBufferSlice ray_direction_dy{};
            PathBufferSlice throughputs{};
            PathBufferSlice radiances{};
            PathBufferSlice wavelengths{};
            PathBufferSlice wavelength_pdfs{};
            PathBufferSlice r_u{};
            PathBufferSlice r_l{};
            PathBufferSlice current_media{};
            PathBufferSlice light_context_normals{};
            PathBufferSlice flags{};
            PathBufferSlice eta_scales{};
            PathBufferSlice hit_normal_distances{};
            PathBufferSlice hit_geometric_normal_u{};
            PathBufferSlice hit_tangent_v{};
            PathBufferSlice hit_dpdu{};
            PathBufferSlice hit_dpdv{};
            PathBufferSlice hit_dndu{};
            PathBufferSlice hit_dndv{};
            PathBufferSlice hit_identifiers{};
            PathBufferSlice shadow_path_ids{};
            PathBufferSlice shadow_origins{};
            PathBufferSlice shadow_directions{};
            PathBufferSlice shadow_contributions{};
            PathBufferSlice shadow_r_p{};
            PathBufferSlice shadow_pdfs{};
            PathBufferSlice shadow_media{};
            PathBufferSlice texture_evaluation_stack{};
            PathBufferSlice evaluated_texture_values{};
            PathBufferSlice filter_weights{};
            PathBufferSlice film_rgb_sums{};
            PathBufferSlice film_weight_sums{};
            PathBufferSlice gbuffer_sample_albedo{};
            PathBufferSlice gbuffer_sample_shading_normal{};
            PathBufferSlice gbuffer_sample_geometric_normal{};
            PathBufferSlice gbuffer_sample_position_depth{};
            PathBufferSlice gbuffer_sample_uv{};
            PathBufferSlice gbuffer_sample_identity_0{};
            PathBufferSlice gbuffer_sample_identity_1{};
            PathBufferSlice gbuffer_albedo_sums{};
            PathBufferSlice gbuffer_shading_normal_sums{};
            PathBufferSlice gbuffer_geometric_normal_sums{};
            PathBufferSlice gbuffer_position_depth_sums{};
            PathBufferSlice gbuffer_uv_weight_sums{};
            PathBufferSlice gbuffer_identity_0{};
            PathBufferSlice gbuffer_identity_1{};
            runtime::GpuBuffer arena{};
            runtime::GpuBuffer indirect_commands{};
            GpuBufferBinding bindings{};
            std::vector<GpuBufferBinding> parameters{};
            std::uint32_t texture_stack_size{};
            std::uint32_t texture_value_count{};
            bool indirect_commands_configured{};
            std::uint32_t sample_index{};
            bool initialized{};
        } session;

        struct {
            scene::SceneChange pending_changes{scene::SceneChange::None};
            GpuSceneChange pending_gpu_changes{GpuSceneChange::None};
            std::uint64_t pending_gpu_structure_revision{};
            bool paused{};
            std::uint64_t camera_revision{};
        } control;

        std::shared_ptr<PathSceneCompilation> compilation{};
        std::shared_future<void> compilation_task{};

        void begin_compilation(scene::ResolvedSceneView scene);
        void release_compilation() noexcept;
        void destroy_scene() noexcept;
        void commit_filter(std::unique_ptr<CompiledPathFilter> compiled, const vk::raii::CommandBuffer& command_buffer);
        void compile_filter(const scene::Film& film, const vk::raii::CommandBuffer& command_buffer);
        void commit_sampler(std::unique_ptr<CompiledPathSampler> compiled, const vk::raii::CommandBuffer& command_buffer);
        void compile_sampler(const scene::Sampler& sampler, const vk::raii::CommandBuffer& command_buffer);
        void commit_scene(std::unique_ptr<CompiledPathScene> compiled, const vk::raii::CommandBuffer& command_buffer);
        void compile_scene(scene::ResolvedSceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void update_volumes(scene::ResolvedSceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void update_dynamic_lights(const vk::raii::CommandBuffer& command_buffer);
        void synchronize_scene(scene::ResolvedSceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void initialize_session();
        void destroy_session() noexcept;
        void resize_session(vk::Extent2D extent, std::uint32_t texture_evaluation_stack_size, std::uint32_t material_texture_value_count, const vk::raii::CommandBuffer& command_buffer);
        void configure_indirect_commands();
        void record_integrator(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
    };

} // namespace spectra::render
