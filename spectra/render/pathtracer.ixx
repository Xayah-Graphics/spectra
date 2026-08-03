export module spectra.render:pathtracer;

export import :common;

import std;
import vulkan;

namespace spectra {
    struct GpuBufferBinding {
        GpuBuffer buffer{};
        DescriptorHandle descriptor{};

        GpuBufferBinding() = default;
        explicit GpuBufferBinding(const DescriptorHandle descriptor) noexcept : descriptor(descriptor) {}
        GpuBufferBinding(GpuBufferBinding&&) noexcept            = default;
        GpuBufferBinding& operator=(GpuBufferBinding&&) noexcept = default;
        GpuBufferBinding(const GpuBufferBinding&)                = delete;
        GpuBufferBinding& operator=(const GpuBufferBinding&)     = delete;
    };

    struct RgbSigmoidPolynomial {
        float c0{};
        float c1{};
        float c2{};

        [[nodiscard]] float evaluate(float wavelength) const noexcept;

        friend auto operator<=>(const RgbSigmoidPolynomial&, const RgbSigmoidPolynomial&) = default;
    };

    struct RgbToSpectrumTable {
        explicit RgbToSpectrumTable(std::span<const std::uint32_t> data);

        [[nodiscard]] RgbSigmoidPolynomial polynomial(math::Float3 rgb) const noexcept;

    private:
        std::array<float, 64> scale{};
        std::span<const std::uint32_t> coefficients{};
    };

    struct RgbToSpectrumTables {
        explicit RgbToSpectrumTables(std::span<const std::uint32_t> data);

        [[nodiscard]] const RgbToSpectrumTable& table_for(scene::SpectrumColorSpace color_space) const noexcept;

    private:
        RgbToSpectrumTable srgb;
        RgbToSpectrumTable rec2020;
        RgbToSpectrumTable aces2065_1;
    };

    enum class CompiledSpectrumKind : std::uint32_t {
        Rgb,
        Constant,
        Blackbody,
        PiecewiseLinear,
    };

    enum class CompiledIlluminant : std::uint32_t {
        None,
        D65,
        D60,
    };

    struct alignas(16) CompiledSpectrum {
        std::array<float, 4> parameters{};
        std::array<std::uint32_t, 4> metadata{};
    };

    [[nodiscard]] CompiledSpectrum compile_spectrum(const scene::SpectrumParameter& spectrum, const RgbToSpectrumTables& tables, std::vector<math::Float2>& piecewise_samples);

    enum class PathMaterialTable : std::size_t {
        Header,
        Diffuse,
        DiffuseTransmission,
        Conductor,
        Dielectric,
        ThinDielectric,
        CoatedDiffuse,
        CoatedConductor,
        Mix,
        TextureRequest,
        Count,
    };

    enum class PathTextureTable : std::size_t {
        Header,
        Mapping,
        Constant,
        Image,
        Checkerboard,
        Scale,
        Mix,
        DirectionMix,
        Bilerp,
        Count,
    };

    struct PathVolumeResources {
        GpuBufferBinding majorant{};
        scene::VolumeId volume_id{};
        scene::ResourceRevision revision{};
        math::UInt3 resolution{};
    };

    struct PathFilterResources {
        scene::Film film{};
        GpuBufferBinding distribution{};
        GpuBufferBinding sensor_response{};
        std::array<std::uint32_t, 2> resolution{};
        float absolute_integral{};
    };

    struct PathSamplerResources {
        scene::Sampler sampler{};
        std::vector<std::uint32_t> table_data{};
        GpuBufferBinding tables{};
        GpuBufferBinding pixel_samples{};
        std::uint32_t pixel_tile_size{1};
    };

    export struct PathTracer {
        static constexpr RendererDescriptor descriptor{"pathtracer", "Path"};
        static constexpr bool renders_visualizations = false;

        PathTracer(VulkanRuntime& runtime, GpuScene& gpu_scene, scene::SceneView scene, std::filesystem::path shader_directory);
        ~PathTracer();

        PathTracer(const PathTracer&)            = delete;
        PathTracer(PathTracer&&)                 = delete;
        PathTracer& operator=(const PathTracer&) = delete;
        PathTracer& operator=(PathTracer&&)      = delete;

        void invalidate(scene::SceneChange changes) noexcept;
        void prepare(scene::SceneView scene, const RenderView& view, const vk::raii::CommandBuffer& command_buffer);
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        [[nodiscard]] RenderOutput output() const noexcept;
        [[nodiscard]] RenderProgress progress() const noexcept;
        void set_paused(bool paused) noexcept;
        void reset() noexcept;
        [[nodiscard]] RenderGBufferReadback readback();

        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            std::vector<std::uint32_t> rgb_to_spectrum_table_data{};
            std::vector<float> cie_samples{};
            GpuBufferBinding primitives{};
            std::array<GpuBufferBinding, static_cast<std::size_t>(PathMaterialTable::Count)> materials{};
            std::array<GpuBufferBinding, static_cast<std::size_t>(PathTextureTable::Count)> textures{};
            GpuBufferBinding light_table{};
            GpuBufferBinding light_shapes{};
            GpuBufferBinding light_distribution{};
            GpuBufferBinding light_distribution_data{};
            GpuBufferBinding portals{};
            GpuBufferBinding light_bvh_nodes{};
            GpuBufferBinding light_bvh_bit_trails{};
            GpuBufferBinding face_materials{};
            GpuBufferBinding media{};
            GpuBufferBinding volumes{};
            GpuBufferBinding zero_volume_field{};
            std::vector<PathVolumeResources> volume_resources{};
            vk::raii::ShaderEXT volume_majorant_shader{nullptr};
            GpuBufferBinding spectra{};
            GpuBufferBinding piecewise_spectra{};
            GpuBufferBinding cie_spectra{};
            GpuBufferBinding rgb_to_spectrum_tables{};
            GpuBufferBinding bindings{};
            PathFilterResources filter{};
            PathSamplerResources sampler{};
            std::uint32_t light_count{};
            std::uint32_t texture_stack_size{1};
            std::uint32_t material_texture_value_count{1};
            std::uint32_t camera_medium_index{std::numeric_limits<std::uint32_t>::max()};
            scene::Camera camera{};
            scene::TransportSettings transport_settings{};
            math::Bounds3 compiled_bounds{};
            std::vector<math::Transform> compiled_instance_transforms{};
            scene::SceneRevision compiled_revision{};
            bool initialized{};
        } scene;

        struct {
            vk::Extent2D render_extent{};
            std::uint32_t pixel_capacity{};
            GpuImage output_image{};
            DescriptorHandle output_descriptor{};
            DescriptorHandle sampled_output_descriptor{};
            vk::ImageLayout output_layout{vk::ImageLayout::eUndefined};
            GpuBufferBinding queue_counts{};
            GpuBufferBinding ray_queue_0{};
            GpuBufferBinding ray_queue_1{};
            GpuBufferBinding ray_origins{};
            GpuBufferBinding ray_directions{};
            GpuBufferBinding ray_origin_dx{};
            GpuBufferBinding ray_origin_dy{};
            GpuBufferBinding ray_direction_dx{};
            GpuBufferBinding ray_direction_dy{};
            GpuBufferBinding throughputs{};
            GpuBufferBinding radiances{};
            GpuBufferBinding wavelengths{};
            GpuBufferBinding wavelength_pdfs{};
            GpuBufferBinding r_u{};
            GpuBufferBinding r_l{};
            GpuBufferBinding current_media{};
            GpuBufferBinding light_context_normals{};
            GpuBufferBinding flags{};
            GpuBufferBinding eta_scales{};
            GpuBufferBinding hit_normal_distances{};
            GpuBufferBinding hit_geometric_normal_u{};
            GpuBufferBinding hit_tangent_v{};
            GpuBufferBinding hit_dpdu{};
            GpuBufferBinding hit_dpdv{};
            GpuBufferBinding hit_dndu{};
            GpuBufferBinding hit_dndv{};
            GpuBufferBinding hit_identifiers{};
            GpuBufferBinding shadow_path_ids{};
            GpuBufferBinding shadow_origins{};
            GpuBufferBinding shadow_directions{};
            GpuBufferBinding shadow_contributions{};
            GpuBufferBinding shadow_r_p{};
            GpuBufferBinding shadow_pdfs{};
            GpuBufferBinding shadow_media{};
            GpuBufferBinding texture_evaluation_stack{};
            GpuBufferBinding evaluated_texture_values{};
            GpuBufferBinding filter_weights{};
            GpuBufferBinding film_rgb_sums{};
            GpuBufferBinding film_weight_sums{};
            GpuBufferBinding gbuffer_sample_albedo{};
            GpuBufferBinding gbuffer_sample_shading_normal{};
            GpuBufferBinding gbuffer_sample_geometric_normal{};
            GpuBufferBinding gbuffer_sample_position_depth{};
            GpuBufferBinding gbuffer_sample_uv{};
            GpuBufferBinding gbuffer_sample_identity_0{};
            GpuBufferBinding gbuffer_sample_identity_1{};
            GpuBufferBinding gbuffer_albedo_sums{};
            GpuBufferBinding gbuffer_shading_normal_sums{};
            GpuBufferBinding gbuffer_geometric_normal_sums{};
            GpuBufferBinding gbuffer_position_depth_sums{};
            GpuBufferBinding gbuffer_uv_weight_sums{};
            GpuBufferBinding gbuffer_identity_0{};
            GpuBufferBinding gbuffer_identity_1{};
            GpuBuffer indirect_commands{};
            GpuBufferBinding bindings{};
            std::vector<GpuBufferBinding> parameters{};
            std::uint32_t texture_stack_size{};
            std::uint32_t texture_value_count{};
            bool indirect_commands_configured{};
            std::uint32_t sample_index{};
            bool initialized{};
        } session;

        struct {
            vk::raii::ShaderEXT generate_camera_rays{nullptr};
            vk::raii::ShaderEXT evaluate_surface_textures{nullptr};
            vk::raii::ShaderEXT record_surface_gbuffer{nullptr};
            vk::raii::ShaderEXT sample_direct_lighting{nullptr};
            vk::raii::ShaderEXT shade_surfaces{nullptr};
            vk::raii::ShaderEXT resolve_visibility{nullptr};
            vk::raii::ShaderEXT accumulate_film{nullptr};
            vk::raii::Pipeline ray_generation_library{nullptr};
            vk::raii::Pipeline hit_library{nullptr};
            vk::raii::Pipeline pipeline{nullptr};
            GpuBuffer shader_binding_table{};
            vk::StridedDeviceAddressRegionKHR surface_ray_generation_region{};
            vk::StridedDeviceAddressRegionKHR shadow_ray_generation_region{};
            vk::StridedDeviceAddressRegionKHR miss_region{};
            vk::StridedDeviceAddressRegionKHR hit_region{};
            std::uint32_t stack_size{};
            bool integrator_initialized{};
            bool initialized{};
        } integrator;

        struct {
            scene::SceneChange pending_changes{scene::SceneChange::None};
            bool paused{};
            std::uint64_t camera_revision{};
        } control;

    private:
        void initialize_scene(scene::SceneView scene);
        void destroy_scene() noexcept;
        void synchronize_scene(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void compile_scene(scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer);
        void compile_filter(const scene::Film& film, const vk::raii::CommandBuffer* command_buffer);
        void compile_sampler(const scene::Sampler& sampler, const vk::raii::CommandBuffer* command_buffer);
        void update_volumes(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void initialize_session();
        void destroy_session() noexcept;
        void resize_session(vk::Extent2D extent, std::uint32_t texture_evaluation_stack_size, std::uint32_t material_texture_value_count);
        [[nodiscard]] std::vector<float> read_radiance();
        void initialize_integrator();
        void destroy_integrator() noexcept;
        void create_ray_tracing_pipeline();
        void create_shader_binding_table();
        void configure_indirect_commands();
        void record_integrator(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        void destroy() noexcept;
    };

    static_assert(SceneRenderer<PathTracer>);
    static_assert(ProgressiveSceneRenderer<PathTracer>);
    static_assert(GBufferSceneRenderer<PathTracer>);
} // namespace spectra
