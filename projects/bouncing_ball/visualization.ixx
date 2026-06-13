export module xayah.projects.bouncing_ball.visualization;

import std;
import xayah.projects.bouncing_ball;

namespace xayah::projects::bouncing_ball {
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

    export struct VisualMeshVertex {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
    };

    export struct VisualMesh {
        std::string_view name{};
        std::vector<VisualMeshVertex> vertices{};
        std::vector<std::uint32_t> indices{};
        std::string_view material_name{};
        VisualTransform transform{};
        bool dynamic{true};
    };

    export struct VisualSphere {
        std::string_view name{};
        float radius{1.0f};
        std::string_view material_name{};
        VisualTransform transform{};
        bool dynamic{true};
    };

    export class Visualization final {
    public:
        Visualization() : solver(this->config) {}

        [[nodiscard]] static std::string_view visualization_id() {
            return "project.bouncing_ball";
        }

        [[nodiscard]] static std::string_view visualization_title() {
            return "Bouncing Ball";
        }

        [[nodiscard]] double frames_per_second() const {
            return 60.0;
        }

        void reset() {
            this->solver.reset();
            this->rebuild_primitives();
        }

        void step(const float delta_seconds) {
            this->solver.step(delta_seconds);
            this->rebuild_primitives();
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

        [[nodiscard]] const std::vector<VisualMesh>& meshes() const {
            return this->visual_meshes;
        }

        [[nodiscard]] const std::vector<VisualSphere>& spheres() const {
            return this->visual_spheres;
        }

    private:
        Config config{};
        Solver solver;
        std::vector<VisualMaterial> visual_materials{
            VisualMaterial{.name = "ball", .base_color = std::array<float, 4>{0.95f, 0.28f, 0.18f, 1.0f}, .roughness = 0.42f},
            VisualMaterial{.name = "floor", .base_color = std::array<float, 4>{0.38f, 0.43f, 0.38f, 1.0f}, .roughness = 0.74f},
        };
        std::vector<VisualLight> visual_lights{
            VisualLight{
                .name      = "key",
                .kind      = "directional",
                .transform = VisualTransform{.rotation = std::array<float, 4>{-0.35f, 0.0f, 0.0f, 0.94f}},
                .color     = std::array<float, 3>{1.0f, 0.97f, 0.92f},
                .intensity = 3.0f,
            },
        };
        VisualCamera visual_camera{
            .name                 = "camera.main",
            .transform            = VisualTransform{.position = std::array<float, 3>{4.4f, 2.7f, 5.8f}},
            .target               = std::array<float, 3>{0.0f, 1.25f, 0.0f},
            .vertical_fov_degrees = 42.0f,
            .near_plane           = 0.05f,
            .far_plane            = 80.0f,
        };
        std::vector<VisualMesh> visual_meshes{};
        std::vector<VisualSphere> visual_spheres{};

        [[nodiscard]] VisualMesh make_floor_mesh() const {
            return VisualMesh{
                .name = "floor.mesh",
                .vertices = {
                    VisualMeshVertex{.position = std::array<float, 3>{-5.5f, 0.0f, -5.5f}},
                    VisualMeshVertex{.position = std::array<float, 3>{5.5f, 0.0f, -5.5f}},
                    VisualMeshVertex{.position = std::array<float, 3>{5.5f, 0.0f, 5.5f}},
                    VisualMeshVertex{.position = std::array<float, 3>{-5.5f, 0.0f, 5.5f}},
                },
                .indices       = {0u, 1u, 2u, 0u, 2u, 3u},
                .material_name = "floor",
                .dynamic       = false,
            };
        }

        [[nodiscard]] VisualSphere make_ball_sphere() const {
            return VisualSphere{
                .name          = "ball.sphere",
                .radius        = this->config.radius,
                .material_name = "ball",
                .transform     = VisualTransform{.position = this->solver.current_position()},
                .dynamic       = true,
            };
        }

        void rebuild_primitives() {
            this->visual_meshes.clear();
            this->visual_meshes.push_back(this->make_floor_mesh());
            this->visual_spheres.clear();
            this->visual_spheres.push_back(this->make_ball_sphere());
        }
    };
} // namespace xayah::projects::bouncing_ball
