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

    export enum class SceneObjectKind : std::uint32_t {
        volume = 0,
    };

    export struct SceneObjectSelection {
        SceneObjectKind kind{SceneObjectKind::volume};
        std::string name{};
    };

    export class Scene {
    public:
        std::vector<Volume> volumes{};
        SceneObjectSelection selected_object{};

        void validate() const;
        void initialize_selection();
        void select_volume(const Volume& volume);
        void initialize_volume_render_settings(Volume& volume);
        void select_first_volume_grid(Volume& volume);
        [[nodiscard]] Volume& selected_volume();
        [[nodiscard]] const Volume& selected_volume() const;
        [[nodiscard]] const CenteredScalarGrid& selected_centered_scalar_grid(const Volume& volume) const;
        [[nodiscard]] const StaggeredVectorGrid& selected_staggered_vector_grid(const Volume& volume) const;
    };

    void Scene::validate() const {
        if (this->volumes.empty()) throw std::runtime_error("Scene has no volumes to render");

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
    }

    void Scene::initialize_selection() {
        for (Volume& volume : this->volumes) {
            this->initialize_volume_render_settings(volume);
        }

        if (this->selected_object.name.empty()) {
            this->select_volume(this->volumes.front());
            return;
        }

        if (this->selected_object.kind == SceneObjectKind::volume) {
            static_cast<void>(this->selected_volume());
            return;
        }

        throw std::runtime_error("Unsupported selected scene object kind");
    }

    void Scene::select_volume(const Volume& volume) {
        this->selected_object.kind = SceneObjectKind::volume;
        this->selected_object.name = volume.name;
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
