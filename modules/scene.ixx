export module scene;
import std;

namespace xayah {
    export struct Transform {
        std::array<float, 3> translation{0.0f, 0.0f, 0.0f};
        std::array<float, 3> rotation_degrees{0.0f, 0.0f, 0.0f};
        std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    };

    export struct CenteredScalarGrid {
        std::string name{};
        std::array<std::uint32_t, 3> resolution{0, 0, 0};
        std::vector<float> values{};
    };

    export struct StaggeredVectorGrid {
        std::string name{};
        std::array<std::uint32_t, 3> resolution{0, 0, 0};
        std::vector<float> x_values{};
        std::vector<float> y_values{};
        std::vector<float> z_values{};
    };

    export enum class VolumeGridKind : std::uint32_t {
        centered_scalar  = 0,
        staggered_vector = 1,
    };

    export enum class VolumeDisplayMode : std::uint32_t {
        direct = 0,
        slice  = 1,
    };

    export enum class VolumeSliceAxis : std::uint32_t {
        x = 0,
        y = 1,
        z = 2,
    };

    export enum class VolumeColorMap : std::uint32_t {
        grayscale = 0,
        viridis   = 1,
        turbo     = 2,
        heat      = 3,
    };

    export struct VolumeRenderSettings {
        std::string grid_name{};
        VolumeGridKind grid_kind{VolumeGridKind::centered_scalar};
        VolumeDisplayMode display_mode{VolumeDisplayMode::direct};
        bool show_bounding_box{true};
        VolumeSliceAxis slice_axis{VolumeSliceAxis::y};
        VolumeColorMap color_map{VolumeColorMap::viridis};
        float slice_position{0.5f};
        float value_min{0.0f};
        float value_max{1.0f};
        float opacity{0.65f};
        float raymarch_step{0.025f};
    };

    export struct Volume {
        std::uint64_t id{0};
        std::string name{};
        bool visible{true};
        Transform transform{};
        std::array<float, 3> size{2.0f, 2.0f, 2.0f};
        std::vector<CenteredScalarGrid> centered_scalar_grids{};
        std::vector<StaggeredVectorGrid> staggered_vector_grids{};
        VolumeRenderSettings render_settings{};
    };

    export struct MeshVertex {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
        std::array<float, 3> color{0.8f, 0.8f, 0.8f};
    };

    export enum class MeshDisplayMode : std::uint32_t {
        surface   = 0,
        wireframe = 1,
    };

    export struct MeshRenderSettings {
        MeshDisplayMode display_mode{MeshDisplayMode::surface};
        bool show_bounding_box{false};
    };

    export struct Mesh {
        std::uint64_t id{0};
        std::string name{};
        bool visible{true};
        Transform transform{};
        std::vector<MeshVertex> vertices{};
        std::vector<std::uint32_t> indices{};
        MeshRenderSettings render_settings{};
    };

    export struct Particle {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        float radius{0.03f};
        std::array<float, 3> color{0.35f, 0.70f, 1.0f};
    };

    export struct ParticleRenderSettings {
        float radius_scale{1.0f};
        bool show_bounding_box{false};
    };

    export struct Particles {
        std::uint64_t id{0};
        std::string name{};
        bool visible{true};
        Transform transform{};
        std::vector<Particle> particles{};
        ParticleRenderSettings render_settings{};
    };

    export enum class ScenePlaybackMode : std::uint32_t {
        live  = 0,
        baked = 1,
    };

    export struct BakedVolumeFrame {
        std::uint64_t volume_id{0};
        std::vector<CenteredScalarGrid> centered_scalar_grids{};
        std::vector<StaggeredVectorGrid> staggered_vector_grids{};
    };

    export struct BakedMeshFrame {
        std::uint64_t mesh_id{0};
        std::vector<MeshVertex> vertices{};
    };

    export struct BakedParticlesFrame {
        std::uint64_t particles_id{0};
        std::vector<Particle> particles{};
    };

    export struct BakedSceneFrame {
        int frame_index{0};
        std::vector<BakedVolumeFrame> volumes{};
        std::vector<BakedMeshFrame> meshes{};
        std::vector<BakedParticlesFrame> particles{};
    };

    export struct SceneBake {
        ScenePlaybackMode mode{ScenePlaybackMode::live};
        std::vector<BakedSceneFrame> frames{};
    };

    export enum class SceneObjectKind : std::uint32_t {
        volume    = 0,
        mesh      = 1,
        particles = 2,
    };

    export struct SceneSelection {
        std::uint64_t object_id{0};
    };

    export struct SceneObjectRef {
        SceneObjectKind kind{SceneObjectKind::volume};
        std::size_t index{0};
    };

    export class Scene {
    public:
        std::vector<Volume> volumes{};
        std::vector<Mesh> meshes{};
        std::vector<Particles> particles{};
        SceneSelection selection{};
        SceneBake bake{};

        void validate() const;
        void validate_bake() const;
        void initialize_selection();
        void select_object(std::uint64_t object_id);
        void apply_playback_frame(int frame_index);
        void initialize_volume_render_settings(Volume& volume);
        void select_first_volume_grid(Volume& volume);
        [[nodiscard]] int baked_frame_min() const;
        [[nodiscard]] int baked_frame_max() const;
        [[nodiscard]] SceneObjectRef object_ref(std::uint64_t object_id) const;
        [[nodiscard]] SceneObjectRef selected_object_ref() const;
        [[nodiscard]] Transform& object_transform(std::uint64_t object_id);
        [[nodiscard]] const Transform& object_transform(std::uint64_t object_id) const;
        [[nodiscard]] Transform& selected_transform();
        [[nodiscard]] const Transform& selected_transform() const;
        [[nodiscard]] bool selected_object_visible() const;
        [[nodiscard]] const CenteredScalarGrid& render_centered_scalar_grid(const Volume& volume) const;
        [[nodiscard]] const StaggeredVectorGrid& render_staggered_vector_grid(const Volume& volume) const;
    };

    void Scene::validate() const {
        if (this->volumes.empty() && this->meshes.empty() && this->particles.empty()) throw std::runtime_error("Scene has no objects to render");

        std::set<std::uint64_t> object_ids{};
        std::set<std::string> volume_names{};
        for (const Volume& volume : this->volumes) {
            if (volume.id == 0) throw std::runtime_error(std::string{"Volume id must not be zero: "} + volume.name);
            if (!object_ids.insert(volume.id).second) throw std::runtime_error(std::string{"Duplicate scene object id: "} + std::to_string(volume.id));
            if (volume.name.empty()) throw std::runtime_error("Volume name must not be empty");
            if (!volume_names.insert(volume.name).second) throw std::runtime_error(std::string{"Duplicate volume name: "} + volume.name);
            if (volume.size[0] <= 0.0f || volume.size[1] <= 0.0f || volume.size[2] <= 0.0f) throw std::runtime_error(std::string{"Volume size must be positive: "} + volume.name);
            if (volume.transform.scale[0] <= 0.0f || volume.transform.scale[1] <= 0.0f || volume.transform.scale[2] <= 0.0f) throw std::runtime_error(std::string{"Volume transform scale must be positive: "} + volume.name);
            if (volume.centered_scalar_grids.empty() && volume.staggered_vector_grids.empty()) throw std::runtime_error(std::string{"Volume has no grids: "} + volume.name);

            std::set<std::string> grid_names{};
            for (const CenteredScalarGrid& grid : volume.centered_scalar_grids) {
                if (grid.name.empty()) throw std::runtime_error(std::string{"Centered scalar grid name must not be empty in volume: "} + volume.name);
                if (!grid_names.insert(grid.name).second) throw std::runtime_error(std::string{"Duplicate grid name in volume "} + volume.name + ": " + grid.name);
                if (grid.resolution[0] < 2 || grid.resolution[1] < 2 || grid.resolution[2] < 2) throw std::runtime_error(std::string{"Centered scalar grid resolution must be at least 2 on every axis: "} + grid.name);
                const std::size_t value_count = static_cast<std::size_t>(grid.resolution[0]) * static_cast<std::size_t>(grid.resolution[1]) * static_cast<std::size_t>(grid.resolution[2]);
                if (grid.values.size() != value_count) throw std::runtime_error(std::string{"Centered scalar grid value count does not match grid resolution: "} + grid.name);
            }

            for (const StaggeredVectorGrid& grid : volume.staggered_vector_grids) {
                if (grid.name.empty()) throw std::runtime_error(std::string{"Staggered vector grid name must not be empty in volume: "} + volume.name);
                if (!grid_names.insert(grid.name).second) throw std::runtime_error(std::string{"Duplicate grid name in volume "} + volume.name + ": " + grid.name);
                if (grid.resolution[0] < 2 || grid.resolution[1] < 2 || grid.resolution[2] < 2) throw std::runtime_error(std::string{"Staggered vector grid resolution must be at least 2 on every axis: "} + grid.name);
                const std::size_t x_count = static_cast<std::size_t>(grid.resolution[0] + 1) * static_cast<std::size_t>(grid.resolution[1]) * static_cast<std::size_t>(grid.resolution[2]);
                const std::size_t y_count = static_cast<std::size_t>(grid.resolution[0]) * static_cast<std::size_t>(grid.resolution[1] + 1) * static_cast<std::size_t>(grid.resolution[2]);
                const std::size_t z_count = static_cast<std::size_t>(grid.resolution[0]) * static_cast<std::size_t>(grid.resolution[1]) * static_cast<std::size_t>(grid.resolution[2] + 1);
                if (grid.x_values.size() != x_count) throw std::runtime_error(std::string{"Staggered vector grid x-face value count does not match grid resolution: "} + grid.name);
                if (grid.y_values.size() != y_count) throw std::runtime_error(std::string{"Staggered vector grid y-face value count does not match grid resolution: "} + grid.name);
                if (grid.z_values.size() != z_count) throw std::runtime_error(std::string{"Staggered vector grid z-face value count does not match grid resolution: "} + grid.name);
            }
        }

        std::set<std::string> mesh_names{};
        for (const Mesh& mesh : this->meshes) {
            if (mesh.id == 0) throw std::runtime_error(std::string{"Mesh id must not be zero: "} + mesh.name);
            if (!object_ids.insert(mesh.id).second) throw std::runtime_error(std::string{"Duplicate scene object id: "} + std::to_string(mesh.id));
            if (mesh.name.empty()) throw std::runtime_error("Mesh name must not be empty");
            if (!mesh_names.insert(mesh.name).second) throw std::runtime_error(std::string{"Duplicate mesh name: "} + mesh.name);
            if (mesh.vertices.empty()) throw std::runtime_error(std::string{"Mesh has no vertices: "} + mesh.name);
            if (mesh.indices.empty()) throw std::runtime_error(std::string{"Mesh has no indices: "} + mesh.name);
            if (mesh.indices.size() % 3 != 0) throw std::runtime_error(std::string{"Mesh index count must be divisible by 3: "} + mesh.name);
            if (mesh.indices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error(std::string{"Mesh has too many indices for Vulkan draw: "} + mesh.name);
            if (mesh.transform.scale[0] <= 0.0f || mesh.transform.scale[1] <= 0.0f || mesh.transform.scale[2] <= 0.0f) throw std::runtime_error(std::string{"Mesh transform scale must be positive: "} + mesh.name);

            for (const MeshVertex& vertex : mesh.vertices) {
                const float normal_length_squared = vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] + vertex.normal[2] * vertex.normal[2];
                if (normal_length_squared <= 0.000001f) throw std::runtime_error(std::string{"Mesh vertex normal must not be zero: "} + mesh.name);
            }

            for (const std::uint32_t index : mesh.indices) {
                if (index >= mesh.vertices.size()) throw std::runtime_error(std::string{"Mesh index is outside vertex range: "} + mesh.name);
            }
        }

        std::set<std::string> particles_names{};
        for (const Particles& particles : this->particles) {
            if (particles.id == 0) throw std::runtime_error(std::string{"Particles id must not be zero: "} + particles.name);
            if (!object_ids.insert(particles.id).second) throw std::runtime_error(std::string{"Duplicate scene object id: "} + std::to_string(particles.id));
            if (particles.name.empty()) throw std::runtime_error("Particles name must not be empty");
            if (!particles_names.insert(particles.name).second) throw std::runtime_error(std::string{"Duplicate particles name: "} + particles.name);
            if (particles.particles.empty()) throw std::runtime_error(std::string{"Particles object has no particles: "} + particles.name);
            if (particles.render_settings.radius_scale <= 0.0f) throw std::runtime_error(std::string{"Particles radius scale must be positive: "} + particles.name);
            if (particles.transform.scale[0] <= 0.0f || particles.transform.scale[1] <= 0.0f || particles.transform.scale[2] <= 0.0f) throw std::runtime_error(std::string{"Particles transform scale must be positive: "} + particles.name);

            for (const Particle& particle : particles.particles) {
                if (particle.radius <= 0.0f) throw std::runtime_error(std::string{"Particle radius must be positive: "} + particles.name);
            }
        }

        if (this->selection.object_id != 0) static_cast<void>(this->object_ref(this->selection.object_id));
    }

    void Scene::validate_bake() const {
        if (this->bake.mode == ScenePlaybackMode::live) return;
        if (this->bake.frames.empty()) throw std::runtime_error("Baked playback has no frames");

        std::set<int> frame_indices{};
        int frame_min = this->bake.frames.front().frame_index;
        int frame_max = this->bake.frames.front().frame_index;
        for (const BakedSceneFrame& frame : this->bake.frames) {
            if (!frame_indices.insert(frame.frame_index).second) throw std::runtime_error(std::string{"Duplicate baked frame index: "} + std::to_string(frame.frame_index));
            if (frame.frame_index < frame_min) frame_min = frame.frame_index;
            if (frame.frame_index > frame_max) frame_max = frame.frame_index;
            if (frame.volumes.size() != this->volumes.size()) throw std::runtime_error(std::string{"Baked frame volume count does not match scene volume count: "} + std::to_string(frame.frame_index));
            if (frame.meshes.size() != this->meshes.size()) throw std::runtime_error(std::string{"Baked frame mesh count does not match scene mesh count: "} + std::to_string(frame.frame_index));
            if (frame.particles.size() != this->particles.size()) throw std::runtime_error(std::string{"Baked frame particles count does not match scene particles count: "} + std::to_string(frame.frame_index));

            std::set<std::uint64_t> baked_volume_ids{};
            for (const BakedVolumeFrame& baked_volume : frame.volumes) {
                if (baked_volume.volume_id == 0) throw std::runtime_error("Baked volume id must not be zero");
                if (!baked_volume_ids.insert(baked_volume.volume_id).second) throw std::runtime_error(std::string{"Duplicate baked volume id in frame: "} + std::to_string(baked_volume.volume_id));
            }

            for (const Volume& volume : this->volumes) {
                const BakedVolumeFrame* baked_volume = nullptr;
                for (const BakedVolumeFrame& candidate : frame.volumes) {
                    if (candidate.volume_id == volume.id) {
                        baked_volume = &candidate;
                        break;
                    }
                }
                if (baked_volume == nullptr) throw std::runtime_error(std::string{"Baked frame is missing volume: "} + volume.name);
                if (baked_volume->centered_scalar_grids.size() != volume.centered_scalar_grids.size()) throw std::runtime_error(std::string{"Baked centered scalar grid count does not match volume: "} + volume.name);
                if (baked_volume->staggered_vector_grids.size() != volume.staggered_vector_grids.size()) throw std::runtime_error(std::string{"Baked staggered vector grid count does not match volume: "} + volume.name);

                std::set<std::string> baked_grid_names{};
                for (const CenteredScalarGrid& baked_grid : baked_volume->centered_scalar_grids) {
                    if (baked_grid.name.empty()) throw std::runtime_error(std::string{"Baked centered scalar grid name must not be empty in volume: "} + volume.name);
                    if (!baked_grid_names.insert(baked_grid.name).second) throw std::runtime_error(std::string{"Duplicate baked grid in volume "} + volume.name + ": " + baked_grid.name);
                }
                for (const StaggeredVectorGrid& baked_grid : baked_volume->staggered_vector_grids) {
                    if (baked_grid.name.empty()) throw std::runtime_error(std::string{"Baked staggered vector grid name must not be empty in volume: "} + volume.name);
                    if (!baked_grid_names.insert(baked_grid.name).second) throw std::runtime_error(std::string{"Duplicate baked grid in volume "} + volume.name + ": " + baked_grid.name);
                }

                for (const CenteredScalarGrid& grid : volume.centered_scalar_grids) {
                    const CenteredScalarGrid* baked_grid = nullptr;
                    for (const CenteredScalarGrid& candidate : baked_volume->centered_scalar_grids) {
                        if (candidate.name == grid.name) {
                            baked_grid = &candidate;
                            break;
                        }
                    }
                    if (baked_grid == nullptr) throw std::runtime_error(std::string{"Baked frame is missing centered scalar grid: "} + grid.name);
                    if (baked_grid->resolution != grid.resolution) throw std::runtime_error(std::string{"Baked centered scalar grid resolution does not match live grid: "} + grid.name);
                    if (baked_grid->values.size() != grid.values.size()) throw std::runtime_error(std::string{"Baked centered scalar grid value count does not match live grid: "} + grid.name);
                }

                for (const StaggeredVectorGrid& grid : volume.staggered_vector_grids) {
                    const StaggeredVectorGrid* baked_grid = nullptr;
                    for (const StaggeredVectorGrid& candidate : baked_volume->staggered_vector_grids) {
                        if (candidate.name == grid.name) {
                            baked_grid = &candidate;
                            break;
                        }
                    }
                    if (baked_grid == nullptr) throw std::runtime_error(std::string{"Baked frame is missing staggered vector grid: "} + grid.name);
                    if (baked_grid->resolution != grid.resolution) throw std::runtime_error(std::string{"Baked staggered vector grid resolution does not match live grid: "} + grid.name);
                    if (baked_grid->x_values.size() != grid.x_values.size()) throw std::runtime_error(std::string{"Baked staggered vector grid x-face value count does not match live grid: "} + grid.name);
                    if (baked_grid->y_values.size() != grid.y_values.size()) throw std::runtime_error(std::string{"Baked staggered vector grid y-face value count does not match live grid: "} + grid.name);
                    if (baked_grid->z_values.size() != grid.z_values.size()) throw std::runtime_error(std::string{"Baked staggered vector grid z-face value count does not match live grid: "} + grid.name);
                }
            }

            std::set<std::uint64_t> baked_mesh_ids{};
            for (const BakedMeshFrame& baked_mesh : frame.meshes) {
                if (baked_mesh.mesh_id == 0) throw std::runtime_error("Baked mesh id must not be zero");
                if (!baked_mesh_ids.insert(baked_mesh.mesh_id).second) throw std::runtime_error(std::string{"Duplicate baked mesh id in frame: "} + std::to_string(baked_mesh.mesh_id));
            }

            for (const Mesh& mesh : this->meshes) {
                const BakedMeshFrame* baked_mesh = nullptr;
                for (const BakedMeshFrame& candidate : frame.meshes) {
                    if (candidate.mesh_id == mesh.id) {
                        baked_mesh = &candidate;
                        break;
                    }
                }
                if (baked_mesh == nullptr) throw std::runtime_error(std::string{"Baked frame is missing mesh: "} + mesh.name);
                if (baked_mesh->vertices.size() != mesh.vertices.size()) throw std::runtime_error(std::string{"Baked mesh vertex count does not match live mesh: "} + mesh.name);

                for (const MeshVertex& vertex : baked_mesh->vertices) {
                    const float normal_length_squared = vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] + vertex.normal[2] * vertex.normal[2];
                    if (normal_length_squared <= 0.000001f) throw std::runtime_error(std::string{"Baked mesh vertex normal must not be zero: "} + mesh.name);
                }
            }

            std::set<std::uint64_t> baked_particles_ids{};
            for (const BakedParticlesFrame& baked_particles : frame.particles) {
                if (baked_particles.particles_id == 0) throw std::runtime_error("Baked particles id must not be zero");
                if (!baked_particles_ids.insert(baked_particles.particles_id).second) throw std::runtime_error(std::string{"Duplicate baked particles id in frame: "} + std::to_string(baked_particles.particles_id));
            }

            for (const Particles& particles : this->particles) {
                const BakedParticlesFrame* baked_particles = nullptr;
                for (const BakedParticlesFrame& candidate : frame.particles) {
                    if (candidate.particles_id == particles.id) {
                        baked_particles = &candidate;
                        break;
                    }
                }
                if (baked_particles == nullptr) throw std::runtime_error(std::string{"Baked frame is missing particles: "} + particles.name);

                for (const Particle& particle : baked_particles->particles) {
                    if (particle.radius <= 0.0f) throw std::runtime_error(std::string{"Baked particle radius must be positive: "} + particles.name);
                }
            }
        }

        for (int frame_index = frame_min; frame_index <= frame_max; ++frame_index) {
            if (!frame_indices.contains(frame_index)) throw std::runtime_error(std::string{"Baked playback is missing frame: "} + std::to_string(frame_index));
        }
    }

    void Scene::initialize_selection() {
        for (Volume& volume : this->volumes) {
            this->initialize_volume_render_settings(volume);
        }

        if (this->selection.object_id == 0) return;

        static_cast<void>(this->selected_object_ref());
    }

    void Scene::select_object(const std::uint64_t object_id) {
        static_cast<void>(this->object_ref(object_id));
        this->selection.object_id = object_id;
    }

    void Scene::apply_playback_frame(const int frame_index) {
        if (this->bake.mode == ScenePlaybackMode::live) return;

        const BakedSceneFrame* baked_frame = nullptr;
        for (const BakedSceneFrame& frame : this->bake.frames) {
            if (frame.frame_index == frame_index) {
                baked_frame = &frame;
                break;
            }
        }
        if (baked_frame == nullptr) throw std::runtime_error(std::string{"Baked frame does not exist: "} + std::to_string(frame_index));

        for (const BakedVolumeFrame& baked_volume : baked_frame->volumes) {
            Volume* volume = nullptr;
            for (Volume& candidate : this->volumes) {
                if (candidate.id == baked_volume.volume_id) {
                    volume = &candidate;
                    break;
                }
            }
            if (volume == nullptr) throw std::runtime_error(std::string{"Baked frame references missing volume id: "} + std::to_string(baked_volume.volume_id));

            for (CenteredScalarGrid& grid : volume->centered_scalar_grids) {
                const CenteredScalarGrid* baked_grid = nullptr;
                for (const CenteredScalarGrid& candidate : baked_volume.centered_scalar_grids) {
                    if (candidate.name == grid.name) {
                        baked_grid = &candidate;
                        break;
                    }
                }
                if (baked_grid == nullptr) throw std::runtime_error(std::string{"Baked frame is missing centered scalar grid: "} + grid.name);
                if (baked_grid->resolution != grid.resolution) throw std::runtime_error(std::string{"Baked centered scalar grid resolution does not match live grid: "} + grid.name);
                if (baked_grid->values.size() != grid.values.size()) throw std::runtime_error(std::string{"Baked centered scalar grid value count does not match live grid: "} + grid.name);
                grid.values = baked_grid->values;
            }

            for (StaggeredVectorGrid& grid : volume->staggered_vector_grids) {
                const StaggeredVectorGrid* baked_grid = nullptr;
                for (const StaggeredVectorGrid& candidate : baked_volume.staggered_vector_grids) {
                    if (candidate.name == grid.name) {
                        baked_grid = &candidate;
                        break;
                    }
                }
                if (baked_grid == nullptr) throw std::runtime_error(std::string{"Baked frame is missing staggered vector grid: "} + grid.name);
                if (baked_grid->resolution != grid.resolution) throw std::runtime_error(std::string{"Baked staggered vector grid resolution does not match live grid: "} + grid.name);
                if (baked_grid->x_values.size() != grid.x_values.size()) throw std::runtime_error(std::string{"Baked staggered vector grid x-face value count does not match live grid: "} + grid.name);
                if (baked_grid->y_values.size() != grid.y_values.size()) throw std::runtime_error(std::string{"Baked staggered vector grid y-face value count does not match live grid: "} + grid.name);
                if (baked_grid->z_values.size() != grid.z_values.size()) throw std::runtime_error(std::string{"Baked staggered vector grid z-face value count does not match live grid: "} + grid.name);
                grid.x_values = baked_grid->x_values;
                grid.y_values = baked_grid->y_values;
                grid.z_values = baked_grid->z_values;
            }
        }

        for (Mesh& mesh : this->meshes) {
            const BakedMeshFrame* baked_mesh = nullptr;
            for (const BakedMeshFrame& candidate : baked_frame->meshes) {
                if (candidate.mesh_id == mesh.id) {
                    baked_mesh = &candidate;
                    break;
                }
            }
            if (baked_mesh == nullptr) throw std::runtime_error(std::string{"Baked frame is missing mesh: "} + mesh.name);
            if (baked_mesh->vertices.size() != mesh.vertices.size()) throw std::runtime_error(std::string{"Baked mesh vertex count does not match live mesh: "} + mesh.name);
            mesh.vertices = baked_mesh->vertices;
        }

        for (Particles& particles : this->particles) {
            const BakedParticlesFrame* baked_particles = nullptr;
            for (const BakedParticlesFrame& candidate : baked_frame->particles) {
                if (candidate.particles_id == particles.id) {
                    baked_particles = &candidate;
                    break;
                }
            }
            if (baked_particles == nullptr) throw std::runtime_error(std::string{"Baked frame is missing particles: "} + particles.name);
            particles.particles = baked_particles->particles;
        }
    }

    void Scene::initialize_volume_render_settings(Volume& volume) {
        if (volume.render_settings.grid_name.empty())
            this->select_first_volume_grid(volume);
        else if (volume.render_settings.grid_kind == VolumeGridKind::centered_scalar)
            static_cast<void>(this->render_centered_scalar_grid(volume));
        else
            static_cast<void>(this->render_staggered_vector_grid(volume));
    }

    void Scene::select_first_volume_grid(Volume& volume) {
        if (!volume.centered_scalar_grids.empty()) {
            volume.render_settings.grid_kind = VolumeGridKind::centered_scalar;
            volume.render_settings.grid_name = volume.centered_scalar_grids.front().name;
            return;
        }
        if (!volume.staggered_vector_grids.empty()) {
            volume.render_settings.grid_kind = VolumeGridKind::staggered_vector;
            volume.render_settings.grid_name = volume.staggered_vector_grids.front().name;
            return;
        }
        throw std::runtime_error(std::string{"Volume has no selectable grids: "} + volume.name);
    }

    int Scene::baked_frame_min() const {
        if (this->bake.frames.empty()) throw std::runtime_error("Baked playback has no frames");
        int frame_min = this->bake.frames.front().frame_index;
        for (const BakedSceneFrame& frame : this->bake.frames) {
            if (frame.frame_index < frame_min) frame_min = frame.frame_index;
        }
        return frame_min;
    }

    int Scene::baked_frame_max() const {
        if (this->bake.frames.empty()) throw std::runtime_error("Baked playback has no frames");
        int frame_max = this->bake.frames.front().frame_index;
        for (const BakedSceneFrame& frame : this->bake.frames) {
            if (frame.frame_index > frame_max) frame_max = frame.frame_index;
        }
        return frame_max;
    }

    SceneObjectRef Scene::object_ref(const std::uint64_t object_id) const {
        if (object_id == 0) throw std::runtime_error("Scene object id must not be zero");
        for (std::size_t index = 0; index < this->volumes.size(); ++index) {
            if (this->volumes[index].id == object_id) return SceneObjectRef{SceneObjectKind::volume, index};
        }
        for (std::size_t index = 0; index < this->meshes.size(); ++index) {
            if (this->meshes[index].id == object_id) return SceneObjectRef{SceneObjectKind::mesh, index};
        }
        for (std::size_t index = 0; index < this->particles.size(); ++index) {
            if (this->particles[index].id == object_id) return SceneObjectRef{SceneObjectKind::particles, index};
        }
        throw std::runtime_error(std::string{"Scene object id does not exist: "} + std::to_string(object_id));
    }

    SceneObjectRef Scene::selected_object_ref() const {
        return this->object_ref(this->selection.object_id);
    }

    Transform& Scene::object_transform(const std::uint64_t object_id) {
        const SceneObjectRef reference = this->object_ref(object_id);
        if (reference.kind == SceneObjectKind::volume) return this->volumes.at(reference.index).transform;
        if (reference.kind == SceneObjectKind::mesh) return this->meshes.at(reference.index).transform;
        if (reference.kind == SceneObjectKind::particles) return this->particles.at(reference.index).transform;
        throw std::runtime_error("Cannot access transform for unsupported scene object kind");
    }

    const Transform& Scene::object_transform(const std::uint64_t object_id) const {
        const SceneObjectRef reference = this->object_ref(object_id);
        if (reference.kind == SceneObjectKind::volume) return this->volumes.at(reference.index).transform;
        if (reference.kind == SceneObjectKind::mesh) return this->meshes.at(reference.index).transform;
        if (reference.kind == SceneObjectKind::particles) return this->particles.at(reference.index).transform;
        throw std::runtime_error("Cannot access transform for unsupported scene object kind");
    }

    Transform& Scene::selected_transform() {
        return this->object_transform(this->selection.object_id);
    }

    const Transform& Scene::selected_transform() const {
        return this->object_transform(this->selection.object_id);
    }

    bool Scene::selected_object_visible() const {
        const SceneObjectRef reference = this->selected_object_ref();
        if (reference.kind == SceneObjectKind::volume) return this->volumes.at(reference.index).visible;
        if (reference.kind == SceneObjectKind::mesh) return this->meshes.at(reference.index).visible;
        if (reference.kind == SceneObjectKind::particles) return this->particles.at(reference.index).visible;
        throw std::runtime_error("Cannot query visibility for unsupported scene object kind");
    }

    const CenteredScalarGrid& Scene::render_centered_scalar_grid(const Volume& volume) const {
        for (const CenteredScalarGrid& grid : volume.centered_scalar_grids) {
            if (grid.name == volume.render_settings.grid_name) return grid;
        }
        throw std::runtime_error(std::string{"Volume render centered scalar grid does not exist: "} + volume.render_settings.grid_name);
    }

    const StaggeredVectorGrid& Scene::render_staggered_vector_grid(const Volume& volume) const {
        for (const StaggeredVectorGrid& grid : volume.staggered_vector_grids) {
            if (grid.name == volume.render_settings.grid_name) return grid;
        }
        throw std::runtime_error(std::string{"Volume render staggered vector grid does not exist: "} + volume.render_settings.grid_name);
    }
} // namespace xayah
