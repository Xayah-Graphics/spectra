module;

#include <Windows.h>

#include <glaze/glaze.hpp>

module spectra.scene;

import :format;
import spectra.util.hash;
import std;

template <>
struct glz::meta<spectra::math::Float2> {
    static constexpr auto read  = [](spectra::math::Float2& value, const std::array<float, 2>& data) { value = {data[0], data[1]}; };
    static constexpr auto write = [](const spectra::math::Float2& value) { return std::array{value.x, value.y}; };
    static constexpr auto value = glz::custom<read, write>;
};

template <>
struct glz::meta<spectra::math::Float3> {
    static constexpr auto read  = [](spectra::math::Float3& value, const std::array<float, 3>& data) { value = {data[0], data[1], data[2]}; };
    static constexpr auto write = [](const spectra::math::Float3& value) { return std::array{value.x, value.y, value.z}; };
    static constexpr auto value = glz::custom<read, write>;
};

template <>
struct glz::meta<spectra::math::Float4> {
    static constexpr auto read  = [](spectra::math::Float4& value, const std::array<float, 4>& data) { value = {data[0], data[1], data[2], data[3]}; };
    static constexpr auto write = [](const spectra::math::Float4& value) { return std::array{value.x, value.y, value.z, value.w}; };
    static constexpr auto value = glz::custom<read, write>;
};

template <>
struct glz::meta<spectra::math::UInt3> {
    static constexpr auto read  = [](spectra::math::UInt3& value, const std::array<std::uint32_t, 3>& data) { value = {data[0], data[1], data[2]}; };
    static constexpr auto write = [](const spectra::math::UInt3& value) { return std::array{value.x, value.y, value.z}; };
    static constexpr auto value = glz::custom<read, write>;
};

template <>
struct glz::meta<spectra::math::Transform> {
    static constexpr auto read  = [](spectra::math::Transform& value, const std::array<float, 16>& data) { value.matrix = data; };
    static constexpr auto write = [](const spectra::math::Transform& value) -> const std::array<float, 16>& { return value.matrix; };
    static constexpr auto value = glz::custom<read, write>;
};

#define SPECTRA_ID_META(id_type)                                                                                        \
    template <>                                                                                                         \
    struct glz::meta<spectra::scene::id_type> {                                                                         \
        static constexpr auto read  = [](spectra::scene::id_type& id, const std::uint64_t value) { id.value = value; }; \
        static constexpr auto write = [](const spectra::scene::id_type id) { return id.value; };                        \
        static constexpr auto value = glz::custom<read, write>;                                                         \
    }

SPECTRA_ID_META(GeometryId);
SPECTRA_ID_META(ParticleSetId);
SPECTRA_ID_META(VolumeId);
SPECTRA_ID_META(TextureId);
SPECTRA_ID_META(MaterialId);
SPECTRA_ID_META(MediumId);
SPECTRA_ID_META(LightId);
SPECTRA_ID_META(CameraId);
SPECTRA_ID_META(FilmId);
SPECTRA_ID_META(SamplerId);
SPECTRA_ID_META(PrototypeId);
SPECTRA_ID_META(InstanceId);

#undef SPECTRA_ID_META

template <>
struct glz::meta<spectra::scene::DynamicSystemId> {
    static constexpr auto read  = [](spectra::scene::DynamicSystemId& id, std::string value) { id.value = std::move(value); };
    static constexpr auto write = [](const spectra::scene::DynamicSystemId& id) -> const std::string& { return id.value; };
    static constexpr auto value = glz::custom<read, write>;
};

template <>
struct glz::meta<spectra::scene::DynamicResourceKind> {
    static constexpr auto value = glz::enumerate("Instance", spectra::scene::DynamicResourceKind::Instance, "Geometry", spectra::scene::DynamicResourceKind::Geometry, "ParticleSet", spectra::scene::DynamicResourceKind::ParticleSet, "Volume", spectra::scene::DynamicResourceKind::Volume);
};

template <>
struct glz::meta<spectra::scene::DynamicParameterKind> {
    static constexpr auto value = glz::enumerate("Boolean", spectra::scene::DynamicParameterKind::Boolean, "Integer", spectra::scene::DynamicParameterKind::Integer, "Float", spectra::scene::DynamicParameterKind::Float, "math::Float3", spectra::scene::DynamicParameterKind::Float3, "Enumeration", spectra::scene::DynamicParameterKind::Enumeration);
};

template <>
struct glz::meta<spectra::scene::DynamicParameterValue> {
    static constexpr auto value = glz::object("kind", &spectra::scene::DynamicParameterValue::kind, "integer", &spectra::scene::DynamicParameterValue::integer, "floating", &spectra::scene::DynamicParameterValue::floating);
};

template <>
struct glz::meta<spectra::scene::DynamicParameterSetting> {
    static constexpr auto value = glz::object("id", &spectra::scene::DynamicParameterSetting::parameter_id, "value", &spectra::scene::DynamicParameterSetting::value);
};

template <>
struct glz::meta<spectra::scene::DynamicPortBinding> {
    static constexpr auto value = glz::object("port", &spectra::scene::DynamicPortBinding::port_id, "resource_kind", &spectra::scene::DynamicPortBinding::resource_kind, "resource", &spectra::scene::DynamicPortBinding::resource_id);
};

template <>
struct glz::meta<spectra::scene::DynamicSystem> {
    static constexpr auto value = glz::object("id", &spectra::scene::DynamicSystem::id, "name", &spectra::scene::DynamicSystem::name, "provider", &spectra::scene::DynamicSystem::provider_id, "enabled", &spectra::scene::DynamicSystem::enabled, "visible", &spectra::scene::DynamicSystem::visible, "parameters", &spectra::scene::DynamicSystem::parameters, "bindings", &spectra::scene::DynamicSystem::bindings);
};

template <>
struct glz::meta<spectra::scene::DynamicClock> {
    static constexpr auto value = glz::object("step_seconds", &spectra::scene::DynamicClock::step_seconds, "start_step", &spectra::scene::DynamicClock::start_step, "end_step", &spectra::scene::DynamicClock::end_step, "loop", &spectra::scene::DynamicClock::loop);
};

template <>
struct glz::meta<spectra::scene::DynamicSetup> {
    static constexpr auto value = glz::object("clock", &spectra::scene::DynamicSetup::clock, "seed", &spectra::scene::DynamicSetup::seed, "systems", &spectra::scene::DynamicSetup::systems);
};

template <>
struct glz::meta<spectra::scene::TriangleMeshGeometry> {
    static constexpr auto value = glz::object("asset", &spectra::scene::TriangleMeshGeometry::asset);
};

template <>
struct glz::meta<spectra::scene::ParticleSet> {
    static constexpr auto value = glz::object("id", &spectra::scene::ParticleSet::id, "name", &spectra::scene::ParticleSet::name, "asset", &spectra::scene::ParticleSet::asset, "material", &spectra::scene::ParticleSet::material);
};

template <>
struct glz::meta<spectra::scene::DensityGridVolume> {
    static constexpr auto value = glz::object("resolution", &spectra::scene::DensityGridVolume::resolution, "asset", &spectra::scene::DensityGridVolume::asset);
};

template <>
struct glz::meta<spectra::scene::RgbGridVolume> {
    static constexpr auto value = glz::object("resolution", &spectra::scene::RgbGridVolume::resolution, "color_space", &spectra::scene::RgbGridVolume::color_space, "asset", &spectra::scene::RgbGridVolume::asset);
};

template <>
struct glz::meta<spectra::scene::NanoVdbVolume> {
    static constexpr auto value = glz::object("asset", &spectra::scene::NanoVdbVolume::asset, "density_grid", &spectra::scene::NanoVdbVolume::density_grid, "temperature_grid", &spectra::scene::NanoVdbVolume::temperature_grid);
};

template <>
struct glz::meta<spectra::scene::ProceduralCloudVolume> {
    static constexpr auto value = glz::object("density", &spectra::scene::ProceduralCloudVolume::density, "wispiness", &spectra::scene::ProceduralCloudVolume::wispiness, "frequency", &spectra::scene::ProceduralCloudVolume::frequency);
};

template <>
struct glz::meta<spectra::scene::TextureValueKind> {
    static constexpr auto value = glz::enumerate("float", spectra::scene::TextureValueKind::Float, "spectrum", spectra::scene::TextureValueKind::Spectrum);
};

template <>
struct glz::meta<spectra::scene::TextureMapping> {
    static constexpr auto value = glz::object("data", &spectra::scene::TextureMapping::data);
};

template <>
struct glz::meta<spectra::scene::CheckerboardMapping> {
    static constexpr auto value = glz::object("data", &spectra::scene::CheckerboardMapping::data);
};

template <>
struct glz::meta<spectra::scene::TextureWrapMode> {
    static constexpr auto value = glz::enumerate("repeat", spectra::scene::TextureWrapMode::Repeat, "clamp", spectra::scene::TextureWrapMode::Clamp, "black", spectra::scene::TextureWrapMode::Black);
};

template <>
struct glz::meta<spectra::scene::TextureColorSpace> {
    static constexpr auto value = glz::enumerate("linear", spectra::scene::TextureColorSpace::Linear, "srgb", spectra::scene::TextureColorSpace::Srgb, "aces2065_1", spectra::scene::TextureColorSpace::Aces2065_1, "rec2020", spectra::scene::TextureColorSpace::Rec2020);
};

template <>
struct glz::meta<spectra::scene::TextureChannel> {
    static constexpr auto value = glz::enumerate("red", spectra::scene::TextureChannel::Red, "green", spectra::scene::TextureChannel::Green, "blue", spectra::scene::TextureChannel::Blue, "alpha", spectra::scene::TextureChannel::Alpha, "average", spectra::scene::TextureChannel::Average, "luminance", spectra::scene::TextureChannel::Luminance);
};

template <>
struct glz::meta<spectra::scene::TextureFilter> {
    static constexpr auto value = glz::enumerate("point", spectra::scene::TextureFilter::Point, "bilinear", spectra::scene::TextureFilter::Bilinear, "trilinear", spectra::scene::TextureFilter::Trilinear, "ewa", spectra::scene::TextureFilter::Ewa);
};

template <>
struct glz::meta<spectra::scene::TextureSpectrumType> {
    static constexpr auto value = glz::enumerate("albedo", spectra::scene::TextureSpectrumType::Albedo, "unbounded", spectra::scene::TextureSpectrumType::Unbounded, "illuminant", spectra::scene::TextureSpectrumType::Illuminant);
};

template <>
struct glz::meta<spectra::scene::ImageTexture> {
    static constexpr auto value = glz::object("asset", &spectra::scene::ImageTexture::asset, "mapping", &spectra::scene::ImageTexture::mapping, "wrap", &spectra::scene::ImageTexture::wrap, "channel", &spectra::scene::ImageTexture::channel, "filter", &spectra::scene::ImageTexture::filter, "maximum_anisotropy", &spectra::scene::ImageTexture::maximum_anisotropy, "scale", &spectra::scene::ImageTexture::scale, "invert", &spectra::scene::ImageTexture::invert);
};

template <>
struct glz::meta<spectra::scene::SpectrumEncoding> {
    static constexpr auto value = glz::enumerate("rgb_albedo", spectra::scene::SpectrumEncoding::RgbAlbedo, "rgb_unbounded", spectra::scene::SpectrumEncoding::RgbUnbounded, "rgb_illuminant", spectra::scene::SpectrumEncoding::RgbIlluminant, "constant", spectra::scene::SpectrumEncoding::Constant, "blackbody", spectra::scene::SpectrumEncoding::Blackbody, "piecewise_linear", spectra::scene::SpectrumEncoding::PiecewiseLinear);
};

template <>
struct glz::meta<spectra::scene::SpectrumColorSpace> {
    static constexpr auto value = glz::enumerate("srgb", spectra::scene::SpectrumColorSpace::Srgb, "rec2020", spectra::scene::SpectrumColorSpace::Rec2020, "aces2065_1", spectra::scene::SpectrumColorSpace::Aces2065_1);
};

template <>
struct glz::meta<spectra::scene::EmissionSidedness> {
    static constexpr auto value = glz::enumerate("front", spectra::scene::EmissionSidedness::Front, "both", spectra::scene::EmissionSidedness::Both);
};

template <>
struct glz::meta<spectra::scene::FilterKind> {
    static constexpr auto value = glz::enumerate("box", spectra::scene::FilterKind::Box, "gaussian", spectra::scene::FilterKind::Gaussian, "mitchell", spectra::scene::FilterKind::Mitchell, "sinc", spectra::scene::FilterKind::Sinc, "triangle", spectra::scene::FilterKind::Triangle);
};

template <>
struct glz::meta<spectra::scene::SamplerKind> {
    static constexpr auto value = glz::enumerate("independent", spectra::scene::SamplerKind::Independent, "stratified", spectra::scene::SamplerKind::Stratified, "halton", spectra::scene::SamplerKind::Halton, "sobol", spectra::scene::SamplerKind::Sobol, "padded_sobol", spectra::scene::SamplerKind::PaddedSobol, "zsobol", spectra::scene::SamplerKind::ZSobol, "pmj02bn", spectra::scene::SamplerKind::Pmj02bn);
};

template <>
struct glz::meta<spectra::scene::SamplerRandomization> {
    static constexpr auto value = glz::enumerate("none", spectra::scene::SamplerRandomization::None, "permute_digits", spectra::scene::SamplerRandomization::PermuteDigits, "fast_owen", spectra::scene::SamplerRandomization::FastOwen, "owen", spectra::scene::SamplerRandomization::Owen);
};

template <>
struct glz::meta<spectra::scene::LightSamplerKind> {
    static constexpr auto value = glz::enumerate("uniform", spectra::scene::LightSamplerKind::Uniform, "power", spectra::scene::LightSamplerKind::Power, "bvh", spectra::scene::LightSamplerKind::Bvh);
};

template <>
struct glz::meta<std::variant<spectra::scene::UvTextureMapping, spectra::scene::PlanarTextureMapping, spectra::scene::SphericalTextureMapping, spectra::scene::CylindricalTextureMapping>> {
    static constexpr std::string_view tag = "type";
    static constexpr std::array ids{
        "uv",
        "planar",
        "spherical",
        "cylindrical",
    };
};

template <>
struct glz::meta<std::variant<spectra::scene::TextureMapping, spectra::scene::TextureMapping3D>> {
    static constexpr std::string_view tag = "dimension";
    static constexpr std::array ids{
        "2d",
        "3d",
    };
};

template <>
struct glz::meta<std::variant<spectra::scene::TriangleMeshGeometry, spectra::scene::SphereGeometry, spectra::scene::BoxGeometry, spectra::scene::RectangleGeometry, spectra::scene::DiskGeometry, spectra::scene::CylinderGeometry>> {
    static constexpr std::string_view tag = "type";
    static constexpr std::array ids{
        "triangle_mesh",
        "sphere",
        "box",
        "rectangle",
        "disk",
        "cylinder",
    };
};

template <>
struct glz::meta<std::variant<spectra::scene::DensityGridVolume, spectra::scene::RgbGridVolume, spectra::scene::NanoVdbVolume, spectra::scene::ProceduralCloudVolume>> {
    static constexpr std::string_view tag = "type";
    static constexpr std::array ids{
        "density_grid",
        "rgb_grid",
        "nanovdb",
        "procedural_cloud",
    };
};

static_assert(glz::tag_v<decltype(spectra::scene::Volume{}.data)> == "type");
static_assert(glz::ids_v<decltype(spectra::scene::Volume{}.data)>.size() == 4);
static_assert(glz::ids_v<decltype(spectra::scene::Volume{}.data)>[0] == "density_grid");
static_assert(glz::variant_is_auto_deducible<decltype(spectra::scene::Volume{}.data)>());

template <>
struct glz::meta<spectra::scene::Volume> {
    static constexpr auto value = glz::object("id", &spectra::scene::Volume::id, "name", &spectra::scene::Volume::name, "bounds", &spectra::scene::Volume::bounds, "transform", &spectra::scene::Volume::transform, "data", &spectra::scene::Volume::data);
};

static_assert(glz::reflect<spectra::scene::Volume>::size == 5);
static_assert(glz::reflect<spectra::scene::Volume>::keys[4] == "data");

template <>
struct glz::meta<std::variant<spectra::scene::ConstantTexture, spectra::scene::ImageTexture, spectra::scene::CheckerboardTexture, spectra::scene::ScaleTexture, spectra::scene::MixTexture, spectra::scene::DirectionMixTexture, spectra::scene::BilerpTexture>> {
    static constexpr std::string_view tag = "type";
    static constexpr std::array ids{
        "constant",
        "image",
        "checkerboard",
        "scale",
        "mix",
        "direction_mix",
        "bilerp",
    };
};

template <>
struct glz::meta<std::variant<spectra::scene::InterfaceMaterialData, spectra::scene::DiffuseMaterialData, spectra::scene::DiffuseTransmissionMaterialData, spectra::scene::ConductorMaterialData, spectra::scene::DielectricMaterialData, spectra::scene::ThinDielectricMaterialData, spectra::scene::CoatedDiffuseMaterialData, spectra::scene::CoatedConductorMaterialData, spectra::scene::MixMaterialData>> {
    static constexpr std::string_view tag = "type";
    static constexpr std::array ids{
        "interface",
        "diffuse",
        "diffuse_transmission",
        "conductor",
        "dielectric",
        "thin_dielectric",
        "coated_diffuse",
        "coated_conductor",
        "mix",
    };
};

template <>
struct glz::meta<std::variant<spectra::scene::ConductorEtaK, spectra::scene::ConductorReflectance>> {
    static constexpr std::string_view tag = "type";
    static constexpr std::array ids{
        "eta_k",
        "reflectance",
    };
};

template <>
struct glz::meta<std::variant<spectra::scene::HomogeneousMedium, spectra::scene::VolumeMedium>> {
    static constexpr std::string_view tag = "type";
    static constexpr std::array ids{
        "homogeneous",
        "volume",
    };
};

template <>
struct glz::meta<std::variant<spectra::scene::PointLight, spectra::scene::SpotLight, spectra::scene::DistantLight, spectra::scene::DiffuseAreaLight, spectra::scene::InfiniteLight, spectra::scene::PortalInfiniteLight>> {
    static constexpr std::string_view tag = "type";
    static constexpr std::array ids{
        "point",
        "spot",
        "distant",
        "diffuse_area",
        "infinite",
        "portal_infinite",
    };
};

template <>
struct glz::meta<std::variant<spectra::scene::PerspectiveCameraData, spectra::scene::OrthographicCameraData>> {
    static constexpr std::string_view tag = "type";
    static constexpr std::array ids{
        "perspective",
        "orthographic",
    };
};

template <>
struct glz::meta<spectra::scene::PerspectiveCameraData> {
    static constexpr auto value = glz::object("vertical_fov", &spectra::scene::PerspectiveCameraData::vertical_fov, "screen", &spectra::scene::PerspectiveCameraData::screen_window, "lens_radius", &spectra::scene::PerspectiveCameraData::lens_radius, "focal_distance", &spectra::scene::PerspectiveCameraData::focal_distance, "near_plane", &spectra::scene::PerspectiveCameraData::near_plane, "far_plane", &spectra::scene::PerspectiveCameraData::far_plane);
};

template <>
struct glz::meta<spectra::scene::OrthographicCameraData> {
    static constexpr auto value = glz::object("screen", &spectra::scene::OrthographicCameraData::screen_window, "lens_radius", &spectra::scene::OrthographicCameraData::lens_radius, "focal_distance", &spectra::scene::OrthographicCameraData::focal_distance, "near_plane", &spectra::scene::OrthographicCameraData::near_plane, "far_plane", &spectra::scene::OrthographicCameraData::far_plane);
};

template <>
struct glz::meta<spectra::scene::Geometry> {
    static constexpr auto value = glz::object("id", &spectra::scene::Geometry::id, "name", &spectra::scene::Geometry::name, "data", &spectra::scene::Geometry::data);
};

template <>
struct glz::meta<spectra::scene::Texture> {
    static constexpr auto value = glz::object("id", &spectra::scene::Texture::id, "name", &spectra::scene::Texture::name, "value_kind", &spectra::scene::Texture::value_kind, "spectrum_type", &spectra::scene::Texture::spectrum_type, "color_space", &spectra::scene::Texture::color_space, "data", &spectra::scene::Texture::data);
};

template <>
struct glz::meta<spectra::scene::Material> {
    static constexpr auto value = glz::object("id", &spectra::scene::Material::id, "name", &spectra::scene::Material::name, "data", &spectra::scene::Material::data);
};

template <>
struct glz::meta<spectra::scene::Medium> {
    static constexpr auto value = glz::object("id", &spectra::scene::Medium::id, "name", &spectra::scene::Medium::name, "data", &spectra::scene::Medium::data);
};

template <>
struct glz::meta<spectra::scene::Light> {
    static constexpr auto value = glz::object("id", &spectra::scene::Light::id, "name", &spectra::scene::Light::name, "data", &spectra::scene::Light::data);
};

template <>
struct glz::meta<spectra::scene::Prototype> {
    static constexpr auto value = glz::object("id", &spectra::scene::Prototype::id, "name", &spectra::scene::Prototype::name, "primitives", &spectra::scene::Prototype::primitives);
};

template <>
struct glz::meta<spectra::scene::Instance> {
    static constexpr auto value = glz::object("id", &spectra::scene::Instance::id, "name", &spectra::scene::Instance::name, "prototype", &spectra::scene::Instance::prototype, "transform", &spectra::scene::Instance::transform, "visible", &spectra::scene::Instance::visible);
};

template <>
struct glz::meta<spectra::scene::Camera> {
    static constexpr auto value = glz::object("id", &spectra::scene::Camera::id, "name", &spectra::scene::Camera::name, "transform", &spectra::scene::Camera::transform, "exposure_time", &spectra::scene::Camera::exposure_time, "medium", &spectra::scene::Camera::medium, "data", &spectra::scene::Camera::data);
};

template <>
struct glz::meta<spectra::scene::Film> {
    static constexpr auto value = glz::object("id", &spectra::scene::Film::id, "name", &spectra::scene::Film::name, "resolution", &spectra::scene::Film::resolution, "pixel_minimum", &spectra::scene::Film::pixel_minimum, "pixel_maximum", &spectra::scene::Film::pixel_maximum, "exposure", &spectra::scene::Film::exposure, "iso", &spectra::scene::Film::iso, "color_space", &spectra::scene::Film::color_space, "sensor_response", &spectra::scene::Film::sensor_response, "sensor_to_output_rgb", &spectra::scene::Film::sensor_to_output_rgb, "maximum_component_value", &spectra::scene::Film::maximum_component_value, "filter", &spectra::scene::Film::filter, "gbuffer", &spectra::scene::Film::gbuffer, "gbuffer_camera_space", &spectra::scene::Film::gbuffer_camera_space);
};

template <>
struct glz::meta<spectra::scene::Sampler> {
    static constexpr auto value = glz::object("id", &spectra::scene::Sampler::id, "name", &spectra::scene::Sampler::name, "kind", &spectra::scene::Sampler::kind, "samples_per_pixel", &spectra::scene::Sampler::samples_per_pixel, "seed", &spectra::scene::Sampler::seed, "jitter", &spectra::scene::Sampler::jitter, "x_strata", &spectra::scene::Sampler::x_strata, "y_strata", &spectra::scene::Sampler::y_strata, "randomization", &spectra::scene::Sampler::randomization);
};

template <>
struct glz::meta<spectra::scene::Scene> {
    static constexpr auto value = glz::object("format_version", &spectra::scene::Scene::format_version, "name", &spectra::scene::Scene::name, "resources", &spectra::scene::Scene::resources, "active_camera", &spectra::scene::Scene::active_camera, "active_film", &spectra::scene::Scene::active_film, "active_sampler", &spectra::scene::Scene::active_sampler, "transport", &spectra::scene::Scene::transport, "dynamic_setup", &spectra::scene::Scene::dynamic_setup);
};


namespace spectra::scene {
    struct SceneHeader {
        std::uint32_t format_version{};
    };

    namespace {
        struct GeometryAssetHeader {
            std::array<char, 8> magic{'S', 'P', 'G', 'E', 'O', 'M', '0', '3'};
            std::uint32_t version{3};
            std::uint32_t reserved{};
            std::uint64_t position_count{};
            std::uint64_t normal_count{};
            std::uint64_t tangent_count{};
            std::uint64_t texture_coordinate_count{};
            std::uint64_t index_count{};
        };

        struct ParticleAssetHeader {
            std::array<char, 8> magic{'S', 'P', 'P', 'A', 'R', 'T', '0', '1'};
            std::uint32_t version{1};
            std::uint32_t reserved{};
            std::uint64_t position_count{};
            std::uint64_t radius_count{};
            std::uint64_t velocity_count{};
            std::uint64_t color_count{};
            std::uint64_t temperature_count{};
            std::uint64_t material_count{};
        };

        enum class VolumeAssetKind : std::uint32_t {
            DensityGrid,
            RgbGrid,
            NanoVdb,
        };

        struct VolumeAssetHeader {
            std::array<char, 8> magic{'S', 'P', 'V', 'O', 'L', '0', '0', '2'};
            std::uint32_t version{2};
            VolumeAssetKind kind{};
            math::UInt3 resolution{};
            std::uint32_t reserved{};
            std::uint64_t primary_element_count{};
            std::uint64_t secondary_element_count{};
            std::uint64_t tertiary_element_count{};
        };

        struct TextureAssetHeader {
            std::array<char, 8> magic{'S', 'P', 'T', 'E', 'X', '0', '0', '1'};
            std::uint32_t version{1};
            std::uint32_t mip_count{};
            std::uint32_t width{};
            std::uint32_t height{};
            std::uint64_t texel_count{};
        };

        static_assert(sizeof(math::Float2) == sizeof(float) * 2);
        static_assert(sizeof(math::Float3) == sizeof(float) * 3);
        static_assert(sizeof(MaterialId) == sizeof(std::uint64_t));
        static_assert(sizeof(GeometryAssetHeader) == 56);
        static_assert(sizeof(ParticleAssetHeader) == 64);
        static_assert(sizeof(VolumeAssetHeader) == 56);
        static_assert(sizeof(TextureAssetHeader) == 32);

        [[nodiscard]] std::uint64_t volume_sample_count(const math::UInt3 resolution) {
            if (resolution.x == 0 || resolution.y == 0 || resolution.z == 0) throw std::runtime_error("Spectra volume resolution must be positive");
            const std::uint64_t xy = static_cast<std::uint64_t>(resolution.x) * resolution.y;
            if (xy > std::numeric_limits<std::uint64_t>::max() / resolution.z) throw std::runtime_error("Spectra volume sample count overflows uint64");
            return xy * resolution.z;
        }

        [[nodiscard]] std::filesystem::path asset_path(const std::filesystem::path& package_root, const AssetReference& reference, const std::string_view extension) {
            if (reference.content_hash.size() != 64 || !std::ranges::all_of(reference.content_hash, [](const char character) { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); })) throw std::runtime_error("Spectra asset content_hash must be a lowercase SHA-256 digest");
            const std::filesystem::path relative = std::filesystem::path{"assets"} / std::format("{}{}", reference.content_hash, extension);
            if (reference.relative_path != relative.generic_string()) throw std::runtime_error(std::format("Spectra asset path {} does not match its content hash", reference.relative_path));
            return package_root / relative;
        }

        void inspect_asset(const std::filesystem::path& package_root, const AssetReference& reference, const std::string_view extension) {
            const std::filesystem::path path = asset_path(package_root, reference, extension);
            std::error_code error{};
            const std::uint64_t byte_size = std::filesystem::file_size(path, error);
            if (error || byte_size != reference.byte_size) throw std::runtime_error(std::format("Spectra asset size mismatch: {}", path.string()));
        }

        void verify_asset(const std::filesystem::path& package_root, const AssetReference& reference, const std::string_view extension) {
            inspect_asset(package_root, reference, extension);
            const std::filesystem::path path = asset_path(package_root, reference, extension);
            if (sha256_file(path) != reference.content_hash) throw std::runtime_error(std::format("Spectra asset SHA-256 mismatch: {}", path.string()));
        }

        template <class Value>
        void write_values(std::ofstream& stream, const std::span<const Value> values) {
            const std::span<const std::byte> bytes = std::as_bytes(values);
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        template <class Value>
        void read_values(std::ifstream& stream, std::vector<Value>& values) {
            const std::span<std::byte> bytes = std::as_writable_bytes(std::span{values});
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        [[nodiscard]] AssetReference write_geometry_asset(const TriangleMeshGeometry& mesh, const std::filesystem::path& package_root) {
            const GeometryAssetHeader header{
                .position_count           = mesh.positions.size(),
                .normal_count             = mesh.normals.size(),
                .tangent_count            = mesh.tangents.size(),
                .texture_coordinate_count = mesh.texture_coordinates.size(),
                .index_count              = mesh.indices.size(),
            };
            const std::array blocks{
                std::as_bytes(std::span{&header, 1}),
                std::as_bytes(std::span{mesh.positions}),
                std::as_bytes(std::span{mesh.normals}),
                std::as_bytes(std::span{mesh.tangents}),
                std::as_bytes(std::span{mesh.texture_coordinates}),
                std::as_bytes(std::span{mesh.indices}),
            };
            AssetReference reference{
                .content_hash = sha256_hex(blocks),
            };
            reference.relative_path          = (std::filesystem::path{"assets"} / std::format("{}.geometry", reference.content_hash)).generic_string();
            reference.byte_size              = sizeof(header) + mesh.positions.size() * sizeof(math::Float3) + mesh.normals.size() * sizeof(math::Float3) + mesh.tangents.size() * sizeof(math::Float3) + mesh.texture_coordinates.size() * sizeof(math::Float2) + mesh.indices.size() * sizeof(std::uint32_t);
            const std::filesystem::path path = package_root / reference.relative_path;
            if (std::filesystem::exists(path)) {
                verify_asset(package_root, reference, ".geometry");
                return reference;
            }
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream{path, std::ios::binary | std::ios::trunc};
            if (!stream) throw std::runtime_error(std::format("Failed to create Spectra geometry asset: {}", path.string()));
            write_values(stream, std::span{&header, 1});
            write_values(stream, std::span{mesh.positions});
            write_values(stream, std::span{mesh.normals});
            write_values(stream, std::span{mesh.tangents});
            write_values(stream, std::span{mesh.texture_coordinates});
            write_values(stream, std::span{mesh.indices});
            if (!stream) throw std::runtime_error(std::format("Failed to write Spectra geometry asset: {}", path.string()));
            return reference;
        }

        void load_geometry_asset(TriangleMeshGeometry& mesh, const std::filesystem::path& package_root) {
            verify_asset(package_root, mesh.asset, ".geometry");
            const std::filesystem::path path = asset_path(package_root, mesh.asset, ".geometry");
            std::ifstream stream{path, std::ios::binary};
            GeometryAssetHeader header{};
            stream.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (!stream || header.magic != GeometryAssetHeader{}.magic || header.version != 3 || header.reserved != 0) throw std::runtime_error(std::format("Invalid Spectra geometry asset header: {}", path.string()));
            const std::uint64_t expected_size = sizeof(header) + header.position_count * sizeof(math::Float3) + header.normal_count * sizeof(math::Float3) + header.tangent_count * sizeof(math::Float3) + header.texture_coordinate_count * sizeof(math::Float2) + header.index_count * sizeof(std::uint32_t);
            if (expected_size != mesh.asset.byte_size) throw std::runtime_error(std::format("Invalid Spectra geometry asset payload size: {}", path.string()));
            mesh.positions.resize(header.position_count);
            mesh.normals.resize(header.normal_count);
            mesh.tangents.resize(header.tangent_count);
            mesh.texture_coordinates.resize(header.texture_coordinate_count);
            mesh.indices.resize(header.index_count);
            read_values(stream, mesh.positions);
            read_values(stream, mesh.normals);
            read_values(stream, mesh.tangents);
            read_values(stream, mesh.texture_coordinates);
            read_values(stream, mesh.indices);
            if (!stream) throw std::runtime_error(std::format("Failed to read Spectra geometry asset payload: {}", path.string()));
        }

        [[nodiscard]] AssetReference write_particle_asset(const ParticleSet& particles, const std::filesystem::path& package_root) {
            const std::size_t particle_count = particles.positions.size();
            if (particles.radii.size() != particle_count || (!particles.velocities.empty() && particles.velocities.size() != particle_count) || (!particles.colors.empty() && particles.colors.size() != particle_count) || (!particles.temperatures.empty() && particles.temperatures.size() != particle_count) || (!particles.particle_materials.empty() && particles.particle_materials.size() != particle_count)) throw std::runtime_error("Spectra particle attributes do not match the particle count");
            const ParticleAssetHeader header{
                .position_count    = particles.positions.size(),
                .radius_count      = particles.radii.size(),
                .velocity_count    = particles.velocities.size(),
                .color_count       = particles.colors.size(),
                .temperature_count = particles.temperatures.size(),
                .material_count    = particles.particle_materials.size(),
            };
            const std::array blocks{
                std::as_bytes(std::span{&header, 1}),
                std::as_bytes(std::span{particles.positions}),
                std::as_bytes(std::span{particles.radii}),
                std::as_bytes(std::span{particles.velocities}),
                std::as_bytes(std::span{particles.colors}),
                std::as_bytes(std::span{particles.temperatures}),
                std::as_bytes(std::span{particles.particle_materials}),
            };
            AssetReference reference{
                .content_hash = sha256_hex(blocks),
            };
            reference.relative_path          = (std::filesystem::path{"assets"} / std::format("{}.particles", reference.content_hash)).generic_string();
            reference.byte_size              = sizeof(header) + particles.positions.size() * sizeof(math::Float3) + particles.radii.size() * sizeof(float) + particles.velocities.size() * sizeof(math::Float3) + particles.colors.size() * sizeof(math::Float3) + particles.temperatures.size() * sizeof(float) + particles.particle_materials.size() * sizeof(MaterialId);
            const std::filesystem::path path = package_root / reference.relative_path;
            if (std::filesystem::exists(path)) {
                verify_asset(package_root, reference, ".particles");
                return reference;
            }
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream{path, std::ios::binary | std::ios::trunc};
            if (!stream) throw std::runtime_error(std::format("Failed to create Spectra particle asset: {}", path.string()));
            write_values(stream, std::span{&header, 1});
            write_values(stream, std::span{particles.positions});
            write_values(stream, std::span{particles.radii});
            write_values(stream, std::span{particles.velocities});
            write_values(stream, std::span{particles.colors});
            write_values(stream, std::span{particles.temperatures});
            write_values(stream, std::span{particles.particle_materials});
            if (!stream) throw std::runtime_error(std::format("Failed to write Spectra particle asset: {}", path.string()));
            return reference;
        }

        void load_particle_asset(ParticleSet& particles, const std::filesystem::path& package_root) {
            verify_asset(package_root, particles.asset, ".particles");
            const std::filesystem::path path = asset_path(package_root, particles.asset, ".particles");
            std::ifstream stream{path, std::ios::binary};
            ParticleAssetHeader header{};
            stream.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (!stream || header.magic != ParticleAssetHeader{}.magic || header.version != 1 || header.reserved != 0) throw std::runtime_error(std::format("Invalid Spectra particle asset header: {}", path.string()));
            if (header.radius_count != header.position_count || (header.velocity_count != 0 && header.velocity_count != header.position_count) || (header.color_count != 0 && header.color_count != header.position_count) || (header.temperature_count != 0 && header.temperature_count != header.position_count) || (header.material_count != 0 && header.material_count != header.position_count)) throw std::runtime_error(std::format("Invalid Spectra particle attribute counts: {}", path.string()));
            const std::uint64_t expected_size = sizeof(header) + header.position_count * sizeof(math::Float3) + header.radius_count * sizeof(float) + header.velocity_count * sizeof(math::Float3) + header.color_count * sizeof(math::Float3) + header.temperature_count * sizeof(float) + header.material_count * sizeof(MaterialId);
            if (expected_size != particles.asset.byte_size) throw std::runtime_error(std::format("Invalid Spectra particle asset payload size: {}", path.string()));
            particles.positions.resize(header.position_count);
            particles.radii.resize(header.radius_count);
            particles.velocities.resize(header.velocity_count);
            particles.colors.resize(header.color_count);
            particles.temperatures.resize(header.temperature_count);
            particles.particle_materials.resize(header.material_count);
            read_values(stream, particles.positions);
            read_values(stream, particles.radii);
            read_values(stream, particles.velocities);
            read_values(stream, particles.colors);
            read_values(stream, particles.temperatures);
            read_values(stream, particles.particle_materials);
            if (!stream) throw std::runtime_error(std::format("Failed to read Spectra particle asset payload: {}", path.string()));
        }

        template <typename... Element>
        [[nodiscard]] AssetReference write_volume_asset_payload(const VolumeAssetHeader& header, const std::filesystem::path& package_root, const std::span<const Element>... payload) {
            const std::array blocks{std::as_bytes(std::span{&header, 1}), std::as_bytes(payload)...};
            AssetReference reference{
                .content_hash = sha256_hex(blocks),
            };
            reference.relative_path          = (std::filesystem::path{"assets"} / std::format("{}.volume", reference.content_hash)).generic_string();
            reference.byte_size              = sizeof(header) + (payload.size_bytes() + ... + std::size_t{});
            const std::filesystem::path path = package_root / reference.relative_path;
            if (std::filesystem::exists(path)) {
                verify_asset(package_root, reference, ".volume");
                return reference;
            }
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream{path, std::ios::binary | std::ios::trunc};
            if (!stream) throw std::runtime_error(std::format("Failed to create Spectra volume asset: {}", path.string()));
            write_values(stream, std::span{&header, 1});
            (write_values(stream, payload), ...);
            if (!stream) throw std::runtime_error(std::format("Failed to write Spectra volume asset: {}", path.string()));
            return reference;
        }

        [[nodiscard]] AssetReference write_volume_asset(const DensityGridVolume& volume, const std::filesystem::path& package_root) {
            const std::uint64_t sample_count = volume_sample_count(volume.resolution);
            if (volume.density.size() != sample_count || (!volume.temperature.empty() && volume.temperature.size() != sample_count) || (!volume.emission_scale.empty() && volume.emission_scale.size() != sample_count)) throw std::runtime_error("Spectra density-grid payload does not match its resolution");
            const VolumeAssetHeader header{
                .kind                    = VolumeAssetKind::DensityGrid,
                .resolution              = volume.resolution,
                .primary_element_count   = volume.density.size(),
                .secondary_element_count = volume.temperature.size(),
                .tertiary_element_count  = volume.emission_scale.size(),
            };
            return write_volume_asset_payload(header, package_root, std::span<const float>{volume.density}, std::span<const float>{volume.temperature}, std::span<const float>{volume.emission_scale});
        }

        [[nodiscard]] AssetReference write_volume_asset(const RgbGridVolume& volume, const std::filesystem::path& package_root) {
            const std::uint64_t sample_count = volume_sample_count(volume.resolution);
            if ((volume.sigma_a.empty() && volume.sigma_s.empty()) || (!volume.sigma_a.empty() && volume.sigma_a.size() != sample_count) || (!volume.sigma_s.empty() && volume.sigma_s.size() != sample_count) || (!volume.emission.empty() && (volume.sigma_a.empty() || volume.emission.size() != sample_count))) throw std::runtime_error("Spectra RGB-grid payload does not match its resolution");
            const VolumeAssetHeader header{
                .kind                    = VolumeAssetKind::RgbGrid,
                .resolution              = volume.resolution,
                .primary_element_count   = volume.sigma_a.size(),
                .secondary_element_count = volume.sigma_s.size(),
                .tertiary_element_count  = volume.emission.size(),
            };
            return write_volume_asset_payload(header, package_root, std::span<const math::Float3>{volume.sigma_a}, std::span<const math::Float3>{volume.sigma_s}, std::span<const math::Float3>{volume.emission});
        }

        [[nodiscard]] AssetReference write_volume_asset(const NanoVdbVolume& volume, const std::filesystem::path& package_root) {
            const std::uint64_t majorant_count = volume_sample_count(volume.majorant_resolution);
            if (volume.density_data.empty() || volume.majorant.size() != majorant_count) throw std::runtime_error("Spectra NanoVDB payload and majorant grid are required");
            const VolumeAssetHeader header{
                .kind                    = VolumeAssetKind::NanoVdb,
                .resolution              = volume.majorant_resolution,
                .primary_element_count   = volume.density_data.size(),
                .secondary_element_count = volume.temperature_data.size(),
                .tertiary_element_count  = volume.majorant.size(),
            };
            return write_volume_asset_payload(header, package_root, std::span<const std::uint32_t>{volume.density_data}, std::span<const std::uint32_t>{volume.temperature_data}, std::span<const float>{volume.majorant});
        }

        [[nodiscard]] AssetReference write_texture_asset(const ImageTexture& texture, const std::filesystem::path& package_root) {
            if (texture.width == 0 || texture.height == 0 || texture.mip_offsets.empty()) throw std::runtime_error("Image Texture dimensions and mip offsets are required");
            std::uint32_t width  = texture.width;
            std::uint32_t height = texture.height;
            std::uint64_t expected_texels{};
            for (const std::uint64_t offset : texture.mip_offsets) {
                if (offset != expected_texels) throw std::runtime_error("Image Texture mip offsets must be tightly packed");
                expected_texels += static_cast<std::uint64_t>(width) * height;
                width  = std::max(1u, width / 2u);
                height = std::max(1u, height / 2u);
            }
            if (expected_texels != texture.texels.size() || texture.mip_offsets.size() > std::bit_width(std::max(texture.width, texture.height))) throw std::runtime_error("Image Texture mip pyramid is invalid");
            const TextureAssetHeader header{
                .mip_count   = static_cast<std::uint32_t>(texture.mip_offsets.size()),
                .width       = texture.width,
                .height      = texture.height,
                .texel_count = texture.texels.size(),
            };
            const std::array blocks{
                std::as_bytes(std::span{&header, 1}),
                std::as_bytes(std::span{texture.texels}),
            };
            AssetReference reference{
                .content_hash = sha256_hex(blocks),
            };
            reference.relative_path          = (std::filesystem::path{"assets"} / std::format("{}.texture", reference.content_hash)).generic_string();
            reference.byte_size              = sizeof(header) + texture.texels.size() * sizeof(math::Float4);
            const std::filesystem::path path = package_root / reference.relative_path;
            if (std::filesystem::exists(path)) {
                verify_asset(package_root, reference, ".texture");
                return reference;
            }
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream{path, std::ios::binary | std::ios::trunc};
            if (!stream) throw std::runtime_error(std::format("Failed to create Spectra texture asset: {}", path.string()));
            write_values(stream, std::span{&header, 1});
            write_values(stream, std::span{texture.texels});
            if (!stream) throw std::runtime_error(std::format("Failed to write Spectra texture asset: {}", path.string()));
            return reference;
        }

        [[nodiscard]] VolumeAssetHeader read_volume_asset_header(const AssetReference& reference, const std::filesystem::path& package_root, std::ifstream& stream) {
            verify_asset(package_root, reference, ".volume");
            const std::filesystem::path path = asset_path(package_root, reference, ".volume");
            stream.open(path, std::ios::binary);
            VolumeAssetHeader header{};
            stream.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (!stream || header.magic != VolumeAssetHeader{}.magic || header.version != 2 || header.reserved != 0) throw std::runtime_error(std::format("Invalid Spectra volume asset header: {}", path.string()));
            return header;
        }

        void load_volume_asset(DensityGridVolume& volume, const std::filesystem::path& package_root) {
            std::ifstream stream{};
            const VolumeAssetHeader header = read_volume_asset_header(volume.asset, package_root, stream);
            if (header.kind != VolumeAssetKind::DensityGrid || header.resolution != volume.resolution) throw std::runtime_error("Spectra density-grid volume asset metadata is inconsistent");
            const std::uint64_t sample_count = volume_sample_count(header.resolution);
            if (header.primary_element_count != sample_count || (header.secondary_element_count != 0 && header.secondary_element_count != sample_count) || (header.tertiary_element_count != 0 && header.tertiary_element_count != sample_count)) throw std::runtime_error("Invalid Spectra density-grid volume asset sample counts");
            const std::uint64_t expected_size = sizeof(header) + (header.primary_element_count + header.secondary_element_count + header.tertiary_element_count) * sizeof(float);
            if (expected_size != volume.asset.byte_size) throw std::runtime_error("Invalid Spectra density-grid volume asset payload size");
            volume.density.resize(header.primary_element_count);
            volume.temperature.resize(header.secondary_element_count);
            volume.emission_scale.resize(header.tertiary_element_count);
            read_values(stream, volume.density);
            read_values(stream, volume.temperature);
            read_values(stream, volume.emission_scale);
            if (!stream) throw std::runtime_error("Failed to read Spectra density-grid volume asset payload");
        }

        void load_volume_asset(RgbGridVolume& volume, const std::filesystem::path& package_root) {
            std::ifstream stream{};
            const VolumeAssetHeader header = read_volume_asset_header(volume.asset, package_root, stream);
            if (header.kind != VolumeAssetKind::RgbGrid || header.resolution != volume.resolution) throw std::runtime_error("Spectra RGB-grid volume asset metadata is inconsistent");
            const std::uint64_t sample_count = volume_sample_count(header.resolution);
            if ((header.primary_element_count == 0 && header.secondary_element_count == 0) || (header.primary_element_count != 0 && header.primary_element_count != sample_count) || (header.secondary_element_count != 0 && header.secondary_element_count != sample_count) || (header.tertiary_element_count != 0 && (header.primary_element_count == 0 || header.tertiary_element_count != sample_count))) throw std::runtime_error("Invalid Spectra RGB-grid volume asset sample counts");
            const std::uint64_t expected_size = sizeof(header) + (header.primary_element_count + header.secondary_element_count + header.tertiary_element_count) * sizeof(math::Float3);
            if (expected_size != volume.asset.byte_size) throw std::runtime_error("Invalid Spectra RGB-grid volume asset payload size");
            volume.sigma_a.resize(header.primary_element_count);
            volume.sigma_s.resize(header.secondary_element_count);
            volume.emission.resize(header.tertiary_element_count);
            read_values(stream, volume.sigma_a);
            read_values(stream, volume.sigma_s);
            read_values(stream, volume.emission);
            if (!stream) throw std::runtime_error("Failed to read Spectra RGB-grid volume asset payload");
        }

        void load_volume_asset(NanoVdbVolume& volume, const std::filesystem::path& package_root) {
            std::ifstream stream{};
            const VolumeAssetHeader header = read_volume_asset_header(volume.asset, package_root, stream);
            if (header.kind != VolumeAssetKind::NanoVdb || header.primary_element_count == 0 || header.tertiary_element_count != volume_sample_count(header.resolution)) throw std::runtime_error("Invalid Spectra NanoVDB volume asset metadata");
            const std::uint64_t expected_size = sizeof(header) + (header.primary_element_count + header.secondary_element_count + header.tertiary_element_count) * sizeof(std::uint32_t);
            if (expected_size != volume.asset.byte_size) throw std::runtime_error("Invalid Spectra NanoVDB volume asset payload size");
            volume.majorant_resolution = header.resolution;
            volume.density_data.resize(header.primary_element_count);
            volume.temperature_data.resize(header.secondary_element_count);
            volume.majorant.resize(header.tertiary_element_count);
            read_values(stream, volume.density_data);
            read_values(stream, volume.temperature_data);
            read_values(stream, volume.majorant);
            if (!stream) throw std::runtime_error("Failed to read Spectra NanoVDB volume asset payload");
        }

        void load_texture_asset(ImageTexture& texture, const std::filesystem::path& package_root) {
            verify_asset(package_root, texture.asset, ".texture");
            const std::filesystem::path path = asset_path(package_root, texture.asset, ".texture");
            std::ifstream stream{path, std::ios::binary};
            TextureAssetHeader header{};
            stream.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (!stream || header.magic != TextureAssetHeader{}.magic || header.version != 1 || header.width == 0 || header.height == 0 || header.mip_count == 0) throw std::runtime_error(std::format("Invalid Spectra texture asset header: {}", path.string()));
            std::uint32_t width  = header.width;
            std::uint32_t height = header.height;
            std::uint64_t expected_texels{};
            texture.mip_offsets.clear();
            texture.mip_offsets.reserve(header.mip_count);
            for (std::uint32_t level = 0; level != header.mip_count; ++level) {
                texture.mip_offsets.push_back(expected_texels);
                expected_texels += static_cast<std::uint64_t>(width) * height;
                width  = std::max(1u, width / 2u);
                height = std::max(1u, height / 2u);
            }
            const std::uint32_t maximum_mips  = std::bit_width(std::max(header.width, header.height));
            const std::uint64_t expected_size = sizeof(header) + header.texel_count * sizeof(math::Float4);
            if (header.mip_count > maximum_mips || header.texel_count != expected_texels || expected_size != texture.asset.byte_size) throw std::runtime_error(std::format("Invalid Spectra texture asset payload size: {}", path.string()));
            texture.width  = header.width;
            texture.height = header.height;
            texture.texels.resize(header.texel_count);
            read_values(stream, texture.texels);
            if (!stream) throw std::runtime_error(std::format("Failed to read Spectra texture asset payload: {}", path.string()));
        }

        void copy_asset(AssetReference& reference, const std::string_view extension, const std::filesystem::path& source_root, const std::filesystem::path& target_root) {
            verify_asset(source_root, reference, extension);
            const std::filesystem::path source = asset_path(source_root, reference, extension);
            const std::filesystem::path target = asset_path(target_root, reference, extension);
            if (source == target) return;
            std::filesystem::create_directories(target.parent_path());
            if (std::filesystem::exists(target)) {
                verify_asset(target_root, reference, extension);
                return;
            }
            std::filesystem::copy_file(source, target, std::filesystem::copy_options::none);
        }

        [[nodiscard]] Scene parse_scene(const std::filesystem::path& path) {
            std::ifstream stream{path, std::ios::binary};
            if (!stream) throw std::runtime_error(std::format("Failed to open Spectra scene: {}", path.string()));
            const std::string json{
                std::istreambuf_iterator<char>{stream},
                std::istreambuf_iterator<char>{},
            };

            SceneHeader header{};
            constexpr glz::opts header_options{
                .error_on_unknown_keys = false,
                .error_on_missing_keys = true,
            };
            const glz::error_ctx header_error = glz::read<header_options>(header, json);
            if (header_error) throw std::runtime_error(std::format("Failed to read Spectra scene format_version {}:\n{}", path.string(), glz::format_error(header_error, json)));
            if (header.format_version != current_scene_format_version) throw std::runtime_error(std::format("Unsupported Spectra scene format_version {}", header.format_version));
            Scene scene{};
            constexpr glz::opts options{
                .error_on_unknown_keys = true,
                .error_on_missing_keys = true,
            };
            const glz::error_ctx error = glz::read<options>(scene, json);
            if (error) throw std::runtime_error(std::format("Failed to parse Spectra scene {}:\n{}", path.string(), glz::format_error(error, json)));
            return scene;
        }
    } // namespace

    SceneSummary inspect_scene(const std::filesystem::path& path) {
        const Scene scene                        = parse_scene(path);
        const std::filesystem::path package_root = path.parent_path();
        for (const Geometry& geometry : scene.resources.geometries)
            if (const TriangleMeshGeometry* mesh = std::get_if<TriangleMeshGeometry>(&geometry.data)) inspect_asset(package_root, mesh->asset, ".geometry");
        for (const ParticleSet& particles : scene.resources.particle_sets) inspect_asset(package_root, particles.asset, ".particles");
        for (const Volume& volume : scene.resources.volumes) {
            if (const DensityGridVolume* density = std::get_if<DensityGridVolume>(&volume.data))
                inspect_asset(package_root, density->asset, ".volume");
            else if (const RgbGridVolume* rgb = std::get_if<RgbGridVolume>(&volume.data))
                inspect_asset(package_root, rgb->asset, ".volume");
            else if (const NanoVdbVolume* nanovdb = std::get_if<NanoVdbVolume>(&volume.data))
                inspect_asset(package_root, nanovdb->asset, ".volume");
        }
        for (const Texture& texture : scene.resources.textures)
            if (const ImageTexture* image = std::get_if<ImageTexture>(&texture.data)) inspect_asset(package_root, image->asset, ".texture");

        SceneSummary summary{
            .scene_path   = std::filesystem::weakly_canonical(path),
            .name         = scene.name,
            .has_dynamics = scene.dynamic_setup.has_value(),
        };
        if (scene.dynamic_setup)
            for (const DynamicSystem& system : scene.dynamic_setup->systems)
                if (!std::ranges::contains(summary.provider_ids, system.provider_id)) summary.provider_ids.emplace_back(system.provider_id);
        std::ranges::sort(summary.provider_ids);
        for (const std::string& provider : summary.provider_ids) {
            const std::filesystem::path provider_path = package_root / std::filesystem::path{provider + ".spectra-plugin.dll"};
            std::error_code error{};
            const std::uint64_t byte_size = std::filesystem::file_size(provider_path, error);
            if (error || byte_size == 0) throw std::runtime_error(std::format("Missing or empty Spectra Provider Library: {}", provider_path.string()));
        }
        return summary;
    }

    Scene load_scene(const std::filesystem::path& path) {
        Scene scene                              = parse_scene(path);
        const std::filesystem::path package_root = path.parent_path();
        for (Geometry& geometry : scene.resources.geometries)
            if (TriangleMeshGeometry* mesh = std::get_if<TriangleMeshGeometry>(&geometry.data)) load_geometry_asset(*mesh, package_root);
        for (ParticleSet& particles : scene.resources.particle_sets) load_particle_asset(particles, package_root);
        for (Volume& volume : scene.resources.volumes) {
            if (DensityGridVolume* density = std::get_if<DensityGridVolume>(&volume.data))
                load_volume_asset(*density, package_root);
            else if (RgbGridVolume* rgb = std::get_if<RgbGridVolume>(&volume.data))
                load_volume_asset(*rgb, package_root);
            else if (NanoVdbVolume* nanovdb = std::get_if<NanoVdbVolume>(&volume.data))
                load_volume_asset(*nanovdb, package_root);
        }
        for (Texture& texture : scene.resources.textures)
            if (ImageTexture* image = std::get_if<ImageTexture>(&texture.data)) load_texture_asset(*image, package_root);
        scene.mark_all_changed();
        scene.acknowledge_changes();
        return scene;
    }

    void save_scene(Scene package, const std::filesystem::path& path, const std::filesystem::path& source_scene_path) {
        package.format_version                   = current_scene_format_version;
        const std::filesystem::path package_root = path.parent_path();
        const std::filesystem::path source_root  = source_scene_path.empty() ? package_root : source_scene_path.parent_path();
        for (Geometry& geometry : package.resources.geometries)
            if (TriangleMeshGeometry* mesh = std::get_if<TriangleMeshGeometry>(&geometry.data)) mesh->asset = write_geometry_asset(*mesh, package_root);
        for (ParticleSet& particles : package.resources.particle_sets) particles.asset = write_particle_asset(particles, package_root);
        for (Volume& volume : package.resources.volumes) {
            if (DensityGridVolume* density = std::get_if<DensityGridVolume>(&volume.data))
                density->asset = write_volume_asset(*density, package_root);
            else if (RgbGridVolume* rgb = std::get_if<RgbGridVolume>(&volume.data))
                rgb->asset = write_volume_asset(*rgb, package_root);
            else if (NanoVdbVolume* nanovdb = std::get_if<NanoVdbVolume>(&volume.data))
                if (!nanovdb->density_data.empty())
                    nanovdb->asset = write_volume_asset(*nanovdb, package_root);
                else
                    copy_asset(nanovdb->asset, ".volume", source_root, package_root);
        }
        for (Texture& texture : package.resources.textures)
            if (ImageTexture* image = std::get_if<ImageTexture>(&texture.data)) {
                if (!image->texels.empty())
                    image->asset = write_texture_asset(*image, package_root);
                else if (std::filesystem::exists(asset_path(package_root, image->asset, ".texture")))
                    verify_asset(package_root, image->asset, ".texture");
                else
                    copy_asset(image->asset, ".texture", source_root, package_root);
            }
        std::string json{};
        const glz::error_ctx error = glz::write<glz::opts{.prettify = true}>(package, json);
        if (error) throw std::runtime_error(std::format("Failed to serialize Spectra scene {}: {}", path.string(), glz::format_error(error, json)));
        std::filesystem::path temporary_path = path;
        temporary_path += ".tmp";
        std::ofstream stream{
            temporary_path,
            std::ios::binary | std::ios::trunc,
        };
        if (!stream) throw std::runtime_error(std::format("Failed to create Spectra scene: {}", temporary_path.string()));
        stream.write(json.data(), static_cast<std::streamsize>(json.size()));
        if (!stream) throw std::runtime_error(std::format("Failed to write Spectra scene: {}", temporary_path.string()));
        stream.close();
        if (!MoveFileExW(temporary_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error(std::format("Failed to commit Spectra scene {}: Win32 error {}", path.string(), GetLastError()));
    }
} // namespace spectra::scene
