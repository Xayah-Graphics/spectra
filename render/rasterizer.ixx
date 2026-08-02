export module spectra.rasterizer;

import spectra;
import spectra.render;
import spectra.scene;
import std;
import vulkan;

namespace spectra::rasterizer {
    struct alignas(16) RasterPrimitive {
        std::uint32_t transform{};
        std::uint32_t material{};
        std::uint32_t area_light{};
        std::uint32_t flags{};
        std::uint32_t face_material_offset{};
        std::uint32_t face_material_count{};
        std::array<std::uint32_t, 2> reserved{};
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
        std::array<std::uint32_t, 4> metadata{};
    };

    struct alignas(16) GpuPickPrimitive {
        std::array<std::uint32_t, 4> metadata{};
        std::array<float, 4> parameters{};
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
        DescriptorHandle density{};
        DescriptorHandle temperature_field{};
        DescriptorHandle sigma_a_field{};
        DescriptorHandle sigma_s_field{};
        DescriptorHandle emission_scale_field{};
        DescriptorHandle emission_field{};
        DescriptorHandle majorant{};
    };

    struct alignas(16) RasterVolumeLight {
        std::array<std::uint32_t, 4> metadata{};
        std::array<float, 4> position{};
        std::array<float, 4> direction{};
        std::array<float, 4> radiance{};
        std::array<float, 4> parameters{};
    };

    export struct RasterScene {
        RasterScene(Spectra& runtime, const render::GpuScene& assets, scene::SceneView scene, const std::filesystem::path& shader_directory);
        ~RasterScene();

        RasterScene(const RasterScene&)            = delete;
        RasterScene(RasterScene&&)                 = delete;
        RasterScene& operator=(const RasterScene&) = delete;
        RasterScene& operator=(RasterScene&&)      = delete;

        void synchronize(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);

    private:
        struct VolumeGpuData {
            scene::VolumeId id{};
            scene::ResourceRevision revision{};
            scene::UInt3 resolution{};
            GpuBuffer majorant{};
            DescriptorHandle majorant_descriptor{};
        };

        void upload(scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer = nullptr);
        void update_transforms(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void update_volumes(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);

        Spectra* runtime{};
        GpuBuffer primitive_buffer{};
        GpuBuffer transform_buffer{};
        GpuBuffer material_buffer{};
        GpuBuffer face_material_buffer{};
        GpuBuffer area_light_buffer{};
        GpuBuffer pick_primitive_buffer{};
        GpuBuffer volume_buffer{};
        GpuBuffer volume_light_buffer{};
        GpuBuffer volume_zero_buffer{};
        std::array<GpuBuffer, 9> texture_buffers{};
        GpuBuffer binding_buffer{};
        DescriptorHandle material_descriptor{};
        DescriptorHandle face_material_descriptor{};
        DescriptorHandle area_light_descriptor{};
        DescriptorHandle volume_zero_descriptor{};
        std::array<DescriptorHandle, 9> texture_descriptors{};
        std::uint32_t texture_count{};
        std::uint32_t texture_stack_size{};
        scene::SceneRevision uploaded_revision{};
        std::vector<VolumeGpuData> volume_gpu_data{};
        vk::raii::ShaderEXT volume_majorant_shader{nullptr};

    public:
        const render::GpuScene& gpu_scene;
        DescriptorHandle primitives_descriptor{};
        DescriptorHandle transforms_descriptor{};
        DescriptorHandle pick_primitives_descriptor{};
        DescriptorHandle bindings_descriptor{};
        DescriptorHandle volumes_descriptor{};
        DescriptorHandle volume_lights_descriptor{};
        std::uint32_t volumes_count{};
        std::uint32_t volume_lights_count{};
        scene::CameraResource camera{};
        float film_exposure{};
    };


    export struct Rasterizer {
        Rasterizer(Spectra& runtime, const RasterScene& scene, const std::filesystem::path& shader_directory);
        ~Rasterizer();

        Rasterizer(const Rasterizer&)            = delete;
        Rasterizer(Rasterizer&&)                 = delete;
        Rasterizer& operator=(const Rasterizer&) = delete;
        Rasterizer& operator=(Rasterizer&&)      = delete;

        void prepare(vk::Extent2D extent);
        void record(const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] render::RenderOutput output() const noexcept;

    private:
        void create_shaders(const std::filesystem::path& shader_directory);
        void create_output(vk::Extent2D extent);
        [[nodiscard]] std::array<float, 16> view_projection() const noexcept;

        Spectra* runtime{};
        const RasterScene* scene{};
        vk::raii::ShaderEXTs shaders{nullptr};
        vk::raii::ShaderEXT particle_shader{nullptr};
        vk::raii::ShaderEXT volume_shader{nullptr};
        GpuImage output_image{};
        GpuImage depth_image{};
        DescriptorHandle sampled_output_descriptor{};
        DescriptorHandle storage_output_descriptor{};
        DescriptorHandle sampled_depth_descriptor{};
        vk::ImageLayout output_layout{vk::ImageLayout::eUndefined};
        vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
    };
} // namespace spectra::rasterizer
