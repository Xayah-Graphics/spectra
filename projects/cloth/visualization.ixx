export module xayah.projects.cloth.visualization;

import std;
import xayah.projects.cloth;

namespace xayah::projects::cloth {
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

    export class Visualization final {
    public:
        Visualization() : solver(this->config, this->collider) {}

        [[nodiscard]] static std::string_view visualization_id() {
            return "project.cloth";
        }

        [[nodiscard]] static std::string_view visualization_title() {
            return "Cloth";
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

    private:
        Config config{};
        SphereCollider collider{};
        Solver solver;
        std::vector<VisualMaterial> visual_materials{
            VisualMaterial{.name = "cloth", .base_color = std::array<float, 4>{0.18f, 0.42f, 0.88f, 1.0f}, .roughness = 0.64f},
            VisualMaterial{.name = "cloth.floor", .base_color = std::array<float, 4>{0.34f, 0.38f, 0.36f, 1.0f}, .roughness = 0.78f},
            VisualMaterial{.name = "cloth.collider", .alpha_mode = "blend", .base_color = std::array<float, 4>{0.90f, 0.44f, 0.28f, 0.36f}, .roughness = 0.46f},
        };
        std::vector<VisualLight> visual_lights{
            VisualLight{
                .name      = "key",
                .kind      = "directional",
                .transform = VisualTransform{.rotation = std::array<float, 4>{-0.45f, 0.18f, 0.0f, 0.87f}},
                .color     = std::array<float, 3>{0.92f, 0.96f, 1.0f},
                .intensity = 3.6f,
            },
        };
        VisualCamera visual_camera{
            .name                 = "camera.main",
            .transform            = VisualTransform{.position = std::array<float, 3>{3.8f, 2.8f, 5.2f}},
            .target               = std::array<float, 3>{0.0f, 1.05f, 0.0f},
            .vertical_fov_degrees = 42.0f,
            .near_plane           = 0.05f,
            .far_plane            = 90.0f,
        };
        std::vector<VisualMesh> visual_meshes{};

        [[nodiscard]] VisualMesh make_floor_mesh() const {
            return VisualMesh{
                .name = "cloth.floor.mesh",
                .vertices = {
                    VisualMeshVertex{.position = std::array<float, 3>{-3.8f, -0.02f, -3.2f}},
                    VisualMeshVertex{.position = std::array<float, 3>{3.8f, -0.02f, -3.2f}},
                    VisualMeshVertex{.position = std::array<float, 3>{3.8f, -0.02f, 3.2f}},
                    VisualMeshVertex{.position = std::array<float, 3>{-3.8f, -0.02f, 3.2f}},
                },
                .indices       = {0u, 1u, 2u, 0u, 2u, 3u},
                .material_name = "cloth.floor",
                .dynamic       = false,
            };
        }

        [[nodiscard]] VisualMesh make_collider_mesh() const {
            VisualMesh mesh{
                .name          = "cloth.collider.mesh",
                .material_name = "cloth.collider",
                .transform     = VisualTransform{.position = this->collider.center},
                .dynamic       = false,
            };
            constexpr std::uint32_t latitude_segments = 18u;
            constexpr std::uint32_t longitude_segments = 32u;
            for (std::uint32_t latitude = 0; latitude <= latitude_segments; ++latitude) {
                const float theta = std::numbers::pi_v<float> * static_cast<float>(latitude) / static_cast<float>(latitude_segments);
                const float y = std::cos(theta);
                const float ring = std::sin(theta);
                for (std::uint32_t longitude = 0; longitude < longitude_segments; ++longitude) {
                    const float phi = 2.0f * std::numbers::pi_v<float> * static_cast<float>(longitude) / static_cast<float>(longitude_segments);
                    const std::array<float, 3> normal{ring * std::cos(phi), y, ring * std::sin(phi)};
                    mesh.vertices.push_back(VisualMeshVertex{
                        .position = std::array<float, 3>{normal[0] * this->collider.radius, normal[1] * this->collider.radius, normal[2] * this->collider.radius},
                        .normal   = normal,
                    });
                }
            }
            for (std::uint32_t latitude = 0; latitude < latitude_segments; ++latitude) {
                for (std::uint32_t longitude = 0; longitude < longitude_segments; ++longitude) {
                    const std::uint32_t next_longitude = (longitude + 1u) % longitude_segments;
                    const std::uint32_t i0 = latitude * longitude_segments + longitude;
                    const std::uint32_t i1 = latitude * longitude_segments + next_longitude;
                    const std::uint32_t i2 = (latitude + 1u) * longitude_segments + longitude;
                    const std::uint32_t i3 = (latitude + 1u) * longitude_segments + next_longitude;
                    mesh.indices.push_back(i0);
                    mesh.indices.push_back(i2);
                    mesh.indices.push_back(i1);
                    mesh.indices.push_back(i1);
                    mesh.indices.push_back(i2);
                    mesh.indices.push_back(i3);
                }
            }
            return mesh;
        }

        [[nodiscard]] VisualMesh make_cloth_mesh() const {
            VisualMesh mesh{
                .name          = "cloth.mesh",
                .material_name = "cloth",
                .dynamic       = true,
            };
            const std::vector<Vertex>& vertices = this->solver.mesh_vertices();
            mesh.vertices.reserve(vertices.size());
            for (const Vertex& vertex : vertices) {
                mesh.vertices.push_back(VisualMeshVertex{
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
            this->visual_meshes.push_back(this->make_collider_mesh());
            this->visual_meshes.push_back(this->make_cloth_mesh());
        }
    };
} // namespace xayah::projects::cloth
