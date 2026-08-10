export module spectra.render.rasterizer;

import spectra.render.contract;
import spectra.render.sampling;
import spectra.render.scene;
import spectra.runtime;
import spectra.scene;

import std;
import vulkan;

namespace spectra {
    struct alignas(16) RasterPrimitive {
        std::uint32_t transform_index{};
        std::uint32_t material_index{};
        std::uint32_t area_light_index{};
        std::uint32_t flags{};
        std::uint32_t face_material_offset{};
        std::uint32_t face_material_count{};
        std::uint32_t alpha_texture{};
        std::uint32_t reserved{};
    };

    struct alignas(16) RasterTransform {
        std::array<float, 4> transform_row_0{};
        std::array<float, 4> transform_row_1{};
        std::array<float, 4> transform_row_2{};
        std::array<float, 4> transform_row_3{};
    };

    struct alignas(16) RasterMaterial {
        std::array<std::uint32_t, 4> metadata{};
        std::array<std::uint32_t, 4> materials{};
        std::array<float, 4> spectrum_value_0{};
        std::array<std::uint32_t, 4> spectrum_data_0{};
        std::array<float, 4> spectrum_value_1{};
        std::array<std::uint32_t, 4> spectrum_data_1{};
        std::array<float, 4> spectrum_value_2{};
        std::array<std::uint32_t, 4> spectrum_data_2{};
        std::array<float, 4> spectrum_value_3{};
        std::array<std::uint32_t, 4> spectrum_data_3{};
        std::array<float, 4> scalar_values_0{};
        std::array<std::uint32_t, 4> scalar_textures_0{};
        std::array<float, 4> scalar_values_1{};
        std::array<std::uint32_t, 4> scalar_textures_1{};
    };

    struct alignas(16) RasterTextureHeader {
        std::array<std::uint32_t, 4> metadata{};
        std::array<std::uint32_t, 2> program{};
        std::array<std::uint32_t, 2> reserved{};
    };

    struct alignas(16) RasterTextureMapping {
        std::array<std::uint32_t, 4> metadata{};
        std::array<float, 4> transform_row_0{};
        std::array<float, 4> transform_row_1{};
        std::array<float, 4> transform_row_2{};
        std::array<float, 4> transform_row_3{};
        std::array<float, 4> parameter_0{};
        std::array<float, 4> parameter_1{};
    };

    struct alignas(16) RasterConstantTexture {
        std::array<float, 4> value{};
    };

    struct alignas(16) RasterImageTexture {
        DescriptorHandle image{};
        DescriptorHandle sampler{};
        std::array<std::uint32_t, 4> metadata{};
        std::array<float, 4> parameters{};
    };

    struct alignas(16) RasterCompositeTexture {
        std::array<std::uint32_t, 4> data{};
    };

    struct alignas(16) RasterDirectionMixTexture {
        std::array<std::uint32_t, 4> data{};
        std::array<float, 4> direction{};
    };

    struct alignas(16) RasterBilerpTexture {
        std::array<std::array<float, 4>, 4> values{};
        std::array<std::uint32_t, 4> data{};
    };

    struct alignas(16) RasterAreaLight {
        std::array<float, 4> emission{};
        std::array<std::uint32_t, 4> metadata{};
    };

    struct alignas(16) RasterLight {
        std::array<std::uint32_t, 4> metadata{};
        std::array<float, 4> radiance{};
        std::array<float, 4> position{};
        std::array<float, 4> direction{};
        std::array<float, 4> parameters{};
        std::array<float, 4> transform_row_0{};
        std::array<float, 4> transform_row_1{};
        std::array<float, 4> transform_row_2{};
        std::array<float, 4> texture_coordinates_0{};
        std::array<float, 4> texture_coordinates_1{};
        std::array<float, 4> selection{};
    };

    struct alignas(16) RasterRayPrimitive {
        DescriptorHandle positions{};
        DescriptorHandle texture_coordinates{};
        DescriptorHandle indices{};
        DescriptorHandle radii{};
        std::array<std::uint32_t, 4> metadata{};
        std::array<float, 4> parameters{};
        std::uint32_t scene_primitive_index{};
        std::array<std::uint32_t, 3> reserved{};
    };

    struct RasterDynamicAreaLightRange {
        std::uint32_t scene_primitive_index{};
        std::uint32_t resource_index{};
        std::uint32_t first_light{};
        std::uint32_t capacity{};
        std::uint32_t kind{};
        std::uint32_t light_kind{};
        std::uint32_t flags{};
        std::uint32_t emission_texture{};
        std::uint32_t attribute_mask{};
        std::array<float, 4> geometry_parameters{};
        std::array<float, 4> emission_parameters{};
        std::array<float, 4> radiance{};
    };

    struct alignas(16) RasterSceneBindings {
        DescriptorHandle primitives{};
        DescriptorHandle transforms{};
        DescriptorHandle materials{};
        DescriptorHandle face_materials{};
        DescriptorHandle area_lights{};
        DescriptorHandle texture_headers{};
        DescriptorHandle texture_mappings{};
        DescriptorHandle constant_textures{};
        DescriptorHandle image_textures{};
        DescriptorHandle checkerboard_textures{};
        DescriptorHandle scale_textures{};
        DescriptorHandle mix_textures{};
        DescriptorHandle direction_mix_textures{};
        DescriptorHandle bilerp_textures{};
        DescriptorHandle lights{};
        DescriptorHandle ray_primitives{};
        std::array<std::uint32_t, 4> metadata{};
    };

    struct alignas(16) RasterVolume {
        std::array<std::uint32_t, 4> metadata{};
        std::array<std::uint32_t, 4> majorant_metadata{};
        std::array<float, 4> bounds_minimum{};
        std::array<float, 4> bounds_maximum{};
        std::array<float, 4> inverse_transform_row_0{};
        std::array<float, 4> inverse_transform_row_1{};
        std::array<float, 4> inverse_transform_row_2{};
        std::array<float, 4> sigma_a{};
        std::array<float, 4> sigma_s{};
        std::array<float, 4> emission{};
        std::array<float, 4> scales{};
        std::array<float, 4> temperature{};
        std::array<float, 4> procedural_parameters{};
        DescriptorHandle density{};
        DescriptorHandle temperature_field{};
        DescriptorHandle sigma_a_field{};
        DescriptorHandle sigma_s_field{};
        DescriptorHandle emission_scale_field{};
        DescriptorHandle emission_field{};
        DescriptorHandle majorant{};
    };

    export struct Rasterizer {
        static constexpr RendererDescriptor descriptor = rasterizer_descriptor;

        struct VolumeResources {
            scene::VolumeId volume_id{};
            scene::ResourceRevision revision{};
            math::UInt3 resolution{};
            GpuBuffer majorant{};
            DescriptorLease majorant_descriptor{};
        };

        Rasterizer(VulkanRuntime& runtime, GpuScene& gpu_scene, SamplingResources& sampling, scene::SceneView scene, std::filesystem::path shader_directory);
        ~Rasterizer();

        Rasterizer(const Rasterizer&)            = delete;
        Rasterizer(Rasterizer&&)                 = delete;
        Rasterizer& operator=(const Rasterizer&) = delete;
        Rasterizer& operator=(Rasterizer&&)      = delete;

        void invalidate(scene::SceneChange changes, GpuSceneUpdate gpu_update) noexcept;
        void prepare(scene::SceneView scene, const RenderView& view, const vk::raii::CommandBuffer& command_buffer);
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        [[nodiscard]] RenderOutput output() const noexcept;
        [[nodiscard]] RenderProgress progress() const noexcept;
        void set_paused(bool paused) noexcept;
        void reset() noexcept;
        void set_display_mode(RasterDisplayMode mode) noexcept;

        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            SamplingResources& sampling;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            GpuBuffer primitive_buffer{};
            GpuBuffer material_buffer{};
            GpuBuffer face_material_buffer{};
            GpuBuffer area_light_buffer{};
            GpuBuffer light_buffer{};
            GpuBuffer ray_primitive_buffer{};
            GpuBuffer volume_buffer{};
            GpuBuffer zero_volume_field_buffer{};
            std::array<GpuBuffer, 9> texture_buffers{};
            GpuBuffer scene_binding_buffer{};
            DescriptorLease material_descriptor{};
            DescriptorLease face_material_descriptor{};
            DescriptorLease area_light_descriptor{};
            DescriptorLease light_descriptor{};
            DescriptorLease ray_primitives_descriptor{};
            DescriptorLease zero_volume_field_descriptor{};
            std::array<DescriptorLease, 9> texture_descriptors{};
            std::uint32_t texture_count{};
            std::uint32_t texture_stack_size{};
            scene::SceneRevision uploaded_revision{};
            std::vector<VolumeResources> volume_resources{};
            vk::raii::ShaderEXT volume_majorant_shader{nullptr};
            DescriptorLease primitives_descriptor{};
            DescriptorLease bindings_descriptor{};
            DescriptorLease volumes_descriptor{};
            std::uint32_t volume_count{};
            std::uint32_t light_count{};
            std::vector<RasterDynamicAreaLightRange> dynamic_area_lights{};
            vk::raii::ShaderEXT dynamic_light_shapes_shader{nullptr};
            vk::raii::ShaderEXT dynamic_light_finalize_shader{nullptr};
            vk::raii::ShaderEXT dynamic_light_selection_shader{nullptr};
        } scene;

        struct {
            scene::Camera camera{};
            std::uint64_t camera_revision{};
            float film_exposure{};
            std::array<std::uint32_t, 2> film_resolution{};
            std::array<std::uint32_t, 2> film_pixel_minimum{};
            std::array<std::uint32_t, 2> film_pixel_maximum{};
            RasterDisplayMode display_mode{RasterDisplayMode::Material};
            vk::raii::ShaderEXTs shaders{nullptr};
            vk::raii::ShaderEXT sphere_shader{nullptr};
            vk::raii::ShaderEXT volume_shader{nullptr};
            vk::raii::ShaderEXT background_shader{nullptr};
            vk::raii::ShaderEXT accumulation_shader{nullptr};
            GpuImage sample_image{};
            GpuImage output_image{};
            GpuImage depth_image{};
            DescriptorLease sampled_output_descriptor{};
            DescriptorLease storage_output_descriptor{};
            DescriptorLease storage_sample_descriptor{};
            DescriptorLease sampled_depth_descriptor{};
            vk::ImageLayout output_layout{vk::ImageLayout::eUndefined};
            vk::ImageLayout sample_layout{vk::ImageLayout::eUndefined};
            vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
            scene::Sampler sampler{};
            std::uint32_t sample_index{};
            bool paused{};
            scene::SceneChange pending_changes{scene::SceneChange::None};
            GpuSceneChange pending_gpu_changes{GpuSceneChange::None};
        } renderer;

    private:
        void initialize_scene(scene::SceneView scene);
        void upload_scene(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void update_volumes(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void update_dynamic_lights(const vk::raii::CommandBuffer& command_buffer);
        void synchronize_scene(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void initialize_renderer();
        void create_shaders();
        void create_output(vk::Extent2D extent);
        void record_commands(const vk::raii::CommandBuffer& command_buffer);
        void destroy() noexcept;
    };

    static_assert(SceneRenderer<Rasterizer>);
    static_assert(ProgressiveSceneRenderer<Rasterizer>);
} // namespace spectra
