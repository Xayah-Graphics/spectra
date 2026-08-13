export module spectra.render.rasterizer;

import spectra.render.contract;
import spectra.render.gpu_scene;
import spectra.render.rasterizer.abi;
import spectra.runtime;
import spectra.scene;

import std;
import vulkan;

namespace spectra {
    struct RasterAreaEmitterRange {
        std::uint32_t scene_primitive_index{};
        std::uint32_t resource_index{};
        std::uint32_t kind{};
        std::uint32_t geometry_kind{};
        std::array<float, 4> geometry_parameters{};
        std::array<float, 4> emission_parameters{};
        std::array<float, 4> radiance{};
    };

    export struct Rasterizer {
        static constexpr RendererDescriptor descriptor = rasterizer_descriptor;

        Rasterizer(VulkanRuntime& runtime, GpuScene& gpu_scene, scene::SceneView scene, std::filesystem::path shader_directory);
        ~Rasterizer();

        Rasterizer(const Rasterizer&)            = delete;
        Rasterizer(Rasterizer&&)                 = delete;
        Rasterizer& operator=(const Rasterizer&) = delete;
        Rasterizer& operator=(Rasterizer&&)      = delete;

        void invalidate(scene::SceneChange changes, GpuSceneUpdate gpu_update) noexcept;
        void prepare(scene::SceneView scene, const RenderView& view, const vk::raii::CommandBuffer& command_buffer);
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        [[nodiscard]] RenderOutput output() const noexcept;
        [[nodiscard]] DepthBufferView depth_buffer() noexcept;
        void set_display_mode(RasterDisplayMode mode) noexcept;

    private:
        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            GpuBuffer primitive_buffer{};
            GpuBuffer material_buffer{};
            GpuBuffer material_range_buffer{};
            GpuBuffer material_term_buffer{};
            GpuBuffer material_factor_buffer{};
            GpuBuffer face_material_buffer{};
            GpuBuffer area_light_buffer{};
            GpuBuffer light_buffer{};
            GpuBuffer volume_buffer{};
            GpuBuffer zero_volume_field_buffer{};
            std::array<GpuBuffer, 9> texture_buffers{};
            GpuBuffer scene_binding_buffer{};
            DescriptorLease material_descriptor{};
            DescriptorLease material_range_descriptor{};
            DescriptorLease material_term_descriptor{};
            DescriptorLease material_factor_descriptor{};
            DescriptorLease face_material_descriptor{};
            DescriptorLease area_light_descriptor{};
            DescriptorLease light_descriptor{};
            DescriptorLease zero_volume_field_descriptor{};
            std::array<DescriptorLease, 9> texture_descriptors{};
            scene::SceneRevision uploaded_revision{};
            DescriptorLease primitives_descriptor{};
            DescriptorLease bindings_descriptor{};
            DescriptorLease volumes_descriptor{};
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
            GpuImage output_image{};
            GpuImage depth_image{};
            DescriptorLease sampled_output_descriptor{};
            DescriptorLease storage_output_descriptor{};
            DescriptorLease sampled_depth_descriptor{};
            vk::ImageLayout output_layout{vk::ImageLayout::eUndefined};
            vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
            scene::SceneChange pending_changes{scene::SceneChange::None};
            GpuSceneChange pending_gpu_changes{GpuSceneChange::None};
        } renderer;

        void initialize_scene(scene::SceneView scene);
        void upload_scene(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void update_area_emitters(const vk::raii::CommandBuffer& command_buffer);
        void synchronize_scene(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void initialize_renderer();
        void create_shaders();
        void create_output(vk::Extent2D extent);
        void record_commands(const vk::raii::CommandBuffer& command_buffer);
        void destroy() noexcept;
    };

} // namespace spectra
