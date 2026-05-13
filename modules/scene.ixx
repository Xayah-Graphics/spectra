export module scene;
import std;

namespace xayah {
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
        VolumeSliceAxis slice_axis{VolumeSliceAxis::y};
        VolumeColorMap color_map{VolumeColorMap::viridis};
        float slice_position{0.5f};
        float value_min{0.0f};
        float value_max{1.0f};
        float opacity{0.65f};
        float raymarch_step{0.025f};
    };

    export struct Volume {
        std::string name{};
        std::array<float, 3> origin{-1.0f, -1.0f, -1.0f};
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

    export struct Mesh {
        std::string name{};
        std::vector<MeshVertex> vertices{};
        std::vector<std::uint32_t> indices{};
    };

    export enum class ScenePlaybackMode : std::uint32_t {
        live  = 0,
        baked = 1,
    };

    export struct BakedVolumeFrame {
        std::string volume_name{};
        std::vector<CenteredScalarGrid> centered_scalar_grids{};
        std::vector<StaggeredVectorGrid> staggered_vector_grids{};
    };

    export struct BakedMeshFrame {
        std::string mesh_name{};
        std::vector<MeshVertex> vertices{};
    };

    export struct BakedSceneFrame {
        int frame_index{0};
        std::vector<BakedVolumeFrame> volumes{};
        std::vector<BakedMeshFrame> meshes{};
    };

    export struct SceneBake {
        ScenePlaybackMode mode{ScenePlaybackMode::live};
        std::vector<BakedSceneFrame> frames{};
    };

    export enum class SceneObjectKind : std::uint32_t {
        volume = 0,
        mesh   = 1,
    };

    export struct SceneObjectSelection {
        SceneObjectKind kind{SceneObjectKind::volume};
        std::string name{};
    };

    export class Scene {
    public:
        std::vector<Volume> volumes{};
        std::vector<Mesh> meshes{};
        SceneObjectSelection selected_object{};
        SceneBake bake{};

        void validate() const;
        void validate_bake() const;
        void initialize_selection();
        void select_volume(const Volume& volume);
        void select_mesh(const Mesh& mesh);
        void apply_playback_frame(int frame_index);
        void initialize_volume_render_settings(Volume& volume);
        void select_first_volume_grid(Volume& volume);
        [[nodiscard]] int baked_frame_min() const;
        [[nodiscard]] int baked_frame_max() const;
        [[nodiscard]] Volume& selected_volume();
        [[nodiscard]] const Volume& selected_volume() const;
        [[nodiscard]] Mesh& selected_mesh();
        [[nodiscard]] const Mesh& selected_mesh() const;
        [[nodiscard]] const CenteredScalarGrid& selected_centered_scalar_grid(const Volume& volume) const;
        [[nodiscard]] const StaggeredVectorGrid& selected_staggered_vector_grid(const Volume& volume) const;
    };

    void Scene::validate() const {
        if (this->volumes.empty() && this->meshes.empty()) throw std::runtime_error("Scene has no objects to render");

        std::set<std::string> volume_names{};
        for (const Volume& volume : this->volumes) {
            if (volume.name.empty()) throw std::runtime_error("Volume name must not be empty");
            if (!volume_names.insert(volume.name).second) throw std::runtime_error(std::string{"Duplicate volume name: "} + volume.name);
            if (volume.size[0] <= 0.0f || volume.size[1] <= 0.0f || volume.size[2] <= 0.0f) throw std::runtime_error(std::string{"Volume size must be positive: "} + volume.name);
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
            if (mesh.name.empty()) throw std::runtime_error("Mesh name must not be empty");
            if (!mesh_names.insert(mesh.name).second) throw std::runtime_error(std::string{"Duplicate mesh name: "} + mesh.name);
            if (mesh.vertices.empty()) throw std::runtime_error(std::string{"Mesh has no vertices: "} + mesh.name);
            if (mesh.indices.empty()) throw std::runtime_error(std::string{"Mesh has no indices: "} + mesh.name);
            if (mesh.indices.size() % 3 != 0) throw std::runtime_error(std::string{"Mesh index count must be divisible by 3: "} + mesh.name);
            if (mesh.indices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error(std::string{"Mesh has too many indices for Vulkan draw: "} + mesh.name);

            for (const MeshVertex& vertex : mesh.vertices) {
                const float normal_length_squared = vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] + vertex.normal[2] * vertex.normal[2];
                if (normal_length_squared <= 0.000001f) throw std::runtime_error(std::string{"Mesh vertex normal must not be zero: "} + mesh.name);
            }

            for (const std::uint32_t index : mesh.indices) {
                if (index >= mesh.vertices.size()) throw std::runtime_error(std::string{"Mesh index is outside vertex range: "} + mesh.name);
            }
        }
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

            std::set<std::string> baked_volume_names{};
            for (const BakedVolumeFrame& baked_volume : frame.volumes) {
                if (baked_volume.volume_name.empty()) throw std::runtime_error("Baked volume name must not be empty");
                if (!baked_volume_names.insert(baked_volume.volume_name).second) throw std::runtime_error(std::string{"Duplicate baked volume in frame: "} + baked_volume.volume_name);
            }

            for (const Volume& volume : this->volumes) {
                const BakedVolumeFrame* baked_volume = nullptr;
                for (const BakedVolumeFrame& candidate : frame.volumes) {
                    if (candidate.volume_name == volume.name) {
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

            std::set<std::string> baked_mesh_names{};
            for (const BakedMeshFrame& baked_mesh : frame.meshes) {
                if (baked_mesh.mesh_name.empty()) throw std::runtime_error("Baked mesh name must not be empty");
                if (!baked_mesh_names.insert(baked_mesh.mesh_name).second) throw std::runtime_error(std::string{"Duplicate baked mesh in frame: "} + baked_mesh.mesh_name);
            }

            for (const Mesh& mesh : this->meshes) {
                const BakedMeshFrame* baked_mesh = nullptr;
                for (const BakedMeshFrame& candidate : frame.meshes) {
                    if (candidate.mesh_name == mesh.name) {
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
        }

        for (int frame_index = frame_min; frame_index <= frame_max; ++frame_index) {
            if (!frame_indices.contains(frame_index)) throw std::runtime_error(std::string{"Baked playback is missing frame: "} + std::to_string(frame_index));
        }
    }

    void Scene::initialize_selection() {
        for (Volume& volume : this->volumes) {
            this->initialize_volume_render_settings(volume);
        }

        if (this->selected_object.name.empty()) {
            if (!this->volumes.empty()) {
                this->select_volume(this->volumes.front());
                return;
            }
            this->select_mesh(this->meshes.front());
            return;
        }

        if (this->selected_object.kind == SceneObjectKind::volume) {
            static_cast<void>(this->selected_volume());
            return;
        }
        if (this->selected_object.kind == SceneObjectKind::mesh) {
            static_cast<void>(this->selected_mesh());
            return;
        }

        throw std::runtime_error("Unsupported selected scene object kind");
    }

    void Scene::select_volume(const Volume& volume) {
        this->selected_object.kind = SceneObjectKind::volume;
        this->selected_object.name = volume.name;
    }

    void Scene::select_mesh(const Mesh& mesh) {
        this->selected_object.kind = SceneObjectKind::mesh;
        this->selected_object.name = mesh.name;
    }

    void Scene::apply_playback_frame(int frame_index) {
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
                if (candidate.name == baked_volume.volume_name) {
                    volume = &candidate;
                    break;
                }
            }
            if (volume == nullptr) throw std::runtime_error(std::string{"Baked frame references missing volume: "} + baked_volume.volume_name);

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
                if (candidate.mesh_name == mesh.name) {
                    baked_mesh = &candidate;
                    break;
                }
            }
            if (baked_mesh == nullptr) throw std::runtime_error(std::string{"Baked frame is missing mesh: "} + mesh.name);
            if (baked_mesh->vertices.size() != mesh.vertices.size()) throw std::runtime_error(std::string{"Baked mesh vertex count does not match live mesh: "} + mesh.name);
            mesh.vertices = baked_mesh->vertices;
        }
    }

    void Scene::initialize_volume_render_settings(Volume& volume) {
        if (volume.render_settings.grid_name.empty())
            this->select_first_volume_grid(volume);
        else if (volume.render_settings.grid_kind == VolumeGridKind::centered_scalar)
            static_cast<void>(this->selected_centered_scalar_grid(volume));
        else
            static_cast<void>(this->selected_staggered_vector_grid(volume));
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

    Volume& Scene::selected_volume() {
        if (this->selected_object.kind != SceneObjectKind::volume) throw std::runtime_error("Selected scene object is not a volume");
        for (Volume& volume : this->volumes) {
            if (volume.name == this->selected_object.name) return volume;
        }
        throw std::runtime_error(std::string{"Selected volume does not exist: "} + this->selected_object.name);
    }

    const Volume& Scene::selected_volume() const {
        if (this->selected_object.kind != SceneObjectKind::volume) throw std::runtime_error("Selected scene object is not a volume");
        for (const Volume& volume : this->volumes) {
            if (volume.name == this->selected_object.name) return volume;
        }
        throw std::runtime_error(std::string{"Selected volume does not exist: "} + this->selected_object.name);
    }

    Mesh& Scene::selected_mesh() {
        if (this->selected_object.kind != SceneObjectKind::mesh) throw std::runtime_error("Selected scene object is not a mesh");
        for (Mesh& mesh : this->meshes) {
            if (mesh.name == this->selected_object.name) return mesh;
        }
        throw std::runtime_error(std::string{"Selected mesh does not exist: "} + this->selected_object.name);
    }

    const Mesh& Scene::selected_mesh() const {
        if (this->selected_object.kind != SceneObjectKind::mesh) throw std::runtime_error("Selected scene object is not a mesh");
        for (const Mesh& mesh : this->meshes) {
            if (mesh.name == this->selected_object.name) return mesh;
        }
        throw std::runtime_error(std::string{"Selected mesh does not exist: "} + this->selected_object.name);
    }

    const CenteredScalarGrid& Scene::selected_centered_scalar_grid(const Volume& volume) const {
        for (const CenteredScalarGrid& grid : volume.centered_scalar_grids) {
            if (grid.name == volume.render_settings.grid_name) return grid;
        }
        throw std::runtime_error(std::string{"Selected centered scalar grid does not exist: "} + volume.render_settings.grid_name);
    }

    const StaggeredVectorGrid& Scene::selected_staggered_vector_grid(const Volume& volume) const {
        for (const StaggeredVectorGrid& grid : volume.staggered_vector_grids) {
            if (grid.name == volume.render_settings.grid_name) return grid;
        }
        throw std::runtime_error(std::string{"Selected staggered vector grid does not exist: "} + volume.render_settings.grid_name);
    }
} // namespace xayah
