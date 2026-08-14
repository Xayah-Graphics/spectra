module;

#include "../shaders/shader_semantics.h"

export module spectra.render.pathtracer.scene;

import spectra.render.gpu_scene;
import spectra.render.pathtracer.abi;
import spectra.render.pathtracer.resources;
import spectra.runtime;
import spectra.scene;

import std;
import vulkan;

export namespace spectra {
    struct GpuBufferBinding {
        GpuBuffer buffer{};
        DescriptorLease descriptor{};

        GpuBufferBinding() = default;
        explicit GpuBufferBinding(DescriptorLease descriptor) noexcept : descriptor(std::move(descriptor)) {}
        GpuBufferBinding(GpuBufferBinding&&) noexcept            = default;
        GpuBufferBinding& operator=(GpuBufferBinding&&) noexcept = default;
        GpuBufferBinding(const GpuBufferBinding&)                = delete;
        GpuBufferBinding& operator=(const GpuBufferBinding&)     = delete;
    };

    struct PathBufferSlice {
        DescriptorLease descriptor{};
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

    struct PathSceneGpuSnapshot {
        struct Texture {
            DescriptorHandle image{};
            DescriptorHandle sampler{};
        };

        struct Volume {
            struct Field {
                std::string id{};
                scene::VolumeFieldKind kind{scene::VolumeFieldKind::Float};
                std::vector<DescriptorHandle> descriptors{};
            };

            scene::VolumeId id{};
            scene::ResourceRevision revision{};
            math::UInt3 resolution{};
            std::vector<Field> fields{};
            bool cpu_data_stale{};
        };

        struct Geometry {
            DescriptorHandle positions{};
            DescriptorHandle indices{};
            DescriptorHandle normals{};
            DescriptorHandle tangents{};
            DescriptorHandle texture_coordinates{};
            std::uint32_t vertex_count{};
            std::uint32_t index_count{};
            std::uint32_t vertex_capacity{};
            std::uint32_t index_capacity{};
            std::uint32_t attribute_mask{};
        };

        struct SphereSet {
            DescriptorHandle positions{};
            DescriptorHandle radii{};
            std::uint32_t count{};
            std::uint32_t capacity{};
        };

        std::vector<Texture> textures{};
        std::vector<Volume> volumes{};
        std::vector<Geometry> geometries{};
        std::vector<SphereSet> sphere_sets{};
        std::vector<GpuScenePrimitive> primitives{};
        std::vector<std::uint32_t> acceleration_primitive_indices{};
        std::uint64_t structure_revision{};
    };

    struct PreparedPathVolume {
        scene::VolumeId id{};
        scene::ResourceRevision revision{};
        math::UInt3 resolution{};
        std::vector<float> majorant{};
    };

    struct PreparedPathFilter {
        scene::Film film{};
        std::vector<float> distribution{};
        std::vector<float> sensor_response{};
        std::array<std::uint32_t, 2> resolution{};
        float absolute_integral{};
    };

    struct PreparedPathSampler {
        scene::Sampler sampler{};
        std::vector<math::Float2> pixel_samples{};
        std::uint32_t pixel_tile_size{1};
    };

    struct PreparedPathScene {
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
        std::vector<PreparedPathVolume> volume_resources{};
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

    struct PathTracerScenePreparation {
        struct SceneSnapshot {
            scene::SceneResources resources{};
            scene::Camera camera{};
            scene::Film film{};
            scene::Sampler sampler{};
            scene::TransportSettings transport{};
            scene::SceneRevision revision{};
            math::Bounds3 bounds{};

            [[nodiscard]] scene::SceneView view() const noexcept {
                return {this->resources, this->camera, this->film, this->sampler, this->transport, this->revision};
            }
        };

        std::shared_ptr<const SceneSnapshot> scene{};
        PathSceneGpuSnapshot gpu{};
        PathTracerPreparationState progress{};
        std::unique_ptr<PreparedPathFilter> filter{};
        std::unique_ptr<PreparedPathSampler> sampler{};
        std::unique_ptr<PreparedPathScene> prepared{};
    };

    [[nodiscard]] scene::SceneResources snapshot_path_scene_resources(const scene::SceneResources& source);
    [[nodiscard]] PathSceneGpuSnapshot snapshot_path_scene_gpu(GpuScene& gpu_scene, scene::SceneView scene);
    [[nodiscard]] std::unique_ptr<PreparedPathFilter> prepare_path_filter(const scene::Film& film, const PathTracerResources& resources);
    [[nodiscard]] std::unique_ptr<PreparedPathSampler> prepare_path_sampler(const scene::Sampler& sampler, const PathTracerResources& resources);
    [[nodiscard]] std::unique_ptr<PreparedPathScene> prepare_path_scene(scene::SceneView scene, math::Bounds3 bounds, const PathSceneGpuSnapshot& gpu, const PathTracerResources& resources, PathTracerPreparationState* progress);
} // namespace spectra
