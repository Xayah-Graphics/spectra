export module spectra.scene;

import std;

namespace spectra::scene {
    export struct Float2 {
        float x{};
        float y{};
        friend auto operator<=>(const Float2&, const Float2&) = default;
    };

    export struct Float3 {
        float x{};
        float y{};
        float z{};

        [[nodiscard]] constexpr Float3 operator+(const Float3 right) const noexcept {
            return {this->x + right.x, this->y + right.y, this->z + right.z};
        }
        [[nodiscard]] constexpr Float3 operator-(const Float3 right) const noexcept {
            return {this->x - right.x, this->y - right.y, this->z - right.z};
        }
        [[nodiscard]] constexpr Float3 operator-() const noexcept {
            return {-this->x, -this->y, -this->z};
        }
        [[nodiscard]] constexpr Float3 operator*(const float scale) const noexcept {
            return {this->x * scale, this->y * scale, this->z * scale};
        }
        [[nodiscard]] constexpr Float3 operator/(const float scale) const noexcept {
            return {this->x / scale, this->y / scale, this->z / scale};
        }
        [[nodiscard]] constexpr float dot(const Float3 right) const noexcept {
            return this->x * right.x + this->y * right.y + this->z * right.z;
        }
        [[nodiscard]] constexpr Float3 cross(const Float3 right) const noexcept {
            return {this->y * right.z - this->z * right.y, this->z * right.x - this->x * right.z, this->x * right.y - this->y * right.x};
        }
        [[nodiscard]] float length() const noexcept;
        [[nodiscard]] Float3 normalized() const noexcept;

        friend auto operator<=>(const Float3&, const Float3&) = default;
    };

    export struct Float4 {
        float x{};
        float y{};
        float z{};
        float w{};
        friend auto operator<=>(const Float4&, const Float4&) = default;
    };

    export struct UInt3 {
        std::uint32_t x{};
        std::uint32_t y{};
        std::uint32_t z{};
        friend auto operator<=>(const UInt3&, const UInt3&) = default;
    };

    export struct Transform {
        std::array<float, 16> matrix{
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };

        [[nodiscard]] Float3 transform_point(Float3 point) const noexcept;
        [[nodiscard]] Float3 transform_vector(Float3 vector) const noexcept;
        [[nodiscard]] Transform operator*(const Transform& child) const noexcept;
        [[nodiscard]] Transform inverse() const;
        [[nodiscard]] static Transform look_at(Float3 position, Float3 target, Float3 up) noexcept;

        friend auto operator<=>(const Transform&, const Transform&) = default;
    };

    export struct Bounds3 {
        Float3 minimum{};
        Float3 maximum{};

        [[nodiscard]] static Bounds3 empty() noexcept;
        void include(Float3 point) noexcept;
        void include(Bounds3 bounds) noexcept;
        [[nodiscard]] bool valid() const noexcept;
        [[nodiscard]] Float3 center() const noexcept;
        [[nodiscard]] Float3 diagonal() const noexcept;
        [[nodiscard]] float radius() const noexcept;
        [[nodiscard]] Bounds3 transformed(const Transform& transform) const noexcept;

        friend auto operator<=>(const Bounds3&, const Bounds3&) = default;
    };

    export struct CameraFrame {
        Float3 position{};
        Float3 right{1.0f, 0.0f, 0.0f};
        Float3 up{0.0f, 1.0f, 0.0f};
        Float3 forward{0.0f, 0.0f, -1.0f};
    };

    export struct CameraMatrices {
        std::array<float, 16> view{};
        std::array<float, 16> projection{};
        std::array<float, 16> view_projection{};
        std::array<float, 16> inverse_view_projection{};
    };

    export struct GeometryId {
        std::uint64_t value{};
        friend auto operator<=>(const GeometryId&, const GeometryId&) = default;
    };

    export struct ParticleSetId {
        std::uint64_t value{};
        friend auto operator<=>(const ParticleSetId&, const ParticleSetId&) = default;
    };

    export struct VolumeId {
        std::uint64_t value{};
        friend auto operator<=>(const VolumeId&, const VolumeId&) = default;
    };

    export struct TextureId {
        std::uint64_t value{};
        friend auto operator<=>(const TextureId&, const TextureId&) = default;
    };

    export struct MaterialId {
        std::uint64_t value{};
        friend auto operator<=>(const MaterialId&, const MaterialId&) = default;
    };

    export struct MediumId {
        std::uint64_t value{};
        friend auto operator<=>(const MediumId&, const MediumId&) = default;
    };

    export struct LightId {
        std::uint64_t value{};
        friend auto operator<=>(const LightId&, const LightId&) = default;
    };

    export struct CameraId {
        std::uint64_t value{};
        friend auto operator<=>(const CameraId&, const CameraId&) = default;
    };

    export struct FilmId {
        std::uint64_t value{};
        friend auto operator<=>(const FilmId&, const FilmId&) = default;
    };

    export struct SamplerId {
        std::uint64_t value{};
        friend auto operator<=>(const SamplerId&, const SamplerId&) = default;
    };

    export struct PrototypeId {
        std::uint64_t value{};
        friend auto operator<=>(const PrototypeId&, const PrototypeId&) = default;
    };

    export struct InstanceId {
        std::uint64_t value{};
        friend auto operator<=>(const InstanceId&, const InstanceId&) = default;
    };

    export struct ResourceRevision {
        std::uint64_t content{1};
        std::uint64_t topology{1};
        friend auto operator<=>(const ResourceRevision&, const ResourceRevision&) = default;
    };

    export struct AssetReference {
        std::string content_hash{};
        std::string relative_path{};
        std::uint64_t byte_size{};
        friend auto operator<=>(const AssetReference&, const AssetReference&) = default;
    };

    export enum class SpectrumColorSpace : std::uint8_t {
        Srgb,
        Rec2020,
        Aces2065_1,
    };

    export struct TriangleMeshGeometry {
        AssetReference asset{};
        std::vector<Float3> positions{};
        std::vector<Float3> normals{};
        std::vector<Float3> tangents{};
        std::vector<Float2> texture_coordinates{};
        std::vector<std::uint32_t> indices{};
    };

    export struct SphereGeometry {
        float radius{1.0f};
        float z_min{-1.0f};
        float z_max{1.0f};
        float phi_max{360.0f};
    };

    export struct BoxGeometry {
        Bounds3 bounds{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
    };

    export struct RectangleGeometry {
        Float2 minimum{-1.0f, -1.0f};
        Float2 maximum{1.0f, 1.0f};
    };

    export struct DiskGeometry {
        float height{};
        float radius{1.0f};
        float inner_radius{};
        float phi_max{360.0f};
    };

    export struct CylinderGeometry {
        float radius{1.0f};
        float z_min{-1.0f};
        float z_max{1.0f};
        float phi_max{360.0f};
    };

    export struct Geometry {
        GeometryId id{};
        std::string name{};
        ResourceRevision revision{};
        std::variant<TriangleMeshGeometry, SphereGeometry, BoxGeometry, RectangleGeometry, DiskGeometry, CylinderGeometry> data{};
    };

    export [[nodiscard]] Bounds3 geometry_bounds(const Geometry& geometry) noexcept;
    export [[nodiscard]] float surface_area(const Geometry& geometry) noexcept;

    export struct ParticleSet {
        ParticleSetId id{};
        std::string name{};
        ResourceRevision revision{};
        AssetReference asset{};
        std::vector<Float3> positions{};
        std::vector<float> radii{};
        std::vector<Float3> velocities{};
        std::vector<Float3> colors{};
        std::vector<float> temperatures{};
        MaterialId material{};
        std::vector<MaterialId> particle_materials{};
    };

    export [[nodiscard]] Bounds3 particle_bounds(const ParticleSet& particles) noexcept;

    export struct DensityGridVolume {
        UInt3 resolution{};
        AssetReference asset{};
        std::vector<float> density{};
        std::vector<float> temperature{};
        std::vector<float> emission_scale{};
    };

    export struct RgbGridVolume {
        UInt3 resolution{};
        SpectrumColorSpace color_space{SpectrumColorSpace::Srgb};
        AssetReference asset{};
        std::vector<Float3> sigma_a{};
        std::vector<Float3> sigma_s{};
        std::vector<Float3> emission{};
    };

    export struct NanoVdbVolume {
        AssetReference asset{};
        std::string density_grid{"density"};
        std::optional<std::string> temperature_grid{};
        UInt3 majorant_resolution{};
        std::vector<std::uint32_t> density_data{};
        std::vector<std::uint32_t> temperature_data{};
        std::vector<float> majorant{};
    };

    export struct ProceduralCloudVolume {
        float density{1.0f};
        float wispiness{1.0f};
        float frequency{5.0f};
    };

    export struct VolumeRegion {
        UInt3 minimum{};
        UInt3 maximum{};

        friend auto operator<=>(const VolumeRegion&, const VolumeRegion&) = default;
    };

    export struct Volume {
        VolumeId id{};
        std::string name{};
        ResourceRevision revision{};
        Bounds3 bounds{};
        Transform transform{};
        std::optional<VolumeRegion> dirty_region{};
        std::variant<DensityGridVolume, RgbGridVolume, NanoVdbVolume, ProceduralCloudVolume> data{};
    };

    export enum class TextureValueKind : std::uint8_t {
        Float,
        Spectrum,
    };

    export struct UvTextureMapping {
        Float2 scale{1.0f, 1.0f};
        Float2 offset{};
    };

    export struct PlanarTextureMapping {
        Float3 first_axis{1.0f, 0.0f, 0.0f};
        Float3 second_axis{0.0f, 1.0f, 0.0f};
        Float2 offset{};
        Transform texture_from_render{};
    };

    export struct SphericalTextureMapping {
        Transform texture_from_render{};
    };

    export struct CylindricalTextureMapping {
        Transform texture_from_render{};
    };

    export struct TextureMapping {
        std::variant<UvTextureMapping, PlanarTextureMapping, SphericalTextureMapping, CylindricalTextureMapping> data{UvTextureMapping{}};
    };

    export struct TextureMapping3D {
        Transform texture_from_render{};
    };

    export struct CheckerboardMapping {
        std::variant<TextureMapping, TextureMapping3D> data{TextureMapping{}};
    };

    export enum class TextureWrapMode : std::uint8_t {
        Repeat,
        Clamp,
        Black,
    };

    export enum class TextureColorSpace : std::uint8_t {
        Linear,
        Srgb,
        Aces2065_1,
        Rec2020,
    };

    export enum class TextureChannel : std::uint8_t {
        Red,
        Green,
        Blue,
        Alpha,
        Average,
        Luminance,
    };

    export enum class TextureFilter : std::uint8_t {
        Point,
        Bilinear,
        Trilinear,
        Ewa,
    };

    export enum class TextureSpectrumType : std::uint8_t {
        Albedo,
        Unbounded,
        Illuminant,
    };

    export enum class SpectrumEncoding : std::uint8_t {
        RgbAlbedo,
        RgbUnbounded,
        RgbIlluminant,
        Constant,
        Blackbody,
        PiecewiseLinear,
    };

    export struct SpectrumParameter {
        Float3 value{};
        TextureId texture{};
        SpectrumEncoding encoding{SpectrumEncoding::RgbAlbedo};
        SpectrumColorSpace color_space{SpectrumColorSpace::Srgb};
        float scalar{};
        float temperature{};
        std::vector<float> wavelengths{};
        std::vector<float> samples{};
    };

    export struct BlackbodySpectrum {
        float temperature{};
        float normalization{};

        explicit BlackbodySpectrum(float temperature) noexcept;

        [[nodiscard]] float evaluate(float wavelength) const noexcept;
    };

    export struct PiecewiseLinearSpectrum {
        std::vector<float> wavelengths{};
        std::vector<float> values{};

        [[nodiscard]] float evaluate(float wavelength) const noexcept;
    };

    export struct ConstantTexture {
        float scalar{};
        SpectrumParameter spectrum{};
    };

    export struct ImageTexture {
        AssetReference asset{};
        TextureMapping mapping{};
        TextureWrapMode wrap{TextureWrapMode::Repeat};
        TextureChannel channel{TextureChannel::Luminance};
        TextureFilter filter{TextureFilter::Bilinear};
        float maximum_anisotropy{8.0f};
        float scale{1.0f};
        bool invert{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::uint64_t> mip_offsets{};
        std::vector<Float4> texels{};
    };

    export struct CheckerboardTexture {
        TextureId first{};
        TextureId second{};
        CheckerboardMapping mapping{};
    };

    export struct ScaleTexture {
        TextureId first{};
        TextureId second{};
    };

    export struct MixTexture {
        TextureId first{};
        TextureId second{};
        TextureId amount{};
    };

    export struct DirectionMixTexture {
        TextureId first{};
        TextureId second{};
        Float3 direction{0.0f, 1.0f, 0.0f};
    };

    export struct BilerpTexture {
        std::array<float, 4> scalars{};
        std::array<SpectrumParameter, 4> spectra{};
        TextureMapping mapping{};
    };

    export struct Texture {
        TextureId id{};
        std::string name{};
        ResourceRevision revision{};
        TextureValueKind value_kind{TextureValueKind::Spectrum};
        TextureSpectrumType spectrum_type{TextureSpectrumType::Albedo};
        TextureColorSpace color_space{TextureColorSpace::Srgb};
        std::variant<ConstantTexture, ImageTexture, CheckerboardTexture, ScaleTexture, MixTexture, DirectionMixTexture, BilerpTexture> data{};
    };

    export struct FloatParameter {
        float value{};
        TextureId texture{};
    };

    export struct InterfaceMaterialData {};

    export struct DiffuseMaterialData {
        SpectrumParameter reflectance{{0.5f, 0.5f, 0.5f}, {}};
        TextureId normal_map{};
        TextureId bump_map{};
    };

    export struct DiffuseTransmissionMaterialData {
        SpectrumParameter reflectance{{0.25f, 0.25f, 0.25f}, {}};
        SpectrumParameter transmittance{{0.25f, 0.25f, 0.25f}, {}};
        float scale{1.0f};
        TextureId normal_map{};
        TextureId bump_map{};
    };

    export struct ConductorEtaK {
        SpectrumParameter eta{{1.0f, 1.0f, 1.0f}, {}, SpectrumEncoding::RgbUnbounded};
        SpectrumParameter k{{1.0f, 1.0f, 1.0f}, {}, SpectrumEncoding::RgbUnbounded};
    };

    export struct ConductorReflectance {
        SpectrumParameter reflectance{{0.5f, 0.5f, 0.5f}, {}, SpectrumEncoding::RgbAlbedo};
    };

    export struct MaterialRoughness {
        FloatParameter roughness{};
        std::optional<FloatParameter> u_roughness{};
        std::optional<FloatParameter> v_roughness{};
    };

    export struct CoatingLayer {
        FloatParameter thickness{0.01f, {}};
        SpectrumParameter albedo{{}, {}, SpectrumEncoding::RgbAlbedo};
        FloatParameter g{};
        std::int32_t max_depth{10};
        std::int32_t sample_count{1};
    };

    export struct ConductorMaterialData {
        std::variant<ConductorEtaK, ConductorReflectance> optics{};
        MaterialRoughness distribution{};
        bool remap_roughness{true};
        TextureId normal_map{};
        TextureId bump_map{};
    };

    export struct DielectricMaterialData {
        SpectrumParameter eta{{1.5f, 1.5f, 1.5f}, {}, SpectrumEncoding::Constant, SpectrumColorSpace::Srgb, 1.5f};
        MaterialRoughness distribution{};
        bool remap_roughness{true};
        TextureId normal_map{};
        TextureId bump_map{};
    };

    export struct ThinDielectricMaterialData {
        SpectrumParameter eta{{1.5f, 1.5f, 1.5f}, {}, SpectrumEncoding::Constant, SpectrumColorSpace::Srgb, 1.5f};
        TextureId normal_map{};
        TextureId bump_map{};
    };

    export struct CoatedDiffuseMaterialData {
        SpectrumParameter reflectance{{0.5f, 0.5f, 0.5f}, {}, SpectrumEncoding::RgbAlbedo};
        SpectrumParameter eta{{1.5f, 1.5f, 1.5f}, {}, SpectrumEncoding::Constant, SpectrumColorSpace::Srgb, 1.5f};
        MaterialRoughness interface{};
        CoatingLayer coating{};
        bool remap_roughness{true};
        TextureId normal_map{};
        TextureId bump_map{};
    };

    export struct CoatedConductorMaterialData {
        SpectrumParameter interface_eta{{1.5f, 1.5f, 1.5f}, {}, SpectrumEncoding::Constant, SpectrumColorSpace::Srgb, 1.5f};
        MaterialRoughness interface{};
        std::variant<ConductorEtaK, ConductorReflectance> optics{};
        MaterialRoughness conductor{};
        CoatingLayer coating{};
        bool remap_roughness{true};
        TextureId normal_map{};
        TextureId bump_map{};
    };

    export struct MixMaterialData {
        MaterialId first{};
        MaterialId second{};
        FloatParameter amount{0.5f, {}};
    };

    export struct MaterialResource {
        MaterialId id{};
        std::string name{};
        ResourceRevision revision{};
        std::variant<InterfaceMaterialData, DiffuseMaterialData, DiffuseTransmissionMaterialData, ConductorMaterialData, DielectricMaterialData, ThinDielectricMaterialData, CoatedDiffuseMaterialData, CoatedConductorMaterialData, MixMaterialData> data{};
    };

    export struct MediumInterface {
        MediumId inside{};
        MediumId outside{};
    };

    export struct HomogeneousMedium {
        SpectrumParameter sigma_a{{}, {}, SpectrumEncoding::RgbUnbounded};
        SpectrumParameter sigma_s{{}, {}, SpectrumEncoding::RgbUnbounded};
        SpectrumParameter emission{{}, {}, SpectrumEncoding::RgbIlluminant};
        float density_scale{1.0f};
        float emission_scale{1.0f};
        float anisotropy{};
    };

    export struct VolumeMedium {
        VolumeId volume{};
        SpectrumParameter sigma_a{{}, {}, SpectrumEncoding::RgbUnbounded};
        SpectrumParameter sigma_s{{}, {}, SpectrumEncoding::RgbUnbounded};
        SpectrumParameter emission{{}, {}, SpectrumEncoding::RgbIlluminant};
        float density_scale{1.0f};
        float emission_scale{1.0f};
        float anisotropy{};
        float temperature_scale{1.0f};
        float temperature_offset{};
        float minimum_emission_temperature{100.0f};
        bool blackbody_emission{};
    };

    export struct Medium {
        MediumId id{};
        std::string name{};
        ResourceRevision revision{};
        std::variant<HomogeneousMedium, VolumeMedium> data{};
    };

    export enum class EmissionSidedness : std::uint8_t {
        Front,
        Both,
    };

    export struct PointLight {
        Transform transform{};
        SpectrumParameter intensity{{1.0f, 1.0f, 1.0f}, {}, SpectrumEncoding::RgbIlluminant};
        float scale{1.0f};
    };

    export struct SpotLight {
        Transform transform{};
        SpectrumParameter intensity{{1.0f, 1.0f, 1.0f}, {}, SpectrumEncoding::RgbIlluminant};
        float scale{1.0f};
        float cone_angle{30.0f};
        float cone_delta{5.0f};
    };

    export struct DistantLight {
        Transform transform{};
        SpectrumParameter radiance{{1.0f, 1.0f, 1.0f}, {}, SpectrumEncoding::RgbIlluminant};
        float scale{1.0f};
    };

    export struct DiffuseAreaLight {
        SpectrumParameter radiance{{1.0f, 1.0f, 1.0f}, {}, SpectrumEncoding::RgbIlluminant};
        EmissionSidedness sidedness{EmissionSidedness::Front};
        float scale{1.0f};
        std::optional<float> power{};
        TextureId emission_texture{};
    };

    export struct InfiniteLight {
        SpectrumParameter radiance{{1.0f, 1.0f, 1.0f}, {}, SpectrumEncoding::RgbIlluminant};
        Transform transform{};
        float scale{1.0f};
        TextureId emission_texture{};
    };

    export struct PortalInfiniteLight {
        InfiniteLight environment{};
        std::vector<std::array<Float3, 4>> portals{};
    };

    export struct Light {
        LightId id{};
        std::string name{};
        ResourceRevision revision{};
        std::variant<PointLight, SpotLight, DistantLight, DiffuseAreaLight, InfiniteLight, PortalInfiniteLight> data{};
    };

    export struct Primitive {
        GeometryId geometry{};
        ParticleSetId particles{};
        VolumeId volume{};
        MaterialId material{};
        LightId area_light{};
        MediumInterface media{};
        TextureId alpha{};
        std::vector<MaterialId> face_materials{};
        bool reverse_orientation{};
        Transform transform{};
    };

    export struct Prototype {
        PrototypeId id{};
        std::string name{};
        ResourceRevision revision{};
        std::vector<Primitive> primitives{};
    };

    export struct Instance {
        InstanceId id{};
        std::string name{};
        ResourceRevision revision{};
        PrototypeId prototype{};
        Transform transform{};
        bool visible{true};
    };

    export struct CameraScreen {
        Float2 minimum{-1.0f, -1.0f};
        Float2 maximum{1.0f, 1.0f};
    };

    export struct PerspectiveCameraData {
        float vertical_fov{45.0f};
        CameraScreen screen{};
        float lens_radius{};
        float focal_distance{1.0f};
        float near_plane{0.01f};
        float far_plane{1000.0f};
    };

    export struct OrthographicCameraData {
        CameraScreen screen{};
        float lens_radius{};
        float focal_distance{1.0f};
        float near_plane{0.01f};
        float far_plane{1000.0f};
    };

    export struct CameraResource {
        CameraId id{};
        std::string name{};
        ResourceRevision revision{};
        Transform transform{};
        float exposure_time{1.0f};
        MediumId medium{};
        std::variant<PerspectiveCameraData, OrthographicCameraData> data{};

        [[nodiscard]] CameraFrame frame() const noexcept;
        [[nodiscard]] CameraMatrices matrices() const noexcept;
    };

    export enum class FilterKind : std::uint8_t {
        Box,
        Gaussian,
        Mitchell,
        Sinc,
        Triangle,
    };

    export struct Filter {
        FilterKind kind{FilterKind::Box};
        Float2 radius{0.5f, 0.5f};
        float sigma{0.5f};
        float b{1.0f / 3.0f};
        float c{1.0f / 3.0f};
        float tau{3.0f};

        friend auto operator<=>(const Filter&, const Filter&) = default;
    };

    export struct Film {
        FilmId id{};
        std::string name{};
        ResourceRevision revision{};
        std::array<std::uint32_t, 2> resolution{1440, 900};
        std::array<std::uint32_t, 2> pixel_minimum{};
        std::array<std::uint32_t, 2> pixel_maximum{1440, 900};
        float exposure{};
        float iso{100.0f};
        SpectrumColorSpace color_space{SpectrumColorSpace::Srgb};
        std::vector<float> sensor_response{};
        std::array<float, 9> sensor_to_output_rgb{3.240479f, -1.537150f, -0.498535f, -0.969256f, 1.875991f, 0.041556f, 0.055648f, -0.204043f, 1.057311f};
        std::optional<float> maximum_component_value{};
        Filter filter{};
        bool gbuffer{};
        bool gbuffer_camera_space{true};

        friend auto operator<=>(const Film&, const Film&) = default;
    };

    export enum class SamplerKind : std::uint8_t {
        Independent,
        Stratified,
        Halton,
        Sobol,
        PaddedSobol,
        ZSobol,
        Pmj02bn,
    };

    export enum class SamplerRandomization : std::uint8_t {
        None,
        PermuteDigits,
        FastOwen,
        Owen,
    };

    export enum class LightSamplerKind : std::uint8_t {
        Uniform,
        Power,
        Bvh,
    };

    export struct TransportSettings {
        std::uint32_t maximum_depth{5};
        LightSamplerKind light_sampler{LightSamplerKind::Bvh};
        bool regularize{};

        friend auto operator<=>(const TransportSettings&, const TransportSettings&) = default;
    };

    export struct Sampler {
        SamplerId id{};
        std::string name{};
        ResourceRevision revision{};
        SamplerKind kind{SamplerKind::Independent};
        std::uint32_t samples_per_pixel{1};
        std::uint32_t seed{};
        bool jitter{true};
        std::uint32_t x_strata{1};
        std::uint32_t y_strata{1};
        SamplerRandomization randomization{SamplerRandomization::Owen};

        friend auto operator<=>(const Sampler&, const Sampler&) = default;
    };


    export inline constexpr std::uint32_t current_scene_format_version = 21;

    export struct DynamicSystemId {
        std::string value{};
        friend auto operator<=>(const DynamicSystemId&, const DynamicSystemId&) = default;
    };

    export enum class DynamicResourceKind : std::uint8_t {
        Instance,
        Geometry,
        ParticleSet,
        Volume,
    };

    export enum class DynamicParameterKind : std::uint8_t {
        Boolean,
        Integer,
        Float,
        Float3,
        Enumeration,
    };

    export struct DynamicParameterValue {
        DynamicParameterKind kind{DynamicParameterKind::Float};
        std::int64_t integer{};
        std::array<double, 3> floating{};
        friend auto operator<=>(const DynamicParameterValue&, const DynamicParameterValue&) = default;
    };

    export struct DynamicParameterSetting {
        std::string id{};
        DynamicParameterValue value{};
        friend auto operator<=>(const DynamicParameterSetting&, const DynamicParameterSetting&) = default;
    };

    export struct DynamicPortBinding {
        std::string port{};
        DynamicResourceKind resource_kind{DynamicResourceKind::Geometry};
        std::uint64_t resource{};
        friend auto operator<=>(const DynamicPortBinding&, const DynamicPortBinding&) = default;
    };

    export struct DynamicSystem {
        DynamicSystemId id{};
        std::string name{};
        std::string provider{};
        bool enabled{true};
        bool visible{true};
        std::vector<DynamicParameterSetting> parameters{};
        std::vector<DynamicPortBinding> bindings{};
        friend auto operator<=>(const DynamicSystem&, const DynamicSystem&) = default;
    };

    export struct DynamicClock {
        double step_seconds{1.0 / 120.0};
        std::uint64_t start_step{};
        std::optional<std::uint64_t> end_step{};
        bool loop{};
        friend auto operator<=>(const DynamicClock&, const DynamicClock&) = default;
    };

    export struct DynamicSetup {
        DynamicClock clock{};
        std::uint64_t seed{};
        std::vector<DynamicSystem> systems{};
        friend auto operator<=>(const DynamicSetup&, const DynamicSetup&) = default;
    };

    export struct ResourceTables {
        std::vector<Geometry> geometries{};
        std::vector<ParticleSet> particle_sets{};
        std::vector<Volume> volumes{};
        std::vector<Texture> textures{};
        std::vector<MaterialResource> materials{};
        std::vector<Medium> media{};
        std::vector<Light> lights{};
        std::vector<CameraResource> cameras{};
        std::vector<Film> films{};
        std::vector<Sampler> samplers{};
        std::vector<Prototype> prototypes{};
        std::vector<Instance> instances{};
    };

    export enum class SceneChange : std::uint16_t {
        None          = 0,
        Geometry      = 1 << 0,
        Transform     = 1 << 1,
        Texture       = 1 << 2,
        Material      = 1 << 3,
        Light         = 1 << 4,
        Medium        = 1 << 5,
        Volume        = 1 << 6,
        Camera        = 1 << 7,
        Film          = 1 << 8,
        Sampler       = 1 << 9,
        Metadata      = 1 << 10,
        Transport     = 1 << 11,
        Visualization = 1 << 12,
        All           = 0x1fff,
    };

    export [[nodiscard]] constexpr SceneChange operator|(const SceneChange left, const SceneChange right) noexcept {
        return static_cast<SceneChange>(std::to_underlying(left) | std::to_underlying(right));
    }

    export [[nodiscard]] constexpr SceneChange operator&(const SceneChange left, const SceneChange right) noexcept {
        return static_cast<SceneChange>(std::to_underlying(left) & std::to_underlying(right));
    }

    export struct SceneRevision {
        std::uint64_t value{1};
        SceneChange changes{SceneChange::All};

        friend auto operator<=>(const SceneRevision&, const SceneRevision&) = default;
    };

    export struct SceneView {
        const ResourceTables& resources;
        const CameraResource& camera;
        const Film& film;
        const Sampler& sampler;
        const TransportSettings& transport;
        SceneRevision revision{};

        [[nodiscard]] Bounds3 bounds() const noexcept;
        [[nodiscard]] std::optional<Bounds3> local_bounds(InstanceId instance) const noexcept;
        [[nodiscard]] std::optional<Bounds3> bounds(std::span<const InstanceId> instances) const noexcept;
    };

    export struct Scene {
        std::uint32_t format_version{current_scene_format_version};
        std::string name{};
        ResourceTables resources{};
        CameraId active_camera{};
        FilmId active_film{};
        SamplerId active_sampler{};
        TransportSettings transport{};
        std::optional<DynamicSetup> dynamic_setup{};

        [[nodiscard]] SceneView view() const noexcept;
        [[nodiscard]] const CameraResource& camera() const noexcept;
        [[nodiscard]] const Film& film() const noexcept;
        [[nodiscard]] const Sampler& sampler() const noexcept;
        [[nodiscard]] SceneRevision revision() const noexcept;
        void acknowledge_changes() noexcept;
        void mark_all_changed() noexcept;

    private:
        friend struct SceneUpdate;

        void publish(SceneChange changes) noexcept;

        SceneRevision current_revision{};
    };

    export struct SceneUpdate {
        explicit SceneUpdate(Scene& scene) noexcept;

        void begin_frame() noexcept;
        void commit_frame() noexcept;
        void mark(SceneChange changes) noexcept;

        void update_triangle_mesh(GeometryId geometry, std::span<const Float3> positions, std::span<const Float3> normals, std::span<const Float3> tangents, std::span<const Float2> texture_coordinates, std::span<const std::uint32_t> indices);
        void update_particle_set(ParticleSetId particles, std::span<const Float3> positions, std::span<const float> radii, std::span<const Float3> velocities, std::span<const Float3> colors, std::span<const float> temperatures, std::span<const MaterialId> particle_materials);
        void update_density_grid(VolumeId volume, VolumeRegion region, std::span<const float> density, std::span<const float> temperature, std::span<const float> emission_scale);
        void update_rgb_grid(VolumeId volume, VolumeRegion region, std::span<const Float3> sigma_a, std::span<const Float3> sigma_s, std::span<const Float3> emission);
        void update_transform(InstanceId instance, Transform transform);
        void update_dynamic_setup(std::optional<DynamicSetup> setup);

    private:
        void publish(SceneChange changes) noexcept;

        Scene* scene{};
        SceneChange frame_changes{SceneChange::None};
        bool frame_open{};
    };
} // namespace spectra::scene
