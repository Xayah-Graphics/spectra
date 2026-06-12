export module xayah.projects.sparkles.visualization;

import std;
import xayah.projects.sparkles;

namespace xayah::projects::sparkles {
    export struct VisualTransform {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
        std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    };

    export struct VisualMaterial {
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

    export struct VisualLight {
        std::string_view name{};
        std::string_view kind{};
        VisualTransform transform{};
        std::array<float, 3> color{};
        float intensity{};
        float cone_angle_degrees{};
    };

    export struct VisualCamera {
        std::string_view name{};
        VisualTransform transform{};
        std::array<float, 3> target{0.0f, 0.0f, 0.0f};
        std::array<float, 3> up{0.0f, 1.0f, 0.0f};
        float vertical_fov_degrees{45.0f};
        float near_plane{0.01f};
        float far_plane{200.0f};
    };

    export struct VisualPoint {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
        std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
        float radius{0.01f};
    };

    export struct VisualPointCloud {
        std::string_view name{};
        std::vector<VisualPoint> points{};
        std::string_view material_name{};
        VisualTransform transform{};
        bool dynamic{true};
    };

    export class Visualization final {
    public:
        Visualization() = default;

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

        [[nodiscard]] const std::vector<VisualMaterial>& materials() const {
            return this->visual_materials;
        }

        [[nodiscard]] const std::vector<VisualLight>& lights() const {
            return this->visual_lights;
        }

        [[nodiscard]] const VisualCamera& camera() const {
            return this->visual_camera;
        }

        [[nodiscard]] const std::vector<VisualPointCloud>& point_clouds() const {
            return this->visual_point_clouds;
        }

    private:
        Solver solver{};
        std::vector<VisualMaterial> visual_materials{
            VisualMaterial{
                .name              = "sparkle",
                .model             = "point_sprite",
                .alpha_mode        = "blend",
                .base_color        = std::array<float, 4>{1.0f, 0.78f, 0.24f, 1.0f},
                .emission_color    = std::array<float, 3>{1.0f, 0.54f, 0.12f},
                .emission_strength = 2.4f,
                .roughness         = 0.2f,
            },
        };
        std::vector<VisualLight> visual_lights{
            VisualLight{
                .name      = "environment",
                .kind      = "environment",
                .color     = std::array<float, 3>{0.22f, 0.26f, 0.34f},
                .intensity = 0.8f,
            },
        };
        VisualCamera visual_camera{
            .name                 = "camera.main",
            .transform            = VisualTransform{.position = std::array<float, 3>{4.6f, 3.1f, 6.0f}},
            .target               = std::array<float, 3>{0.0f, 1.9f, 0.0f},
            .vertical_fov_degrees = 42.0f,
            .near_plane           = 0.05f,
            .far_plane            = 90.0f,
        };
        std::vector<VisualPointCloud> visual_point_clouds{};

        void rebuild_point_clouds() {
            VisualPointCloud point_cloud{
                .name          = "sparkles.points",
                .material_name = "sparkle",
                .dynamic       = true,
            };
            const std::span<const Particle> particles = this->solver.particles();
            point_cloud.points.reserve(particles.size());
            for (const Particle& particle : particles) {
                point_cloud.points.push_back(VisualPoint{
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
