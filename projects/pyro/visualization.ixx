export module xayah.projects.pyro.visualization;

import std;
import xayah.projects.pyro;

namespace xayah::projects::pyro {
    export struct PyroVisualTransform {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
        std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    };

    export struct PyroVisualMaterial {
        std::string_view name{};
        std::string_view model{"lit_surface"};
        std::string_view alpha_mode{"opaque"};
        std::array<float, 4> base_color{0.8f, 0.8f, 0.8f, 1.0f};
        std::array<float, 3> emission_color{0.0f, 0.0f, 0.0f};
        float emission_strength{0.0f};
        float roughness{0.5f};
        float metallic{0.0f};
        float alpha_cutoff{0.5f};
        float volume_density_scale{0.08f};
        float volume_temperature_scale{0.035f};
    };

    export struct PyroVisualLight {
        std::string_view name{};
        std::string_view kind{"directional"};
        PyroVisualTransform transform{};
        std::array<float, 3> color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float cone_angle_degrees{45.0f};
    };

    export struct PyroVisualCamera {
        std::string_view name{};
        PyroVisualTransform transform{};
        std::array<float, 3> target{0.0f, 0.0f, 0.0f};
        std::array<float, 3> up{0.0f, 1.0f, 0.0f};
        float vertical_fov_degrees{45.0f};
        float near_plane{0.01f};
        float far_plane{200.0f};
    };

    export struct PyroVisualMeshVertex {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
    };

    export struct PyroVisualMesh {
        std::string_view name{};
        std::vector<PyroVisualMeshVertex> vertices{};
        std::vector<std::uint32_t> indices{};
        std::string_view material_name{};
        PyroVisualTransform transform{};
        bool dynamic{true};
    };

    export struct PyroVisualPoint {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
        std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
        float radius{0.01f};
    };

    export struct PyroVisualPointCloud {
        std::string_view name{};
        std::vector<PyroVisualPoint> points{};
        std::string_view material_name{};
        PyroVisualTransform transform{};
        bool dynamic{true};
    };

    export struct PyroVisualVolumeChannel {
        std::string_view name{};
        std::array<std::uint32_t, 3> dimensions{0u, 0u, 0u};
        std::span<const float> values{};
    };

    export struct PyroVisualVolume {
        std::string_view name{};
        std::array<std::uint32_t, 3> dimensions{0u, 0u, 0u};
        std::array<float, 3> origin{0.0f, 0.0f, 0.0f};
        std::array<float, 3> voxel_size{1.0f, 1.0f, 1.0f};
        std::vector<PyroVisualVolumeChannel> channels{};
        std::string_view material_name{};
        bool dynamic{true};
    };

    export class PyroVisualization final {
    public:
        PyroVisualization() = default;

        [[nodiscard]] static std::string_view visualization_id() {
            return "project.pyro";
        }

        [[nodiscard]] static std::string_view visualization_title() {
            return "Pyro";
        }

        [[nodiscard]] double frames_per_second() const {
            return 60.0;
        }

        void reset() {
            this->solver.reset();
            this->frame_index = 0u;
            this->rebuild_volume(0);
        }

        void step(const float delta_seconds) {
            this->solver.step(delta_seconds);
            ++this->frame_index;
            this->rebuild_volume(this->to_solver_frame_index(this->frame_index));
        }

        [[nodiscard]] const std::vector<PyroVisualMaterial>& materials() const {
            return this->visual_materials;
        }

        [[nodiscard]] const std::vector<PyroVisualLight>& lights() const {
            return this->visual_lights;
        }

        [[nodiscard]] const PyroVisualCamera& camera() const {
            return this->visual_camera;
        }

        [[nodiscard]] const std::vector<PyroVisualMesh>& meshes() const {
            return this->visual_meshes;
        }

        [[nodiscard]] const std::vector<PyroVisualPointCloud>& point_clouds() const {
            return this->visual_point_clouds;
        }

        [[nodiscard]] const std::vector<PyroVisualVolume>& volumes() const {
            return this->visual_volumes;
        }

    private:
        PyroSolver solver{};
        PyroFrame current_frame{};
        std::uint64_t frame_index{};
        std::vector<PyroVisualMaterial> visual_materials{
            PyroVisualMaterial{
                .name              = "smoke",
                .model             = "volume",
                .alpha_mode        = "blend",
                .base_color        = std::array<float, 4>{0.58f, 0.62f, 0.68f, 0.72f},
                .emission_color    = std::array<float, 3>{0.95f, 0.48f, 0.18f},
                .emission_strength = 0.3f,
                .roughness         = 0.9f,
            },
        };
        std::vector<PyroVisualLight> visual_lights{
            PyroVisualLight{
                .name      = "environment",
                .kind      = "environment",
                .color     = std::array<float, 3>{0.24f, 0.26f, 0.30f},
                .intensity = 0.7f,
            },
        };
        PyroVisualCamera visual_camera{
            .name                 = "camera.main",
            .transform            = PyroVisualTransform{.position = std::array<float, 3>{2.35f, 1.65f, 2.8f}},
            .target               = std::array<float, 3>{0.6f, 0.86f, 0.6f},
            .vertical_fov_degrees = 39.0f,
            .near_plane           = 0.03f,
            .far_plane            = 80.0f,
        };
        std::vector<PyroVisualMesh> visual_meshes{};
        std::vector<PyroVisualPointCloud> visual_point_clouds{};
        std::vector<PyroVisualVolume> visual_volumes{};

        [[nodiscard]] static int to_solver_frame_index(const std::uint64_t index) {
            if (index > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Pyro visualization frame index exceeds int range");
            return static_cast<int>(index);
        }

        void rebuild_volume(const int solver_frame_index) {
            this->current_frame = this->solver.read_frame(solver_frame_index);
            PyroVisualVolume volume{
                .name          = "pyro.volume",
                .dimensions    = this->current_frame.resolution,
                .origin        = std::array<float, 3>{0.0f, 0.0f, 0.0f},
                .voxel_size    = std::array<float, 3>{this->current_frame.cell_size, this->current_frame.cell_size, this->current_frame.cell_size},
                .material_name = "smoke",
                .dynamic       = true,
            };
            volume.channels.push_back(PyroVisualVolumeChannel{
                .name       = "density",
                .dimensions = this->current_frame.resolution,
                .values     = std::span<const float>{this->current_frame.density.data(), this->current_frame.density.size()},
            });
            volume.channels.push_back(PyroVisualVolumeChannel{
                .name       = "temperature",
                .dimensions = this->current_frame.resolution,
                .values     = std::span<const float>{this->current_frame.temperature.data(), this->current_frame.temperature.size()},
            });
            this->visual_volumes.clear();
            this->visual_volumes.push_back(std::move(volume));
        }
    };
} // namespace xayah::projects::pyro
