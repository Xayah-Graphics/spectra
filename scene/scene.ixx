export module spectra.scene;

export import spectra.scene.schema;
import std;

namespace spectra::scene {
    export inline constexpr std::uint32_t current_scene_format_version = 15;

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
        None      = 0,
        Geometry  = 1 << 0,
        Transform = 1 << 1,
        Texture   = 1 << 2,
        Material  = 1 << 3,
        Light     = 1 << 4,
        Medium    = 1 << 5,
        Volume    = 1 << 6,
        Camera    = 1 << 7,
        Film      = 1 << 8,
        Sampler   = 1 << 9,
        Metadata  = 1 << 10,
        Transport = 1 << 11,
        All       = 0xfff,
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

        [[nodiscard]] SceneView view() const noexcept;
        [[nodiscard]] const CameraResource& camera() const noexcept;
        [[nodiscard]] const Film& film() const noexcept;
        [[nodiscard]] const Sampler& sampler() const noexcept;
        [[nodiscard]] SceneRevision revision() const noexcept;
        void acknowledge_changes() noexcept;
        void rebuild_resource_state() noexcept;

    private:
        friend struct SceneWriter;

        void publish(SceneChange changes) noexcept;

        SceneRevision current_revision{};
        std::uint64_t next_geometry_id{1};
        std::uint64_t next_particle_set_id{1};
        std::uint64_t next_volume_id{1};
        std::uint64_t next_texture_id{1};
        std::uint64_t next_material_id{1};
        std::uint64_t next_medium_id{1};
        std::uint64_t next_light_id{1};
        std::uint64_t next_prototype_id{1};
        std::uint64_t next_instance_id{1};
        std::uint64_t next_camera_id{1};
        std::uint64_t next_film_id{1};
        std::uint64_t next_sampler_id{1};
    };

    export struct SceneWriter {
        explicit SceneWriter(Scene& scene) noexcept;

        [[nodiscard]] MaterialId create_diffuse_material(Float3 reflectance);
        [[nodiscard]] LightId create_diffuse_area_light(Float3 radiance, EmissionSidedness sidedness);
        [[nodiscard]] GeometryId create_triangle_mesh(std::span<const Float3> positions, std::span<const Float3> normals, std::span<const Float3> tangents, std::span<const Float2> texture_coordinates, std::span<const std::uint32_t> indices, GeometryUpdateMode update_mode);
        [[nodiscard]] ParticleSetId create_particle_set(std::span<const Float3> positions, std::span<const float> radii, std::span<const Float3> velocities, std::span<const Float3> colors, std::span<const float> temperatures, MaterialId material, std::span<const MaterialId> particle_materials, GeometryUpdateMode update_mode);
        [[nodiscard]] PrototypeId create_prototype(Primitive primitive);
        [[nodiscard]] InstanceId create_instance(PrototypeId prototype, Transform transform);
        [[nodiscard]] CameraId define_perspective_camera(Transform transform, float vertical_fov, float near_plane, float far_plane);
        [[nodiscard]] FilmId define_rgb_film(std::array<std::uint32_t, 2> resolution, Filter filter);
        [[nodiscard]] SamplerId define_sampler(SamplerKind kind, std::uint32_t samples_per_pixel, std::uint32_t seed);

        void update_triangle_mesh(GeometryId geometry, std::span<const Float3> positions, std::span<const Float3> normals, std::span<const Float3> tangents, std::span<const Float2> texture_coordinates, std::span<const std::uint32_t> indices);
        void update_particle_set(ParticleSetId particles, std::span<const Float3> positions, std::span<const float> radii, std::span<const Float3> velocities, std::span<const Float3> colors, std::span<const float> temperatures, std::span<const MaterialId> particle_materials);
        void replace_density_grid(VolumeId volume, UInt3 resolution, std::span<const float> density, std::span<const float> temperature, std::span<const float> emission_scale);
        void update_density_grid(VolumeId volume, VolumeRegion region, std::span<const float> density, std::span<const float> temperature, std::span<const float> emission_scale);
        void replace_rgb_grid(VolumeId volume, UInt3 resolution, std::span<const Float3> sigma_a, std::span<const Float3> sigma_s, std::span<const Float3> emission);
        void update_rgb_grid(VolumeId volume, VolumeRegion region, std::span<const Float3> sigma_a, std::span<const Float3> sigma_s, std::span<const Float3> emission);
        void replace_nanovdb(VolumeId volume, Bounds3 bounds, NanoVdbVolume data);
        void update_procedural_cloud(VolumeId volume, ProceduralCloudVolume data);
        void update_transform(InstanceId instance, Transform transform);
        void update_diffuse_material(MaterialId material, Float3 reflectance);
        void update_camera(CameraResource camera);
        void update_sampler(Sampler sampler);
        void update_film(Film film);
        void update_transport(TransportSettings transport);
        void rename_geometry(GeometryId geometry, std::string name);
        void rename_instance(InstanceId instance, std::string name);
        void rename_material(MaterialId material, std::string name);
        void rename_camera(std::string name);

    private:
        Scene* scene{};
    };
} // namespace spectra::scene
