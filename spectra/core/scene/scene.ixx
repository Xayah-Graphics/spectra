export module spectra.scene;

export import spectra.math;

import std;

namespace spectra::scene {
    export struct CameraFrame {
        math::Float3 position{};
        math::Float3 right{1.0f, 0.0f, 0.0f};
        math::Float3 up{0.0f, 1.0f, 0.0f};
        math::Float3 forward{0.0f, 0.0f, -1.0f};
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

    export struct SphereSetId {
        std::uint64_t value{};
        friend auto operator<=>(const SphereSetId&, const SphereSetId&) = default;
    };

    export struct ParticleSetId {
        std::uint64_t value{};
        friend auto operator<=>(const ParticleSetId&, const ParticleSetId&) = default;
    };

    export struct VolumeId {
        std::uint64_t value{};
        friend auto operator<=>(const VolumeId&, const VolumeId&) = default;
    };

    export struct NeuralFieldId {
        std::uint64_t value{};
        friend auto operator<=>(const NeuralFieldId&, const NeuralFieldId&) = default;
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

    export enum class SpectrumColorSpace : std::uint8_t {
        Srgb,
        Rec2020,
        Aces2065_1,
    };

    export struct TriangleMeshGeometry {
        std::string source{};
        std::vector<math::Float3> positions{};
        std::vector<math::Float3> normals{};
        std::vector<math::Float3> tangents{};
        std::vector<math::Float2> texture_coordinates{};
        std::vector<std::uint32_t> indices{};
    };

    export struct SphereGeometry {
        float radius{1.0f};
        float z_min{-1.0f};
        float z_max{1.0f};
        float phi_max{360.0f};
    };

    export struct BoxGeometry {
        math::Bounds3 bounds{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
    };

    export struct RectangleGeometry {
        math::Float2 minimum{-1.0f, -1.0f};
        math::Float2 maximum{1.0f, 1.0f};
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

    export [[nodiscard]] math::Bounds3 geometry_bounds(const Geometry& geometry) noexcept;
    export [[nodiscard]] float surface_area(const Geometry& geometry) noexcept;
    export [[nodiscard]] float surface_area(const Geometry& geometry, const math::Transform& transform) noexcept;

    export struct SphereSet {
        SphereSetId id{};
        std::string name{};
        ResourceRevision revision{};
        std::string source{};
        std::vector<math::Float3> positions{};
        std::vector<float> radii{};
    };

    export [[nodiscard]] math::Bounds3 sphere_set_bounds(const SphereSet& spheres) noexcept;

    export enum class FieldKind : std::uint8_t {
        Float,
        Float3,
        UInt32,
        MacFloat3,
    };

    export enum class VolumeFieldSampling : std::uint8_t {
        Cell,
        Vertex,
    };

    export enum class VolumeVectorSpace : std::uint8_t {
        Grid,
        Local,
        World,
    };

    export enum class VisualizationDepthMode : std::uint8_t {
        Tested,
        XRay,
        Overlay,
    };

    export enum class VisualizationCompositionDomain : std::uint8_t {
        SceneLinear,
        DisplayReferred,
    };

    export enum class VisualizationColorMap : std::uint8_t {
        Viridis,
        Turbo,
        CoolWarm,
        Grayscale,
    };

    export enum class VolumeDiagnosticMode : std::uint8_t {
        Off,
        Slice,
        Cells,
        RayMarch,
        MaximumIntensityProjection,
        Isosurface,
        Glyphs,
        Streamlines,
        Lic,
    };

    export enum class FieldMapping : std::uint8_t {
        Value,
        Magnitude,
        X,
        Y,
        Z,
        Divergence,
        CurlMagnitude,
        QCriterion,
    };

    export struct VolumeDiagnostics {
        std::string field_id{};
        VolumeDiagnosticMode mode{VolumeDiagnosticMode::Off};
        FieldMapping mapping{FieldMapping::Value};
        VisualizationDepthMode depth_mode{VisualizationDepthMode::Tested};
        VisualizationColorMap color_map{VisualizationColorMap::Viridis};
        math::Float4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float minimum{};
        float maximum{1.0f};
        float slice_position{0.5f};
        float opacity{1.0f};
        float threshold{0.5f};
        float scale{0.1f};
        float width{1.5f};
        std::uint32_t axis{2};
        std::uint32_t sampling{8};
        std::uint32_t steps{32};
        std::uint32_t category_mask{0xfffffffeu};
        friend auto operator<=>(const VolumeDiagnostics&, const VolumeDiagnostics&) = default;
    };

    export struct VolumeField {
        std::string id{};
        std::string name{};
        std::string unit{};
        FieldKind kind{FieldKind::Float};
        VolumeFieldSampling sampling{VolumeFieldSampling::Cell};
        VolumeVectorSpace vector_space{VolumeVectorSpace::Local};
        std::vector<float> scalar_values{};
        std::vector<math::Float3> vector_values{};
        std::vector<std::uint32_t> integer_values{};
        std::array<std::vector<float>, 3> mac_values{};
    };

    export enum class ParticleDisplayMode : std::uint8_t {
        Points,
        Discs,
        Spheres,
    };

    export struct ParticleField {
        std::string id{};
        std::string name{};
        std::string unit{};
        FieldKind kind{FieldKind::Float};
        VolumeVectorSpace vector_space{VolumeVectorSpace::Local};
    };

    export struct ParticleVisualization {
        std::string field_id{};
        ParticleDisplayMode display{ParticleDisplayMode::Discs};
        FieldMapping mapping{FieldMapping::Value};
        VisualizationDepthMode depth_mode{VisualizationDepthMode::Tested};
        VisualizationColorMap color_map{VisualizationColorMap::Viridis};
        math::Float4 color{0.18f, 0.55f, 1.0f, 1.0f};
        float minimum{};
        float maximum{1.0f};
        float radius_scale{1.0f};
        float point_size{2.0f};
        friend auto operator<=>(const ParticleVisualization&, const ParticleVisualization&) = default;
    };

    export struct ParticleDiagnostics {
        std::string vector_field{};
        VisualizationColorMap color_map{VisualizationColorMap::Turbo};
        float minimum{};
        float maximum{1.0f};
        float scale{0.1f};
        float width{1.5f};
        std::uint32_t sampling{8u};
        friend auto operator<=>(const ParticleDiagnostics&, const ParticleDiagnostics&) = default;
    };

    export struct ParticleSet {
        ParticleSetId id{};
        std::string name{};
        ResourceRevision revision{};
        math::Bounds3 domain{};
        math::Transform transform{};
        float radius{0.01f};
        std::vector<ParticleField> fields{};
        ParticleVisualization visualization{};
        ParticleDiagnostics diagnostics{};
        bool visible{true};
    };

    export [[nodiscard]] math::Bounds3 particle_set_bounds(const ParticleSet& particles) noexcept;

    export struct GridVolume {
        math::UInt3 resolution{};
        std::string source{};
        std::vector<VolumeField> fields{};
    };

    export struct ProceduralCloudVolume {
        float density{1.0f};
        float wispiness{1.0f};
        float frequency{5.0f};
    };

    export struct VolumeRegion {
        math::UInt3 minimum{};
        math::UInt3 maximum{};

        friend auto operator<=>(const VolumeRegion&, const VolumeRegion&) = default;
    };

    export enum class TextureValueKind : std::uint8_t {
        Float,
        Spectrum,
    };

    export struct UvTextureMapping {
        math::Float2 scale{1.0f, 1.0f};
        math::Float2 offset{};
    };

    export struct PlanarTextureMapping {
        math::Float3 first_axis{1.0f, 0.0f, 0.0f};
        math::Float3 second_axis{0.0f, 1.0f, 0.0f};
        math::Float2 offset{};
        math::Transform texture_from_render{};
    };

    export struct SphericalTextureMapping {
        math::Transform texture_from_render{};
    };

    export struct CylindricalTextureMapping {
        math::Transform texture_from_render{};
    };

    export struct TextureMapping {
        std::variant<UvTextureMapping, PlanarTextureMapping, SphericalTextureMapping, CylindricalTextureMapping> data{UvTextureMapping{}};
    };

    export struct TextureMapping3D {
        math::Transform texture_from_render{};
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
        math::Float3 value{};
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
        std::string source{};
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
        std::vector<math::Float4> texels{};
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
        math::Float3 direction{0.0f, 1.0f, 0.0f};
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

    export struct Material {
        MaterialId id{};
        std::string name{};
        ResourceRevision revision{};
        std::variant<InterfaceMaterialData, DiffuseMaterialData, DiffuseTransmissionMaterialData, ConductorMaterialData, DielectricMaterialData, ThinDielectricMaterialData, CoatedDiffuseMaterialData, CoatedConductorMaterialData, MixMaterialData> data{};
    };

    export struct MediumInterface {
        MediumId inside{};
        MediumId outside{};
    };

    export struct VolumeRendering {
        std::string density_field{};
        std::string temperature_field{};
        std::string emission_scale_field{};
        std::string sigma_a_field{};
        std::string sigma_s_field{};
        std::string emission_field{};
        SpectrumColorSpace field_color_space{SpectrumColorSpace::Srgb};
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

    export struct Volume {
        VolumeId id{};
        std::string name{};
        ResourceRevision revision{};
        math::Bounds3 domain{};
        math::Transform transform{};
        std::variant<GridVolume, ProceduralCloudVolume> data{};
        VolumeRendering rendering{};
        VolumeDiagnostics diagnostics{};
        MediumId exterior_medium{};
        bool visible{true};
    };

    export struct NeuralFieldDiagnostics {
        bool occupancy_grid{};
    };

    export struct NeuralField {
        static constexpr math::Bounds3 local_bounds{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};

        NeuralFieldId id{};
        std::string name{};
        math::Transform transform{};
        NeuralFieldDiagnostics diagnostics{};
        bool visible{true};
    };

    export struct Medium {
        MediumId id{};
        std::string name{};
        ResourceRevision revision{};
        SpectrumParameter sigma_a{{}, {}, SpectrumEncoding::RgbUnbounded};
        SpectrumParameter sigma_s{{}, {}, SpectrumEncoding::RgbUnbounded};
        SpectrumParameter emission{{}, {}, SpectrumEncoding::RgbIlluminant};
        float density_scale{1.0f};
        float emission_scale{1.0f};
        float anisotropy{};
    };

    export enum class EmissionSidedness : std::uint8_t {
        Front,
        Both,
    };

    export struct PointLight {
        math::Transform transform{};
        SpectrumParameter intensity{{1.0f, 1.0f, 1.0f}, {}, SpectrumEncoding::RgbIlluminant};
        float scale{1.0f};
    };

    export struct SpotLight {
        math::Transform transform{};
        SpectrumParameter intensity{{1.0f, 1.0f, 1.0f}, {}, SpectrumEncoding::RgbIlluminant};
        float scale{1.0f};
        float cone_angle{30.0f};
        float cone_delta{5.0f};
    };

    export struct DistantLight {
        math::Transform transform{};
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
        math::Transform transform{};
        float scale{1.0f};
        TextureId emission_texture{};
    };

    export struct PortalInfiniteLight {
        InfiniteLight environment{};
        std::vector<std::array<math::Float3, 4>> portals{};
    };

    export struct Light {
        LightId id{};
        std::string name{};
        ResourceRevision revision{};
        std::variant<PointLight, SpotLight, DistantLight, DiffuseAreaLight, InfiniteLight, PortalInfiniteLight> data{};
    };

    export struct Primitive {
        GeometryId geometry{};
        SphereSetId spheres{};
        MaterialId material{};
        LightId area_light{};
        MediumInterface media{};
        TextureId alpha{};
        std::vector<MaterialId> face_materials{};
        bool reverse_orientation{};
        math::Transform transform{};
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
        math::Transform transform{};
        bool visible{true};
    };

    export struct ScreenWindow {
        math::Float2 minimum{-1.0f, -1.0f};
        math::Float2 maximum{1.0f, 1.0f};
    };

    export struct PerspectiveCameraData {
        float vertical_fov{45.0f};
        ScreenWindow screen_window{};
        float lens_radius{};
        float focal_distance{1.0f};
        float near_plane{0.01f};
        float far_plane{1000.0f};
    };

    export struct OrthographicCameraData {
        ScreenWindow screen_window{};
        float lens_radius{};
        float focal_distance{1.0f};
        float near_plane{0.01f};
        float far_plane{1000.0f};
    };

    export struct Camera {
        CameraId id{};
        std::string name{};
        ResourceRevision revision{};
        math::Transform transform{};
        float exposure_time{1.0f};
        MediumId medium{};
        std::variant<PerspectiveCameraData, OrthographicCameraData> data{};

        [[nodiscard]] CameraFrame frame() const noexcept;
        [[nodiscard]] CameraMatrices matrices() const;
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
        math::Float2 radius{0.5f, 0.5f};
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


    export struct DynamicSystemId {
        std::string value{};
        friend auto operator<=>(const DynamicSystemId&, const DynamicSystemId&) = default;
    };

    export enum class DynamicSceneResourceKind : std::uint8_t {
        Geometry,
        SphereSet,
        ParticleSet,
        Volume,
        NeuralField,
    };

    export enum class VisualizationViewKind : std::uint8_t {
        Segments = 1,
        Vectors  = 2,
        Image    = 3,
        Surface  = 4,
    };

    export enum class VisualizationColorSource : std::uint8_t {
        Element,
        Uniform,
        Scalar,
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
        std::string parameter_id{};
        DynamicParameterValue value{};
        friend auto operator<=>(const DynamicParameterSetting&, const DynamicParameterSetting&) = default;
    };

    export struct DynamicSceneBinding {
        std::string dataset_id{};
        std::uint64_t resource_id{};
        friend auto operator<=>(const DynamicSceneBinding&, const DynamicSceneBinding&) = default;
    };

    export struct SegmentVisualization {
        float width{1.0f};
        float scalar_minimum{};
        float scalar_maximum{1.0f};
        VisualizationColorSource color_source{VisualizationColorSource::Element};
        VisualizationColorMap color_map{VisualizationColorMap::Viridis};
        friend auto operator<=>(const SegmentVisualization&, const SegmentVisualization&) = default;
    };

    export struct VectorVisualization {
        float width{1.0f};
        float scale{1.0f};
        float scalar_minimum{};
        float scalar_maximum{1.0f};
        VisualizationColorSource color_source{VisualizationColorSource::Element};
        VisualizationColorMap color_map{VisualizationColorMap::Viridis};
        friend auto operator<=>(const VectorVisualization&, const VectorVisualization&) = default;
    };

    export struct ImageVisualization {
        math::Float4 screen_rect{0.02f, 0.02f, 0.32f, 0.32f};
        friend auto operator<=>(const ImageVisualization&, const ImageVisualization&) = default;
    };

    export struct SurfaceVisualization {
        float scalar_minimum{};
        float scalar_maximum{1.0f};
        VisualizationColorSource color_source{VisualizationColorSource::Element};
        VisualizationColorMap color_map{VisualizationColorMap::Viridis};
        friend auto operator<=>(const SurfaceVisualization&, const SurfaceVisualization&) = default;
    };

    export struct DynamicVisualizationView {
        std::string dataset_id{};
        std::string name{};
        VisualizationDepthMode depth_mode{VisualizationDepthMode::Tested};
        VisualizationCompositionDomain composition_domain{VisualizationCompositionDomain::DisplayReferred};
        InstanceId anchor{};
        math::Float4 color{1.0f, 1.0f, 1.0f, 1.0f};
        bool visible{true};
        std::variant<SegmentVisualization, VectorVisualization, ImageVisualization, SurfaceVisualization> data{SegmentVisualization{}};
        friend auto operator<=>(const DynamicVisualizationView&, const DynamicVisualizationView&) = default;
    };

    export [[nodiscard]] constexpr VisualizationViewKind visualization_view_kind(const DynamicVisualizationView& view) noexcept {
        return std::visit([]<typename Type>(const Type&) {
            if constexpr (std::same_as<Type, SegmentVisualization>) return VisualizationViewKind::Segments;
            else if constexpr (std::same_as<Type, VectorVisualization>) return VisualizationViewKind::Vectors;
            else if constexpr (std::same_as<Type, ImageVisualization>) return VisualizationViewKind::Image;
            else return VisualizationViewKind::Surface;
        }, view.data);
    }

    export struct DynamicSystem {
        DynamicSystemId id{};
        std::string name{};
        std::string provider_id{};
        bool enabled{true};
        bool visible{true};
        std::vector<DynamicParameterSetting> parameters{};
        std::vector<DynamicSceneBinding> scene_bindings{};
        std::vector<DynamicVisualizationView> visualizations{};
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

    export struct SceneResources {
        std::vector<Geometry> geometries{};
        std::vector<SphereSet> sphere_sets{};
        std::vector<ParticleSet> particle_sets{};
        std::vector<Volume> volumes{};
        std::vector<NeuralField> neural_fields{};
        std::vector<Texture> textures{};
        std::vector<Material> materials{};
        std::vector<Medium> media{};
        std::vector<Light> lights{};
        std::vector<Camera> cameras{};
        std::vector<Film> films{};
        std::vector<Sampler> samplers{};
        std::vector<Prototype> prototypes{};
        std::vector<Instance> instances{};
    };

    export enum class SceneChange : std::uint16_t {
        None        = 0,
        Geometry    = 1 << 0,
        Transform   = 1 << 1,
        Texture     = 1 << 2,
        Material    = 1 << 3,
        Light       = 1 << 4,
        Medium      = 1 << 5,
        Volume      = 1 << 6,
        Camera      = 1 << 7,
        Film        = 1 << 8,
        Sampler     = 1 << 9,
        Metadata    = 1 << 10,
        Transport   = 1 << 11,
        Structure   = 1 << 12,
        NeuralField = 1 << 13,
        Particle     = 1 << 14,
        All          = 0x7fff,
    };

    export [[nodiscard]] constexpr SceneChange operator|(const SceneChange left, const SceneChange right) noexcept {
        return static_cast<SceneChange>(std::to_underlying(left) | std::to_underlying(right));
    }

    export [[nodiscard]] constexpr SceneChange operator&(const SceneChange left, const SceneChange right) noexcept {
        return static_cast<SceneChange>(std::to_underlying(left) & std::to_underlying(right));
    }

    export struct SceneRevision {
        std::uint64_t number{1};
        SceneChange changes{SceneChange::All};

        friend auto operator<=>(const SceneRevision&, const SceneRevision&) = default;
    };

    export struct SceneView {
        const SceneResources& resources;
        const Camera& camera;
        const Film& film;
        const Sampler& sampler;
        const TransportSettings& transport;
        SceneRevision revision{};

        [[nodiscard]] math::Bounds3 bounds() const noexcept;
        [[nodiscard]] std::optional<math::Bounds3> local_bounds(InstanceId instance_id) const noexcept;
        [[nodiscard]] std::optional<math::Bounds3> bounds(std::span<const InstanceId> instances) const noexcept;
    };

    export struct Scene {
        std::string name{};
        SceneResources resources{};
        CameraId active_camera{};
        FilmId active_film{};
        SamplerId active_sampler{};
        TransportSettings transport{};
        std::optional<DynamicSetup> dynamic_setup{};

        [[nodiscard]] SceneView view() const noexcept;
        [[nodiscard]] const Camera& camera() const noexcept;
        [[nodiscard]] const Film& film() const noexcept;
        [[nodiscard]] const Sampler& sampler() const noexcept;
        [[nodiscard]] SceneRevision revision() const noexcept;
        void acknowledge_changes() noexcept;
        void mark_all_changed() noexcept;

        void mark_changed(SceneChange changes) noexcept;

        SceneRevision current_revision{};
    };
} // namespace spectra::scene
