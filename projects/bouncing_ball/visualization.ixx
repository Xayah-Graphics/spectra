export module xayah.projects.bouncing_ball.visualization;

import std;
import xayah.projects.bouncing_ball;

namespace xayah::projects::bouncing_ball {
    export struct BouncingBallVisualTransform {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
        std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    };

    export struct BouncingBallVisualMaterial {
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

    export struct BouncingBallVisualLight {
        std::string_view name{};
        std::string_view kind{"directional"};
        BouncingBallVisualTransform transform{};
        std::array<float, 3> color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float cone_angle_degrees{45.0f};
    };

    export struct BouncingBallVisualCamera {
        std::string_view name{};
        BouncingBallVisualTransform transform{};
        std::array<float, 3> target{0.0f, 0.0f, 0.0f};
        std::array<float, 3> up{0.0f, 1.0f, 0.0f};
        float vertical_fov_degrees{45.0f};
        float near_plane{0.01f};
        float far_plane{200.0f};
    };

    export struct BouncingBallVisualMeshVertex {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
    };

    export struct BouncingBallVisualMesh {
        std::string_view name{};
        std::vector<BouncingBallVisualMeshVertex> vertices{};
        std::vector<std::uint32_t> indices{};
        std::string_view material_name{};
        BouncingBallVisualTransform transform{};
        bool dynamic{true};
    };

    export struct BouncingBallVisualPoint {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
        std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
        float radius{0.01f};
    };

    export struct BouncingBallVisualPointCloud {
        std::string_view name{};
        std::vector<BouncingBallVisualPoint> points{};
        std::string_view material_name{};
        BouncingBallVisualTransform transform{};
        bool dynamic{true};
    };

    export struct BouncingBallVisualVolumeChannel {
        std::string_view name{};
        std::array<std::uint32_t, 3> dimensions{0u, 0u, 0u};
        std::vector<float> values{};
    };

    export struct BouncingBallVisualVolume {
        std::string_view name{};
        std::array<std::uint32_t, 3> dimensions{0u, 0u, 0u};
        std::array<float, 3> origin{0.0f, 0.0f, 0.0f};
        std::array<float, 3> voxel_size{1.0f, 1.0f, 1.0f};
        std::vector<BouncingBallVisualVolumeChannel> channels{};
        std::string_view material_name{};
        bool dynamic{true};
    };

    export class BouncingBallVisualization final {
    public:
        BouncingBallVisualization() {
            this->reset();
        }

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
            this->rebuild_meshes();
        }

        void step(const float delta_seconds) {
            this->solver.step(delta_seconds);
            this->rebuild_meshes();
        }

        [[nodiscard]] const std::vector<BouncingBallVisualMaterial>& materials() const {
            return this->visual_materials;
        }

        [[nodiscard]] const std::vector<BouncingBallVisualLight>& lights() const {
            return this->visual_lights;
        }

        [[nodiscard]] const BouncingBallVisualCamera& camera() const {
            return this->visual_camera;
        }

        [[nodiscard]] const std::vector<BouncingBallVisualMesh>& meshes() const {
            return this->visual_meshes;
        }

        [[nodiscard]] const std::vector<BouncingBallVisualPointCloud>& point_clouds() const {
            return this->visual_point_clouds;
        }

        [[nodiscard]] const std::vector<BouncingBallVisualVolume>& volumes() const {
            return this->visual_volumes;
        }

    private:
        BouncingBallSolver solver{};
        std::vector<BouncingBallVisualMaterial> visual_materials{
            BouncingBallVisualMaterial{.name = "ball", .base_color = std::array<float, 4>{0.95f, 0.28f, 0.18f, 1.0f}, .roughness = 0.42f},
            BouncingBallVisualMaterial{.name = "floor", .base_color = std::array<float, 4>{0.38f, 0.43f, 0.38f, 1.0f}, .roughness = 0.74f},
        };
        std::vector<BouncingBallVisualLight> visual_lights{
            BouncingBallVisualLight{
                .name      = "key",
                .kind      = "directional",
                .transform = BouncingBallVisualTransform{.rotation = std::array<float, 4>{0.35f, 0.0f, 0.0f, 0.94f}},
                .color     = std::array<float, 3>{1.0f, 0.97f, 0.92f},
                .intensity = 3.0f,
            },
        };
        BouncingBallVisualCamera visual_camera{
            .name                 = "camera.main",
            .transform            = BouncingBallVisualTransform{.position = std::array<float, 3>{4.4f, 2.7f, 5.8f}},
            .target               = std::array<float, 3>{0.0f, 1.25f, 0.0f},
            .vertical_fov_degrees = 42.0f,
            .near_plane           = 0.05f,
            .far_plane            = 80.0f,
        };
        std::vector<BouncingBallVisualMesh> visual_meshes{};
        std::vector<BouncingBallVisualPointCloud> visual_point_clouds{};
        std::vector<BouncingBallVisualVolume> visual_volumes{};

        [[nodiscard]] BouncingBallVisualMesh make_floor_mesh() const {
            return BouncingBallVisualMesh{
                .name = "floor.mesh",
                .vertices = {
                    BouncingBallVisualMeshVertex{.position = std::array<float, 3>{-5.5f, 0.0f, -5.5f}},
                    BouncingBallVisualMeshVertex{.position = std::array<float, 3>{5.5f, 0.0f, -5.5f}},
                    BouncingBallVisualMeshVertex{.position = std::array<float, 3>{5.5f, 0.0f, 5.5f}},
                    BouncingBallVisualMeshVertex{.position = std::array<float, 3>{-5.5f, 0.0f, 5.5f}},
                },
                .indices       = {0u, 1u, 2u, 0u, 2u, 3u},
                .material_name = "floor",
                .dynamic       = false,
            };
        }

        [[nodiscard]] BouncingBallVisualMesh make_ball_mesh() const {
            BouncingBallVisualMesh mesh{
                .name          = "ball.mesh",
                .material_name = "ball",
                .transform     = BouncingBallVisualTransform{.position = this->solver.current_position()},
                .dynamic       = true,
            };
            const std::vector<BouncingBallVertex>& vertices = this->solver.mesh_vertices();
            mesh.vertices.reserve(vertices.size());
            for (const BouncingBallVertex& vertex : vertices) {
                mesh.vertices.push_back(BouncingBallVisualMeshVertex{
                    .position = vertex.position,
                    .normal   = vertex.normal,
                });
            }
            mesh.indices = this->solver.mesh_indices();
            return mesh;
        }

        void rebuild_meshes() {
            this->visual_meshes.clear();
            this->visual_meshes.push_back(this->make_floor_mesh());
            this->visual_meshes.push_back(this->make_ball_mesh());
        }
    };
} // namespace xayah::projects::bouncing_ball
