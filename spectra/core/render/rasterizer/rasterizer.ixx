export module spectra.render.rasterizer;

import spectra.render.rasterizer.compiler;
import spectra.render.types;
import spectra.render.gpu_scene;
import spectra.render.rasterizer.abi;
import spectra.runtime;
import spectra.scene;

import std;
import vulkan;

export namespace spectra::render {
    struct Rasterizer {
        Rasterizer(runtime::VulkanRuntime& runtime, GpuScene& gpu_scene, scene::ResolvedSceneView scene, std::filesystem::path shader_directory);
        ~Rasterizer();

        Rasterizer(const Rasterizer&)            = delete;
        Rasterizer(Rasterizer&&)                 = delete;
        Rasterizer& operator=(const Rasterizer&) = delete;
        Rasterizer& operator=(Rasterizer&&)      = delete;

        void invalidate(scene::SceneChange changes, GpuSceneUpdate gpu_update) noexcept;
        void prepare(scene::ResolvedSceneView scene, const RenderView& view, const vk::raii::CommandBuffer& command_buffer);
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        [[nodiscard]] RenderOutput output() const noexcept;
        [[nodiscard]] DepthBufferView depth_buffer() noexcept;
        void set_display_mode(RasterDisplayMode mode) noexcept;

    private:
        struct {
            runtime::VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            runtime::GpuBuffer primitive_buffer{};
            runtime::GpuBuffer material_buffer{};
            runtime::GpuBuffer material_range_buffer{};
            runtime::GpuBuffer material_term_buffer{};
            runtime::GpuBuffer material_factor_buffer{};
            runtime::GpuBuffer face_material_buffer{};
            runtime::GpuBuffer area_light_buffer{};
            runtime::GpuBuffer light_buffer{};
            runtime::GpuBuffer volume_buffer{};
            runtime::GpuBuffer zero_volume_field_buffer{};
            std::array<runtime::GpuBuffer, 9> texture_buffers{};
            runtime::GpuBuffer scene_binding_buffer{};
            runtime::DescriptorLease material_descriptor{};
            runtime::DescriptorLease material_range_descriptor{};
            runtime::DescriptorLease material_term_descriptor{};
            runtime::DescriptorLease material_factor_descriptor{};
            runtime::DescriptorLease face_material_descriptor{};
            runtime::DescriptorLease area_light_descriptor{};
            runtime::DescriptorLease light_descriptor{};
            runtime::DescriptorLease zero_volume_field_descriptor{};
            std::array<runtime::DescriptorLease, 9> texture_descriptors{};
            scene::SceneRevision uploaded_revision{};
            runtime::DescriptorLease primitives_descriptor{};
            runtime::DescriptorLease bindings_descriptor{};
            runtime::DescriptorLease volumes_descriptor{};
            std::uint32_t light_count{};
            std::uint32_t volume_count{};
            math::Float3 environment_radiance{};
            std::vector<RasterAreaEmitterRange> area_emitters{};
            vk::raii::ShaderEXT area_emission_shader{nullptr};
        } scene;

        struct {
            scene::Camera camera{};
            float film_exposure{};
            float film_iso{100.0f};
            scene::SpectrumColorSpace film_color_space{scene::SpectrumColorSpace::Srgb};
            std::optional<float> film_maximum_component_value{};
            std::array<std::uint32_t, 2> film_resolution{};
            std::array<std::uint32_t, 2> film_pixel_minimum{};
            std::array<std::uint32_t, 2> film_pixel_maximum{};
            RasterDisplayMode display_mode{RasterDisplayMode::Material};
            vk::raii::ShaderEXTs shaders{nullptr};
            vk::raii::ShaderEXT sphere_shader{nullptr};
            vk::raii::ShaderEXT volume_shader{nullptr};
            runtime::GpuImage output_image{};
            runtime::GpuImage depth_image{};
            runtime::DescriptorLease sampled_output_descriptor{};
            runtime::DescriptorLease storage_output_descriptor{};
            runtime::DescriptorLease sampled_depth_descriptor{};
            vk::ImageLayout output_layout{vk::ImageLayout::eUndefined};
            vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
            scene::SceneChange pending_changes{scene::SceneChange::None};
            GpuSceneChange pending_gpu_changes{GpuSceneChange::None};
        } renderer;

        void initialize_scene(scene::ResolvedSceneView scene);
        void upload_scene(scene::ResolvedSceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void update_area_emitters(const vk::raii::CommandBuffer& command_buffer);
        void synchronize_scene(scene::ResolvedSceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void initialize_renderer();
        void create_shaders();
        void create_output(vk::Extent2D extent);
        void record_commands(const vk::raii::CommandBuffer& command_buffer);
        void destroy() noexcept;
    };

} // namespace spectra::render
