export module xayah.projects.sparkles.visualization;

import std;
import xayah.projects.sparkles;

namespace xayah::projects::sparkles {
    export struct SparklesVisualTransform {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
        std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    };

    export struct SparklesVisualMaterial {
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

    export struct SparklesVisualLight {
        std::string_view name{};
        std::string_view kind{"directional"};
        SparklesVisualTransform transform{};
        std::array<float, 3> color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float cone_angle_degrees{45.0f};
    };

    export struct SparklesVisualCamera {
        std::string_view name{};
        SparklesVisualTransform transform{};
        std::array<float, 3> target{0.0f, 0.0f, 0.0f};
        std::array<float, 3> up{0.0f, 1.0f, 0.0f};
        float vertical_fov_degrees{45.0f};
        float near_plane{0.01f};
        float far_plane{200.0f};
    };

    export struct SparklesVisualMeshVertex {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
    };

    export struct SparklesVisualMesh {
        std::string_view name{};
        std::vector<SparklesVisualMeshVertex> vertices{};
        std::vector<std::uint32_t> indices{};
        std::string_view material_name{};
        SparklesVisualTransform transform{};
        bool dynamic{true};
    };

    export struct SparklesVisualPoint {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
        std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
        float radius{0.01f};
    };

    export struct SparklesVisualPointCloud {
        std::string_view name{};
        std::vector<SparklesVisualPoint> points{};
        std::string_view material_name{};
        SparklesVisualTransform transform{};
        bool dynamic{true};
    };

    export struct SparklesVisualVolumeChannel {
        std::string_view name{};
        std::array<std::uint32_t, 3> dimensions{0u, 0u, 0u};
        std::vector<float> values{};
    };

    export struct SparklesVisualVolume {
        std::string_view name{};
        std::array<std::uint32_t, 3> dimensions{0u, 0u, 0u};
        std::array<float, 3> origin{0.0f, 0.0f, 0.0f};
        std::array<float, 3> voxel_size{1.0f, 1.0f, 1.0f};
        std::vector<SparklesVisualVolumeChannel> channels{};
        std::string_view material_name{};
        bool dynamic{true};
    };

    export class SparklesVisualization final {
    public:
        SparklesVisualization() {
            this->reset();
        }

        [[nodiscard]] static std::string_view visualization_id() {
            return "project.sparkles";
        }

        [[nodiscard]] static std::string_view visualization_title() {
            return "Sparkles";
        }

        [[nodiscard]] double frames_per_second() const {
            return 60.0;
        }

        void reset() {
            this->solver.reset();
            this->rebuild_point_clouds();
        }

        void step(const float delta_seconds) {
            this->solver.step(delta_seconds);
            this->rebuild_point_clouds();
        }

        [[nodiscard]] const std::vector<SparklesVisualMaterial>& materials() const {
            return this->visual_materials;
        }

        [[nodiscard]] const std::vector<SparklesVisualLight>& lights() const {
            return this->visual_lights;
        }

        [[nodiscard]] const SparklesVisualCamera& camera() const {
            return this->visual_camera;
        }

        [[nodiscard]] const std::vector<SparklesVisualMesh>& meshes() const {
            return this->visual_meshes;
        }

        [[nodiscard]] const std::vector<SparklesVisualPointCloud>& point_clouds() const {
            return this->visual_point_clouds;
        }

        [[nodiscard]] const std::vector<SparklesVisualVolume>& volumes() const {
            return this->visual_volumes;
        }

    private:
        SparklesSolver solver{};
        std::vector<SparklesVisualMaterial> visual_materials{
            SparklesVisualMaterial{
                .name              = "sparkle",
                .model             = "point_sprite",
                .alpha_mode        = "blend",
                .base_color        = std::array<float, 4>{1.0f, 0.78f, 0.24f, 1.0f},
                .emission_color    = std::array<float, 3>{1.0f, 0.54f, 0.12f},
                .emission_strength = 2.4f,
                .roughness         = 0.2f,
            },
        };
        std::vector<SparklesVisualLight> visual_lights{
            SparklesVisualLight{
                .name      = "environment",
                .kind      = "environment",
                .color     = std::array<float, 3>{0.22f, 0.26f, 0.34f},
                .intensity = 0.8f,
            },
        };
        SparklesVisualCamera visual_camera{
            .name                 = "camera.main",
            .transform            = SparklesVisualTransform{.position = std::array<float, 3>{4.6f, 3.1f, 6.0f}},
            .target               = std::array<float, 3>{0.0f, 1.9f, 0.0f},
            .vertical_fov_degrees = 42.0f,
            .near_plane           = 0.05f,
            .far_plane            = 90.0f,
        };
        std::vector<SparklesVisualMesh> visual_meshes{};
        std::vector<SparklesVisualPointCloud> visual_point_clouds{};
        std::vector<SparklesVisualVolume> visual_volumes{};

        void rebuild_point_clouds() {
            SparklesVisualPointCloud point_cloud{
                .name          = "sparkles.points",
                .material_name = "sparkle",
                .dynamic       = true,
            };
            const std::span<const SparklesParticle> particles = this->solver.particles();
            point_cloud.points.reserve(particles.size());
            for (const SparklesParticle& particle : particles) {
                point_cloud.points.push_back(SparklesVisualPoint{
                    .position = particle.position,
                    .color    = std::array<float, 4>{particle.color[0], particle.color[1], particle.color[2], 1.0f},
                    .radius   = particle.radius,
                });
            }
            this->visual_point_clouds.clear();
            this->visual_point_clouds.push_back(std::move(point_cloud));
        }
    };
} // namespace xayah::projects::sparkles
