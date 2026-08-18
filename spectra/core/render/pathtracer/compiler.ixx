module;

#include "../shaders/shader_semantics.h"

export module spectra.render.pathtracer.compiler;

import spectra.render.gpu_scene;
import spectra.render.pathtracer.abi;
import spectra.render.pathtracer.resources;
import spectra.runtime;
import spectra.scene;

import std;
import vulkan;

export namespace spectra::render {
    struct GpuBufferBinding {
        runtime::GpuBuffer buffer{};
        runtime::DescriptorLease descriptor{};

        GpuBufferBinding() = default;
        explicit GpuBufferBinding(runtime::DescriptorLease descriptor) noexcept : descriptor(std::move(descriptor)) {}
        GpuBufferBinding(GpuBufferBinding&&) noexcept            = default;
        GpuBufferBinding& operator=(GpuBufferBinding&&) noexcept = default;
        GpuBufferBinding(const GpuBufferBinding&)                = delete;
        GpuBufferBinding& operator=(const GpuBufferBinding&)     = delete;
    };

    struct PathBufferSlice {
        runtime::DescriptorLease descriptor{};
        vk::DeviceSize offset{};
        vk::DeviceSize size{};
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
        [[nodiscard]] static std::span<const std::uint32_t> checked_data(std::span<const std::uint32_t> data);

        RgbToSpectrumTable srgb;
        RgbToSpectrumTable rec2020;
        RgbToSpectrumTable aces2065_1;
    };

    enum class CompiledSpectrumKind : std::uint32_t {
        Rgb             = shader_semantics::spectrum_rgb,
        Constant        = shader_semantics::spectrum_constant,
        Blackbody       = shader_semantics::spectrum_blackbody,
        PiecewiseLinear = shader_semantics::spectrum_piecewise_linear,
    };

    enum class CompiledIlluminant : std::uint32_t {
        None = shader_semantics::illuminant_none,
        D65  = shader_semantics::illuminant_d65,
        D60  = shader_semantics::illuminant_d60,
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
        GpuBufferBinding pixel_samples{};
        std::uint32_t pixel_tile_size{1};
    };

    struct PathDynamicAreaLightRange {
        std::uint32_t scene_primitive_index{};
        std::uint32_t resource_index{};
        std::uint32_t first_light{};
        std::uint32_t capacity{};
        std::uint32_t kind{};
        std::uint32_t geometry_kind{};
        std::uint32_t reverse_orientation{};
        std::uint32_t alpha_texture{};
        std::uint32_t attribute_mask{};
        std::array<float, 4> geometry_parameters{};
        std::array<float, 4> emission_parameters{};
        std::array<float, 4> selection_parameters{};
    };

    struct PathCompilationInput {
        struct Texture {
            runtime::DescriptorHandle image{};
            runtime::DescriptorHandle sampler{};
        };

        struct Volume {
            struct Field {
                std::string id{};
                scene::FieldKind kind{scene::FieldKind::Float};
                std::vector<runtime::DescriptorHandle> descriptors{};
            };

            scene::VolumeId id{};
            scene::ResourceRevision revision{};
            math::UInt3 resolution{};
            std::vector<Field> fields{};
            bool cpu_data_stale{};
        };

        struct Geometry {
            runtime::DescriptorHandle positions{};
            runtime::DescriptorHandle indices{};
            runtime::DescriptorHandle normals{};
            runtime::DescriptorHandle tangents{};
            runtime::DescriptorHandle texture_coordinates{};
            std::uint32_t vertex_count{};
            std::uint32_t index_count{};
            std::uint32_t vertex_capacity{};
            std::uint32_t index_capacity{};
            std::uint32_t attribute_mask{};
        };

        struct SphereSet {
            runtime::DescriptorHandle positions{};
            runtime::DescriptorHandle radii{};
            std::uint32_t count{};
            std::uint32_t capacity{};
        };

        std::vector<Texture> textures{};
        std::vector<Volume> volumes{};
        std::vector<Geometry> geometries{};
        std::vector<SphereSet> sphere_sets{};
        std::vector<GpuScenePrimitive> primitives{};
        Geometry volume_region_geometry{};
        std::vector<GpuAccelerationEntity> acceleration_entities{};
        std::uint64_t structure_revision{};
    };

    struct CompiledPathVolume {
        scene::VolumeId id{};
        scene::ResourceRevision revision{};
        math::UInt3 resolution{};
        std::vector<float> majorant{};
    };

    struct CompiledPathFilter {
        scene::Film film{};
        std::vector<float> distribution{};
        std::vector<float> sensor_response{};
        std::array<std::uint32_t, 2> resolution{};
        float absolute_integral{};
    };

    struct CompiledPathSampler {
        scene::Sampler sampler{};
        std::vector<math::Float2> pixel_samples{};
        std::uint32_t pixel_tile_size{1};
    };

    struct CompiledPathScene {
        std::vector<std::byte> arena{};
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
        PathBufferSlice face_materials{};
        PathBufferSlice media{};
        PathBufferSlice volumes{};
        PathBufferSlice spectra{};
        PathBufferSlice piecewise_spectra{};
        std::vector<pathtracer::PathVolume> compiled_volumes{};
        std::vector<CompiledPathVolume> volume_resources{};
        std::uint32_t texture_count{};
        std::uint32_t texture_stack_size{1};
        std::uint32_t material_texture_value_count{1};
        std::uint32_t light_count{};
        std::uint32_t light_bvh_node_count{};
        std::uint32_t light_bvh_infinite_count{};
        std::uint32_t camera_medium_index{std::numeric_limits<std::uint32_t>::max()};
        scene::LightSamplerKind light_sampler{scene::LightSamplerKind::Bvh};
        math::Bounds3 bounds{};
        std::vector<math::Transform> instance_transforms{};
        scene::SceneRevision revision{};
        std::uint64_t gpu_structure_revision{};
        std::vector<PathDynamicAreaLightRange> dynamic_area_lights{};
    };

    struct PathSceneCompilation {
        struct InputScene {
            scene::SceneResources resources{};
            scene::Camera camera{};
            scene::Film film{};
            scene::Sampler sampler{};
            scene::TransportSettings transport{};
            scene::SceneRevision revision{};
            math::Bounds3 bounds{};

            [[nodiscard]] scene::ResolvedSceneView view() const noexcept {
                return {this->resources, this->camera, this->film, this->sampler, this->transport, this->revision};
            }
        };

        std::shared_ptr<const InputScene> scene{};
        PathCompilationInput gpu{};
        PathTracerPreparationState progress{};
        std::unique_ptr<CompiledPathFilter> filter{};
        std::unique_ptr<CompiledPathSampler> sampler{};
        std::unique_ptr<CompiledPathScene> compiled{};
    };

    [[nodiscard]] scene::SceneResources snapshot_path_scene_resources(const scene::SceneResources& source);
    [[nodiscard]] PathCompilationInput capture_path_compilation_input(GpuScene& gpu_scene, scene::ResolvedSceneView scene);
    [[nodiscard]] std::unique_ptr<CompiledPathFilter> build_path_filter(const scene::Film& film, const PathTracerResources& resources);
    [[nodiscard]] std::unique_ptr<CompiledPathSampler> build_path_sampler(const scene::Sampler& sampler, const PathTracerResources& resources);
    [[nodiscard]] std::unique_ptr<CompiledPathScene> build_path_scene(scene::ResolvedSceneView scene, math::Bounds3 bounds, const PathCompilationInput& gpu, const PathTracerResources& resources, PathTracerPreparationState* progress);
} // namespace spectra::render
