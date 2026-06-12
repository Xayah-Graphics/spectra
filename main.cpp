#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

import std;
import spectra;
import spectra.pathtracer;
import xayah.projects.bouncing_ball;
import xayah.projects.cloth;
import xayah.projects.pyro;
import xayah.projects.sparkles;
import spectra.rasterizer.renderer;
import spectra.scene;

namespace spectra::app {
    inline constexpr std::string_view CornellBoxSceneId = "cornell-box/cornell-box.pbrt";

    static_assert(pathtracer::PathtracerHost<Spectra>);
    static_assert(rasterizer::Host<Spectra>);
    static_assert(static_cast<std::underlying_type_t<DockSlot>>(pathtracer::PathtracerDockSlot::Center) == static_cast<std::underlying_type_t<DockSlot>>(DockSlot::Center));
    static_assert(static_cast<std::underlying_type_t<DockSlot>>(pathtracer::PathtracerDockSlot::Floating) == static_cast<std::underlying_type_t<DockSlot>>(DockSlot::Floating));
    static_assert(static_cast<std::underlying_type_t<DockSlot>>(rasterizer::DockSlot::Center) == static_cast<std::underlying_type_t<DockSlot>>(DockSlot::Center));
    static_assert(static_cast<std::underlying_type_t<DockSlot>>(rasterizer::DockSlot::Floating) == static_cast<std::underlying_type_t<DockSlot>>(DockSlot::Floating));

    [[nodiscard]] scene::Vector3 ToSceneVector3(const std::array<float, 3>& value) {
        return scene::Vector3{value[0], value[1], value[2]};
    }

    [[nodiscard]] scene::Scene::Document MakeDynamicDocument(std::string name, std::string title, std::string source) {
        return scene::Scene::Document{
            .revision        = scene::Scene::Revision{1},
            .name            = std::move(name),
            .title           = std::move(title),
            .source          = std::move(source),
            .frames_per_second = 60.0,
        };
    }

    [[nodiscard]] int ToPyroFrameIndex(const std::uint64_t frame_index) {
        if (frame_index > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Pyro frame index exceeds int range");
        return static_cast<int>(frame_index);
    }

    class BouncingBallRasterizerProject final {
    public:
        [[nodiscard]] static std::string_view project_id() {
            return "project.bouncing_ball";
        }

        [[nodiscard]] static std::string_view project_title() {
            return "Bouncing Ball";
        }

        [[nodiscard]] scene::Scene::Document create_document() const {
            scene::Scene::Document document = MakeDynamicDocument("project.bouncing_ball", "Bouncing Ball", "project://bouncing_ball");
            document.materials.push_back(scene::Scene::Material{
                .name      = "ball",
                .base_color = scene::Vector4{0.95f, 0.28f, 0.18f, 1.0f},
                .roughness = 0.42f,
            });
            document.materials.push_back(scene::Scene::Material{
                .name      = "floor",
                .base_color = scene::Vector4{0.38f, 0.43f, 0.38f, 1.0f},
                .roughness = 0.74f,
            });
            document.lights.push_back(scene::Scene::Light{
                .name      = "key",
                .kind      = scene::Scene::LightKind::Directional,
                .transform = scene::Transform{.rotation = scene::Quaternion{0.35f, 0.0f, 0.0f, 0.94f}},
                .color     = scene::Vector3{1.0f, 0.97f, 0.92f},
                .intensity = 3.0f,
            });
            document.camera = scene::Scene::Camera{
                .name      = "camera.main",
                .transform = scene::Transform{.position = scene::Vector3{4.4f, 2.7f, 5.8f}},
                .target    = scene::Vector3{0.0f, 1.25f, 0.0f},
                .vertical_fov_degrees = 42.0f,
                .near_plane = 0.05f,
                .far_plane  = 80.0f,
            };
            document.meshes.push_back(this->make_floor_mesh());
            document.meshes.push_back(this->make_mesh());
            document.rigid_bodies.push_back(scene::Scene::RigidBody{
                .name         = "ball.body",
                .mesh_name     = "ball.mesh",
                .material_name = "ball",
                .transform    = scene::Transform{.position = ToSceneVector3(this->solver.current_position())},
                .mass         = 1.0f,
            });
            return document;
        }

        [[nodiscard]] scene::Scene::FrameSnapshot reset() {
            this->solver.reset();
            return this->make_frame(scene::Scene::FrameInfo{.delta_seconds = 0.0, .time_seconds = 0.0, .frame_index = 0u});
        }

        [[nodiscard]] scene::Scene::FrameSnapshot step(const scene::Scene::FrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            return this->make_frame(frame);
        }

    private:
        xayah::projects::bouncing_ball::BouncingBallSolver solver{};

        [[nodiscard]] scene::Scene::Mesh make_floor_mesh() const {
            return scene::Scene::Mesh{
                .name         = "floor.mesh",
                .positions    = {
                    scene::Vector3{-5.5f, 0.0f, -5.5f},
                    scene::Vector3{5.5f, 0.0f, -5.5f},
                    scene::Vector3{5.5f, 0.0f, 5.5f},
                    scene::Vector3{-5.5f, 0.0f, 5.5f},
                },
                .normals      = {
                    scene::Vector3{0.0f, 1.0f, 0.0f},
                    scene::Vector3{0.0f, 1.0f, 0.0f},
                    scene::Vector3{0.0f, 1.0f, 0.0f},
                    scene::Vector3{0.0f, 1.0f, 0.0f},
                },
                .indices      = {0u, 1u, 2u, 0u, 2u, 3u},
                .material_name = "floor",
                .dynamic      = false,
            };
        }

        [[nodiscard]] scene::Scene::Mesh make_mesh() const {
            scene::Scene::Mesh mesh{
                .name         = "ball.mesh",
                .material_name = "ball",
                .transform    = scene::Transform{.position = ToSceneVector3(this->solver.current_position())},
                .dynamic      = true,
            };
            const std::vector<xayah::projects::bouncing_ball::BouncingBallVertex>& vertices = this->solver.mesh_vertices();
            mesh.positions.reserve(vertices.size());
            mesh.normals.reserve(vertices.size());
            for (const xayah::projects::bouncing_ball::BouncingBallVertex& vertex : vertices) {
                mesh.positions.push_back(ToSceneVector3(vertex.position));
                mesh.normals.push_back(ToSceneVector3(vertex.normal));
            }
            mesh.indices = this->solver.mesh_indices();
            return mesh;
        }

        [[nodiscard]] scene::Scene::FrameSnapshot make_frame(const scene::Scene::FrameInfo& frame) const {
            scene::Scene::FrameSnapshot snapshot{
                .cursor = scene::Scene::make_frame_cursor(frame),
            };
            snapshot.meshes.push_back(this->make_mesh());
            snapshot.rigid_bodies.push_back(scene::Scene::RigidBody{
                .name         = "ball.body",
                .mesh_name     = "ball.mesh",
                .material_name = "ball",
                .transform    = scene::Transform{.position = ToSceneVector3(this->solver.current_position())},
                .mass         = 1.0f,
            });
            return snapshot;
        }
    };

    class SparklesRasterizerProject final {
    public:
        [[nodiscard]] static std::string_view project_id() {
            return "project.sparkles";
        }

        [[nodiscard]] static std::string_view project_title() {
            return "Sparkles";
        }

        [[nodiscard]] scene::Scene::Document create_document() const {
            scene::Scene::Document document = MakeDynamicDocument("project.sparkles", "Sparkles", "project://sparkles");
            document.materials.push_back(scene::Scene::Material{
                .name             = "sparkle",
                .base_color        = scene::Vector4{1.0f, 0.78f, 0.24f, 1.0f},
                .emission_color    = scene::Vector3{1.0f, 0.54f, 0.12f},
                .emission_strength = 2.4f,
                .roughness        = 0.2f,
            });
            document.lights.push_back(scene::Scene::Light{
                .name      = "environment",
                .kind      = scene::Scene::LightKind::Environment,
                .color     = scene::Vector3{0.22f, 0.26f, 0.34f},
                .intensity = 0.8f,
            });
            document.camera = scene::Scene::Camera{
                .name      = "camera.main",
                .transform = scene::Transform{.position = scene::Vector3{4.6f, 3.1f, 6.0f}},
                .target    = scene::Vector3{0.0f, 1.9f, 0.0f},
                .vertical_fov_degrees = 42.0f,
                .near_plane = 0.05f,
                .far_plane  = 90.0f,
            };
            document.particle_sets.push_back(this->make_particles());
            return document;
        }

        [[nodiscard]] scene::Scene::FrameSnapshot reset() {
            this->solver.reset();
            return this->make_frame(scene::Scene::FrameInfo{.delta_seconds = 0.0, .time_seconds = 0.0, .frame_index = 0u});
        }

        [[nodiscard]] scene::Scene::FrameSnapshot step(const scene::Scene::FrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            return this->make_frame(frame);
        }

    private:
        xayah::projects::sparkles::SparklesSolver solver{};

        [[nodiscard]] scene::Scene::ParticleSet make_particles() const {
            scene::Scene::ParticleSet particles{
                .name         = "sparkles.particles",
                .material_name = "sparkle",
                .dynamic      = true,
            };
            const std::span<const xayah::projects::sparkles::SparklesParticle> visibleParticles = this->solver.particles();
            particles.positions.reserve(visibleParticles.size());
            particles.radii.reserve(visibleParticles.size());
            particles.colors.reserve(visibleParticles.size());
            for (const xayah::projects::sparkles::SparklesParticle& particle : visibleParticles) {
                particles.positions.push_back(ToSceneVector3(particle.position));
                particles.radii.push_back(particle.radius);
                particles.colors.push_back(scene::Vector4{particle.color[0], particle.color[1], particle.color[2], 1.0f});
            }
            return particles;
        }

        [[nodiscard]] scene::Scene::FrameSnapshot make_frame(const scene::Scene::FrameInfo& frame) const {
            scene::Scene::FrameSnapshot snapshot{
                .cursor = scene::Scene::make_frame_cursor(frame),
            };
            snapshot.particle_sets.push_back(this->make_particles());
            return snapshot;
        }
    };

    class ClothRasterizerProject final {
    public:
        ClothRasterizerProject() : solver(this->config, this->collider) {}

        [[nodiscard]] static std::string_view project_id() {
            return "project.cloth";
        }

        [[nodiscard]] static std::string_view project_title() {
            return "Cloth";
        }

        [[nodiscard]] scene::Scene::Document create_document() const {
            scene::Scene::Document document = MakeDynamicDocument("project.cloth", "Cloth", "project://cloth");
            document.materials.push_back(scene::Scene::Material{
                .name      = "cloth",
                .base_color = scene::Vector4{0.18f, 0.42f, 0.88f, 1.0f},
                .roughness = 0.64f,
            });
            document.materials.push_back(scene::Scene::Material{
                .name      = "cloth.floor",
                .base_color = scene::Vector4{0.34f, 0.38f, 0.36f, 1.0f},
                .roughness = 0.78f,
            });
            document.materials.push_back(scene::Scene::Material{
                .name      = "cloth.collider",
                .base_color = scene::Vector4{0.90f, 0.44f, 0.28f, 1.0f},
                .roughness = 0.46f,
            });
            document.lights.push_back(scene::Scene::Light{
                .name      = "key",
                .kind      = scene::Scene::LightKind::Directional,
                .transform = scene::Transform{.rotation = scene::Quaternion{0.45f, 0.18f, 0.0f, 0.87f}},
                .color     = scene::Vector3{0.92f, 0.96f, 1.0f},
                .intensity = 3.6f,
            });
            document.camera = scene::Scene::Camera{
                .name      = "camera.main",
                .transform = scene::Transform{.position = scene::Vector3{3.8f, 2.8f, 5.2f}},
                .target    = scene::Vector3{0.0f, 1.05f, 0.0f},
                .vertical_fov_degrees = 42.0f,
                .near_plane = 0.05f,
                .far_plane  = 90.0f,
            };
            document.meshes.push_back(this->make_floor_mesh());
            document.meshes.push_back(this->make_collider_mesh());
            document.meshes.push_back(this->make_mesh());
            document.cloths.push_back(scene::Scene::Cloth{
                .name         = "cloth.body",
                .mesh_name     = "cloth.mesh",
                .material_name = "cloth",
                .dynamic      = true,
            });
            document.colliders.push_back(scene::Scene::Collider{
                .name      = "cloth.collider",
                .mesh_name  = "cloth.collider.mesh",
                .transform = scene::Transform{.position = ToSceneVector3(this->collider.center)},
                .friction  = 0.54f,
            });
            return document;
        }

        [[nodiscard]] scene::Scene::FrameSnapshot reset() {
            this->solver.reset();
            return this->make_frame(scene::Scene::FrameInfo{.delta_seconds = 0.0, .time_seconds = 0.0, .frame_index = 0u});
        }

        [[nodiscard]] scene::Scene::FrameSnapshot step(const scene::Scene::FrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            return this->make_frame(frame);
        }

    private:
        xayah::projects::cloth::ClothConfig config{};
        xayah::projects::cloth::ClothSphereCollider collider{};
        xayah::projects::cloth::ClothSolver solver;

        [[nodiscard]] scene::Scene::Mesh make_floor_mesh() const {
            return scene::Scene::Mesh{
                .name         = "cloth.floor.mesh",
                .positions    = {
                    scene::Vector3{-3.8f, -0.02f, -3.2f},
                    scene::Vector3{3.8f, -0.02f, -3.2f},
                    scene::Vector3{3.8f, -0.02f, 3.2f},
                    scene::Vector3{-3.8f, -0.02f, 3.2f},
                },
                .normals      = {
                    scene::Vector3{0.0f, 1.0f, 0.0f},
                    scene::Vector3{0.0f, 1.0f, 0.0f},
                    scene::Vector3{0.0f, 1.0f, 0.0f},
                    scene::Vector3{0.0f, 1.0f, 0.0f},
                },
                .indices      = {0u, 1u, 2u, 0u, 2u, 3u},
                .material_name = "cloth.floor",
                .dynamic      = false,
            };
        }

        [[nodiscard]] scene::Scene::Mesh make_collider_mesh() const {
            scene::Scene::Mesh mesh{
                .name         = "cloth.collider.mesh",
                .material_name = "cloth.collider",
                .transform    = scene::Transform{.position = ToSceneVector3(this->collider.center)},
                .dynamic      = false,
            };
            constexpr std::uint32_t latitude_segments = 18u;
            constexpr std::uint32_t longitude_segments = 32u;
            for (std::uint32_t latitude = 0; latitude <= latitude_segments; ++latitude) {
                const float theta = std::numbers::pi_v<float> * static_cast<float>(latitude) / static_cast<float>(latitude_segments);
                const float y = std::cos(theta);
                const float ring = std::sin(theta);
                for (std::uint32_t longitude = 0; longitude < longitude_segments; ++longitude) {
                    const float phi = 2.0f * std::numbers::pi_v<float> * static_cast<float>(longitude) / static_cast<float>(longitude_segments);
                    const scene::Vector3 normal{ring * std::cos(phi), y, ring * std::sin(phi)};
                    mesh.positions.push_back(scene::Vector3{normal.x * this->collider.radius, normal.y * this->collider.radius, normal.z * this->collider.radius});
                    mesh.normals.push_back(normal);
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

        [[nodiscard]] scene::Scene::Mesh make_mesh() const {
            scene::Scene::Mesh mesh{
                .name         = "cloth.mesh",
                .material_name = "cloth",
                .dynamic      = true,
            };
            const std::vector<xayah::projects::cloth::ClothVertex>& vertices = this->solver.mesh_vertices();
            mesh.positions.reserve(vertices.size());
            mesh.normals.reserve(vertices.size());
            for (const xayah::projects::cloth::ClothVertex& vertex : vertices) {
                mesh.positions.push_back(ToSceneVector3(vertex.position));
                mesh.normals.push_back(ToSceneVector3(vertex.normal));
            }
            mesh.indices = this->solver.mesh_indices();
            return mesh;
        }

        [[nodiscard]] scene::Scene::FrameSnapshot make_frame(const scene::Scene::FrameInfo& frame) const {
            scene::Scene::FrameSnapshot snapshot{
                .cursor = scene::Scene::make_frame_cursor(frame),
            };
            snapshot.meshes.push_back(this->make_mesh());
            snapshot.cloths.push_back(scene::Scene::Cloth{
                .name         = "cloth.body",
                .mesh_name     = "cloth.mesh",
                .material_name = "cloth",
                .dynamic      = true,
            });
            return snapshot;
        }
    };

    class PyroRasterizerProject final {
    public:
        [[nodiscard]] static std::string_view project_id() {
            return "project.pyro";
        }

        [[nodiscard]] static std::string_view project_title() {
            return "Pyro";
        }

        [[nodiscard]] scene::Scene::Document create_document() const {
            scene::Scene::Document document = MakeDynamicDocument("project.pyro", "Pyro", "project://pyro");
            document.materials.push_back(scene::Scene::Material{
                .name             = "smoke",
                .base_color        = scene::Vector4{0.58f, 0.62f, 0.68f, 0.72f},
                .emission_color    = scene::Vector3{0.95f, 0.48f, 0.18f},
                .emission_strength = 0.3f,
                .roughness        = 0.9f,
            });
            document.lights.push_back(scene::Scene::Light{
                .name      = "environment",
                .kind      = scene::Scene::LightKind::Environment,
                .color     = scene::Vector3{0.24f, 0.26f, 0.30f},
                .intensity = 0.7f,
            });
            document.camera = scene::Scene::Camera{
                .name      = "camera.main",
                .transform = scene::Transform{.position = scene::Vector3{2.35f, 1.65f, 2.8f}},
                .target    = scene::Vector3{0.6f, 0.86f, 0.6f},
                .vertical_fov_degrees = 39.0f,
                .near_plane = 0.03f,
                .far_plane  = 80.0f,
            };
            document.volumes.push_back(this->make_volume_metadata(0u));
            return document;
        }

        [[nodiscard]] scene::Scene::FrameSnapshot reset() {
            this->solver.reset();
            return this->make_frame(scene::Scene::FrameInfo{.delta_seconds = 0.0, .time_seconds = 0.0, .frame_index = 0u});
        }

        [[nodiscard]] scene::Scene::FrameSnapshot step(const scene::Scene::FrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            return this->make_frame(frame);
        }

    private:
        xayah::projects::pyro::PyroSolver solver{};
        std::array<std::uint32_t, 3> resolution{64u, 96u, 64u};
        float cell_size{0.01875f};

        [[nodiscard]] scene::Scene::VolumeChannel make_volume_channel(std::string name, const scene::Scene::VolumeChannelLayout layout, const std::array<std::uint32_t, 3> dimensions, std::vector<float> values) const {
            return scene::Scene::VolumeChannel{
                .name       = std::move(name),
                .layout     = layout,
                .dimensions = dimensions,
                .values     = std::move(values),
            };
        }

        [[nodiscard]] scene::Scene::VolumeGrid make_volume_metadata(const std::uint64_t) const {
            return scene::Scene::VolumeGrid{
                .name         = "pyro.volume",
                .kind         = scene::Scene::VolumeKind::GasDensity,
                .dimensions   = this->resolution,
                .origin       = scene::Vector3{0.0f, 0.0f, 0.0f},
                .voxel_size    = scene::Vector3{this->cell_size, this->cell_size, this->cell_size},
                .material_name = "smoke",
                .dynamic      = true,
            };
        }

        [[nodiscard]] scene::Scene::VolumeGrid make_volume(const xayah::projects::pyro::PyroFrame& frame) const {
            scene::Scene::VolumeGrid volume = this->make_volume_metadata(static_cast<std::uint64_t>(frame.frame_index));
            volume.dimensions = frame.resolution;
            volume.voxel_size = scene::Vector3{frame.cell_size, frame.cell_size, frame.cell_size};
            volume.channels.push_back(this->make_volume_channel("density", scene::Scene::VolumeChannelLayout::CellCentered, frame.resolution, frame.density));
            volume.channels.push_back(this->make_volume_channel("temperature", scene::Scene::VolumeChannelLayout::CellCentered, frame.resolution, frame.temperature));
            volume.channels.push_back(this->make_volume_channel("velocity_x", scene::Scene::VolumeChannelLayout::FaceX, std::array<std::uint32_t, 3>{frame.resolution[0] + 1u, frame.resolution[1], frame.resolution[2]}, frame.velocity_x));
            volume.channels.push_back(this->make_volume_channel("velocity_y", scene::Scene::VolumeChannelLayout::FaceY, std::array<std::uint32_t, 3>{frame.resolution[0], frame.resolution[1] + 1u, frame.resolution[2]}, frame.velocity_y));
            volume.channels.push_back(this->make_volume_channel("velocity_z", scene::Scene::VolumeChannelLayout::FaceZ, std::array<std::uint32_t, 3>{frame.resolution[0], frame.resolution[1], frame.resolution[2] + 1u}, frame.velocity_z));
            return volume;
        }

        [[nodiscard]] scene::Scene::FrameSnapshot make_frame(const scene::Scene::FrameInfo& frame) {
            const xayah::projects::pyro::PyroFrame pyroFrame = this->solver.read_frame(ToPyroFrameIndex(frame.frame_index));
            this->resolution                                   = pyroFrame.resolution;
            this->cell_size                                    = pyroFrame.cell_size;
            scene::Scene::FrameSnapshot snapshot{
                .cursor = scene::Scene::make_frame_cursor(frame),
            };
            snapshot.volumes.push_back(this->make_volume(pyroFrame));
            return snapshot;
        }
    };

    template <typename Source>
    concept SceneFrameSource = std::default_initializable<Source> && requires(Source& source, const Source& const_source, const scene::Scene::FrameInfo& frame) {
        { Source::project_id() } -> std::convertible_to<std::string_view>;
        { Source::project_title() } -> std::convertible_to<std::string_view>;
        { const_source.create_document() } -> std::same_as<scene::Scene::Document>;
        { source.reset() } -> std::same_as<scene::Scene::FrameSnapshot>;
        { source.step(frame) } -> std::same_as<scene::Scene::FrameSnapshot>;
    };

    class SceneFrameRuntime {
    public:
        SceneFrameRuntime() = default;

        SceneFrameRuntime(const SceneFrameRuntime& other) = delete;
        SceneFrameRuntime(SceneFrameRuntime&& other) = delete;
        SceneFrameRuntime& operator=(const SceneFrameRuntime& other) = delete;
        SceneFrameRuntime& operator=(SceneFrameRuntime&& other) = delete;
        virtual ~SceneFrameRuntime() noexcept = default;

        [[nodiscard]] virtual scene::Scene::Document create_document() const = 0;
        [[nodiscard]] virtual scene::Scene::FrameSnapshot reset() = 0;
        [[nodiscard]] virtual scene::Scene::FrameSnapshot step(const scene::Scene::FrameInfo& frame) = 0;
    };

    template <SceneFrameSource Source>
    class SceneFrameRuntimeModel final : public SceneFrameRuntime {
    public:
        SceneFrameRuntimeModel() = default;

        [[nodiscard]] scene::Scene::Document create_document() const override {
            return this->source.create_document();
        }

        [[nodiscard]] scene::Scene::FrameSnapshot reset() override {
            return this->source.reset();
        }

        [[nodiscard]] scene::Scene::FrameSnapshot step(const scene::Scene::FrameInfo& frame) override {
            return this->source.step(frame);
        }

    private:
        Source source{};
    };

    enum class AppSceneKind {
        Static,
        Dynamic,
    };

    struct AppSceneEntry {
        std::string id{};
        std::string title{};
        AppSceneKind kind{AppSceneKind::Static};
        std::move_only_function<scene::Scene::Document()> create_static_document{};
        std::move_only_function<std::unique_ptr<SceneFrameRuntime>()> create_dynamic_runtime{};
    };

    class AppSceneRegistry final {
    public:
        AppSceneRegistry() = default;

        AppSceneRegistry(const AppSceneRegistry& other) = delete;
        AppSceneRegistry(AppSceneRegistry&& other) noexcept = default;
        AppSceneRegistry& operator=(const AppSceneRegistry& other) = delete;
        AppSceneRegistry& operator=(AppSceneRegistry&& other) noexcept = default;
        ~AppSceneRegistry() noexcept = default;

        void register_static_scene(std::string id, std::string title, std::move_only_function<scene::Scene::Document()> create_document) {
            if (!create_document) throw std::runtime_error("Static scene entry requires a document factory");
            this->ensure_unique_scene_id(id);
            this->entries.push_back(AppSceneEntry{
                .id                     = std::move(id),
                .title                  = std::move(title),
                .kind                   = AppSceneKind::Static,
                .create_static_document = std::move(create_document),
            });
        }

        template <SceneFrameSource Source>
        void register_dynamic_scene() {
            const std::string id{Source::project_id()};
            this->ensure_unique_scene_id(id);
            this->entries.push_back(AppSceneEntry{
                .id                     = id,
                .title                  = std::string{Source::project_title()},
                .kind                   = AppSceneKind::Dynamic,
                .create_dynamic_runtime = [] { return std::make_unique<SceneFrameRuntimeModel<Source>>(); },
            });
        }

        [[nodiscard]] std::unique_ptr<SceneFrameRuntime> create_dynamic_runtime(const std::size_t index) {
            if (this->entries.empty()) throw std::runtime_error("App scene registry is empty");
            if (index >= this->entries.size()) throw std::runtime_error("App scene index is out of range");
            AppSceneEntry& entry = this->entries.at(index);
            if (entry.kind != AppSceneKind::Dynamic) throw std::runtime_error("App scene entry is not dynamic");
            if (!entry.create_dynamic_runtime) throw std::runtime_error("Dynamic scene entry has no runtime factory");
            return entry.create_dynamic_runtime();
        }

        [[nodiscard]] scene::Scene::Document create_static_document(const std::size_t index) {
            if (this->entries.empty()) throw std::runtime_error("App scene registry is empty");
            if (index >= this->entries.size()) throw std::runtime_error("App scene index is out of range");
            AppSceneEntry& entry = this->entries.at(index);
            if (entry.kind != AppSceneKind::Static) throw std::runtime_error("App scene entry is not static");
            if (!entry.create_static_document) throw std::runtime_error("Static scene entry has no document factory");
            return entry.create_static_document();
        }

        [[nodiscard]] const AppSceneEntry& entry(const std::size_t index) const {
            if (index >= this->entries.size()) throw std::runtime_error("App scene index is out of range");
            return this->entries.at(index);
        }

        [[nodiscard]] std::size_t size() const {
            return this->entries.size();
        }

    private:
        void ensure_unique_scene_id(const std::string& id) const {
            if (id.empty()) throw std::runtime_error("App scene id must not be empty");
            for (const AppSceneEntry& entry : this->entries) {
                if (entry.id == id) throw std::runtime_error("Duplicate app scene id: " + id);
            }
        }

        std::vector<AppSceneEntry> entries{};
    };

    void CommitSceneFrame(scene::Scene& workspace, scene::Scene::FrameSnapshot frame) {
        scene::Scene::Edit edit{};
        edit.replace_frame(std::move(frame));
        const scene::Scene::DirtyFlags dirty = workspace.commit(std::move(edit));
        if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Frame)) throw std::runtime_error("Scene frame commit did not mark the frame dirty");
    }

    void CommitSceneTimeline(scene::Scene& workspace, scene::Scene::Timeline timeline) {
        scene::Scene::Edit edit{};
        edit.replace_timeline(std::move(timeline));
        const scene::Scene::DirtyFlags dirty = workspace.commit(std::move(edit));
        if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Timeline)) throw std::runtime_error("Scene timeline commit did not mark the timeline dirty");
    }

    void CommitSceneTimelineAndFrame(scene::Scene& workspace, scene::Scene::Timeline timeline, scene::Scene::FrameSnapshot frame) {
        scene::Scene::Edit edit{};
        edit.replace_timeline(std::move(timeline));
        edit.replace_frame(std::move(frame));
        const scene::Scene::DirtyFlags dirty = workspace.commit(std::move(edit));
        if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Timeline)) throw std::runtime_error("Scene timeline commit did not mark the timeline dirty");
        if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Frame)) throw std::runtime_error("Scene frame commit did not mark the frame dirty");
    }

    class AppSceneController final {
    public:
        explicit AppSceneController(AppSceneRegistry registry) : registry(std::move(registry)) {
            if (this->registry.size() == 0u) throw std::runtime_error("App scene controller requires at least one scene");
            this->slots.resize(this->registry.size());
            this->ensure_slot(0u);
        }

        AppSceneController(const AppSceneController& other) = delete;
        AppSceneController(AppSceneController&& other) = delete;
        AppSceneController& operator=(const AppSceneController& other) = delete;
        AppSceneController& operator=(AppSceneController&& other) = delete;
        ~AppSceneController() noexcept = default;

        [[nodiscard]] std::shared_ptr<scene::Scene> active_workspace() {
            return this->ensure_slot(this->currentActiveIndex).workspace;
        }

        [[nodiscard]] const AppSceneEntry& entry(const std::size_t index) const {
            return this->registry.entry(index);
        }

        [[nodiscard]] std::size_t size() const {
            return this->registry.size();
        }

        [[nodiscard]] std::size_t selected_index() const {
            return this->pendingActiveIndex.value_or(this->currentActiveIndex);
        }

        [[nodiscard]] bool pending_switch() const {
            return this->pendingActiveIndex.has_value() && *this->pendingActiveIndex != this->currentActiveIndex;
        }

        void request_activate(const std::size_t index) {
            if (index >= this->slots.size()) throw std::runtime_error("App scene activation index is out of range");
            if (index == this->currentActiveIndex) {
                this->pendingActiveIndex.reset();
                return;
            }
            this->pendingActiveIndex = index;
        }

        [[nodiscard]] bool apply_pending_scene() {
            if (!this->pendingActiveIndex.has_value()) return false;
            const std::size_t next_index = *this->pendingActiveIndex;
            this->pendingActiveIndex.reset();
            if (next_index >= this->slots.size()) throw std::runtime_error("Pending app scene index is out of range");
            if (next_index == this->currentActiveIndex) return false;
            this->ensure_slot(next_index);
            this->currentActiveIndex = next_index;
            return true;
        }

        void update_active_scene(const double delta_seconds) {
            const AppSceneEntry& entry = this->registry.entry(this->currentActiveIndex);
            if (entry.kind == AppSceneKind::Static) return;
            SceneSlot& slot = this->ensure_slot(this->currentActiveIndex);
            if (slot.runtime == nullptr) throw std::runtime_error("Dynamic scene slot has no runtime");
            const std::shared_ptr<const scene::Scene::Document> document = slot.workspace->document();
            if (!document->timeline_enabled) throw std::runtime_error("Dynamic scene must enable timeline");
            if (document->frames_per_second <= 0.0) throw std::runtime_error("Dynamic scene frame rate must be positive");
            const double fixed_delta_seconds = 1.0 / document->frames_per_second;
            scene::Scene::Timeline timeline = slot.workspace->timeline();
            if (timeline.frames_per_second <= 0.0) throw std::runtime_error("Scene timeline frame rate must be positive");
            if (timeline.reset_request_serial != slot.observed_reset_request_serial) {
                this->reset_dynamic_scene(slot, std::move(timeline));
                slot.observed_reset_request_serial = slot.workspace->timeline().reset_request_serial;
                slot.committed_playback_frame_index.reset();
                return;
            }
            if (timeline.clear_recording_request_serial != slot.observed_clear_recording_request_serial) {
                timeline.recorded_frames.clear();
                timeline.selected_frame_index = 0;
                CommitSceneTimeline(*slot.workspace, std::move(timeline));
                slot.observed_clear_recording_request_serial = slot.workspace->timeline().clear_recording_request_serial;
                slot.committed_playback_frame_index.reset();
                return;
            }
            if (timeline.mode == scene::Scene::TimelineMode::Playback) {
                if (timeline.recorded_frames.empty()) return;
                if (timeline.selected_frame_index >= timeline.recorded_frames.size()) throw std::runtime_error("Scene playback selected frame is out of range");
                if (slot.committed_playback_frame_index.has_value() && *slot.committed_playback_frame_index == timeline.selected_frame_index) return;
                scene::Scene::FrameSnapshot selected_frame = timeline.recorded_frames.at(timeline.selected_frame_index);
                CommitSceneFrame(*slot.workspace, std::move(selected_frame));
                slot.committed_playback_frame_index = timeline.selected_frame_index;
                return;
            }
            slot.committed_playback_frame_index.reset();
            if (!timeline.playing) return;
            if (!std::isfinite(delta_seconds) || delta_seconds < 0.0) throw std::runtime_error("Scene frame delta time is invalid");
            slot.frame_accumulator_seconds += delta_seconds;
            bool advanced = false;
            scene::Scene::FrameSnapshot snapshot{};
            while (slot.frame_accumulator_seconds >= fixed_delta_seconds) {
                slot.frame_accumulator_seconds -= fixed_delta_seconds;
                ++slot.stream_frame_index;
                slot.stream_time_seconds += fixed_delta_seconds;
                snapshot = slot.runtime->step(scene::Scene::FrameInfo{
                    .delta_seconds = fixed_delta_seconds,
                    .time_seconds  = slot.stream_time_seconds,
                    .frame_index   = slot.stream_frame_index,
                });
                advanced = true;
            }
            if (!advanced) return;
            if (timeline.mode == scene::Scene::TimelineMode::Record) {
                timeline.recorded_frames.push_back(snapshot);
                timeline.selected_frame_index = timeline.recorded_frames.size() - 1u;
                CommitSceneTimelineAndFrame(*slot.workspace, std::move(timeline), std::move(snapshot));
                return;
            }
            CommitSceneFrame(*slot.workspace, std::move(snapshot));
        }

    private:
        struct SceneSlot {
            std::unique_ptr<SceneFrameRuntime> runtime{};
            std::shared_ptr<scene::Scene> workspace{};
            double frame_accumulator_seconds{};
            double stream_time_seconds{};
            std::uint64_t stream_frame_index{};
            std::uint64_t observed_reset_request_serial{};
            std::uint64_t observed_clear_recording_request_serial{};
            std::optional<std::uint64_t> committed_playback_frame_index{};
        };

        [[nodiscard]] SceneSlot& ensure_slot(const std::size_t index) {
            if (index >= this->slots.size()) throw std::runtime_error("App scene slot index is out of range");
            SceneSlot& slot = this->slots.at(index);
            const AppSceneEntry& entry = this->registry.entry(index);
            if (slot.workspace != nullptr) {
                if (entry.kind == AppSceneKind::Dynamic && slot.runtime == nullptr) throw std::runtime_error("Dynamic scene slot has a workspace but no runtime");
                return slot;
            }
            scene::Scene::Document document = entry.kind == AppSceneKind::Static ? this->registry.create_static_document(index) : this->create_dynamic_slot(index, &slot);
            slot.workspace = std::make_shared<scene::Scene>(std::move(document));
            if (entry.kind == AppSceneKind::Static) return slot;
            if (slot.runtime == nullptr) throw std::runtime_error("Dynamic scene slot was not initialized");
            scene::Scene::FrameSnapshot snapshot = slot.runtime->reset();
            const std::shared_ptr<const scene::Scene::Document> scene_document = slot.workspace->document();
            if (!scene_document->timeline_enabled) throw std::runtime_error("Dynamic scene must enable timeline");
            scene::Scene::Timeline timeline{
                .mode               = scene::Scene::TimelineMode::Live,
                .frames_per_second    = scene_document->frames_per_second,
                .playing            = true,
                .loop               = true,
                .selected_frame_index = 0,
            };
            CommitSceneTimelineAndFrame(*slot.workspace, std::move(timeline), std::move(snapshot));
            return slot;
        }

        [[nodiscard]] scene::Scene::Document create_dynamic_slot(const std::size_t index, SceneSlot* slot) {
            if (slot == nullptr) throw std::runtime_error("Dynamic scene slot pointer must not be null");
            slot->runtime = this->registry.create_dynamic_runtime(index);
            scene::Scene::Document document = slot->runtime->create_document();
            if (!document.timeline_enabled) throw std::runtime_error("Dynamic scene document must enable timeline");
            return document;
        }

        void reset_dynamic_scene(SceneSlot& slot, scene::Scene::Timeline timeline) {
            slot.frame_accumulator_seconds = 0.0;
            slot.stream_time_seconds = 0.0;
            slot.stream_frame_index = 0;
            scene::Scene::FrameSnapshot snapshot = slot.runtime->reset();
            timeline.selected_frame_index = 0;
            CommitSceneTimelineAndFrame(*slot.workspace, std::move(timeline), std::move(snapshot));
        }

        AppSceneRegistry registry{};
        std::vector<SceneSlot> slots{};
        std::size_t currentActiveIndex{};
        std::optional<std::size_t> pendingActiveIndex{};
    };

    static_assert(SceneFrameSource<BouncingBallRasterizerProject>);
    static_assert(SceneFrameSource<SparklesRasterizerProject>);
    static_assert(SceneFrameSource<ClothRasterizerProject>);
    static_assert(SceneFrameSource<PyroRasterizerProject>);

    [[nodiscard]] AppSceneRegistry MakeSceneRegistry() {
        AppSceneRegistry registry{};
        registry.register_static_scene(std::string{CornellBoxSceneId}, "Cornell Box", [] { return scene::PbrtScene::load_preview_document(CornellBoxSceneId); });
        registry.register_dynamic_scene<BouncingBallRasterizerProject>();
        registry.register_dynamic_scene<SparklesRasterizerProject>();
        registry.register_dynamic_scene<ClothRasterizerProject>();
        registry.register_dynamic_scene<PyroRasterizerProject>();
        return registry;
    }

    void DrawRasterizerSceneControlPanel(AppSceneController& session) {
        const std::size_t selected_index = session.selected_index();
        const AppSceneEntry& selected_entry = session.entry(selected_index);
        ImGui::SeparatorText("Scene");
        if (ImGui::BeginCombo("Scene", selected_entry.title.c_str())) {
            for (std::size_t index = 0; index < session.size(); ++index) {
                const AppSceneEntry& entry = session.entry(index);
                const bool selected = index == selected_index;
                if (ImGui::Selectable(entry.title.c_str(), selected)) session.request_activate(index);
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("%s", selected_entry.id.c_str());
        ImGui::TextDisabled("%s", selected_entry.kind == AppSceneKind::Static ? "Static" : "Dynamic");
        if (session.pending_switch()) ImGui::TextDisabled("Switching on next frame");
    }

    class PathtracerSpectraRenderer final {
    public:
        PathtracerSpectraRenderer(std::shared_ptr<const scene::PbrtScene> pbrtScene, std::shared_ptr<scene::Scene::CameraWorkspace> cameraWorkspace) : pbrt_scene(std::move(pbrtScene)), camera_workspace(std::move(cameraWorkspace)) {
            if (this->pbrt_scene == nullptr) throw std::runtime_error("Pathtracer adapter requires a PBRT scene");
            if (this->camera_workspace == nullptr) throw std::runtime_error("Pathtracer adapter requires a scene camera workspace");
            this->renderer = std::make_unique<pathtracer::PathtracerRenderer>(this->pbrt_scene, this->camera_workspace);
        }

        PathtracerSpectraRenderer(const PathtracerSpectraRenderer& other)                = delete;
        PathtracerSpectraRenderer(PathtracerSpectraRenderer&& other) noexcept            = default;
        PathtracerSpectraRenderer& operator=(const PathtracerSpectraRenderer& other)     = delete;
        PathtracerSpectraRenderer& operator=(PathtracerSpectraRenderer&& other) noexcept = default;
        ~PathtracerSpectraRenderer() noexcept                                            = default;

        [[nodiscard]] static std::string_view name() {
            return pathtracer::PathtracerRenderer::name();
        }

        void attach(Spectra& host) {
            this->renderer->attach(pathtracer::PathtracerHostView{host});
        }

        void detach() noexcept {
            this->renderer->detach();
        }

        void before_imgui_shutdown() noexcept {
            this->renderer->before_imgui_shutdown();
        }

        void after_imgui_created() {
            this->renderer->after_imgui_created();
        }

        [[nodiscard]] FrameResult begin_frame(Spectra& host, const FrameContext& frame) {
            const pathtracer::PathtracerFrameInfo pathtracer_frame{
                .frame_index = frame.frame_slot_index,
                .image_index = frame.image_index,
            };
            pathtracer::PathtracerFrameResult result = this->renderer->begin_frame(pathtracer::PathtracerHostView{host}, pathtracer_frame);
            return FrameResult{
                .completion_semaphore = std::move(result.completion_semaphore),
                .close_requested      = result.close_requested,
                .window_detail        = std::move(result.window_detail),
            };
        }

        void record_frame(const vk::raii::CommandBuffer& commandBuffer) {
            this->renderer->record_frame(commandBuffer);
        }

    private:
        std::shared_ptr<const scene::PbrtScene> pbrt_scene{};
        std::shared_ptr<scene::Scene::CameraWorkspace> camera_workspace{};
        std::unique_ptr<pathtracer::PathtracerRenderer> renderer{};
    };

    class RasterizerSpectraRenderer final {
    public:
        RasterizerSpectraRenderer(AppSceneRegistry registry, std::shared_ptr<scene::Scene::CameraWorkspace> cameraWorkspace) : scene_controller(std::make_shared<AppSceneController>(std::move(registry))), camera_workspace(std::move(cameraWorkspace)), renderer(std::make_unique<rasterizer::Renderer>(this->scene_controller->active_workspace(), this->camera_workspace)) {
            if (this->scene_controller == nullptr) throw std::runtime_error("Rasterizer adapter requires a scene controller");
            if (this->camera_workspace == nullptr) throw std::runtime_error("Rasterizer adapter requires a scene camera workspace");
            this->renderer->set_control_panel_extension([sceneController = this->scene_controller] { DrawRasterizerSceneControlPanel(*sceneController); });
        }

        RasterizerSpectraRenderer(const RasterizerSpectraRenderer& other)                = delete;
        RasterizerSpectraRenderer(RasterizerSpectraRenderer&& other) noexcept            = default;
        RasterizerSpectraRenderer& operator=(const RasterizerSpectraRenderer& other)     = delete;
        RasterizerSpectraRenderer& operator=(RasterizerSpectraRenderer&& other) noexcept = default;
        ~RasterizerSpectraRenderer() noexcept                                            = default;

        [[nodiscard]] static std::string_view name() {
            return rasterizer::Renderer::name();
        }

        void attach(Spectra& host) {
            this->renderer->attach(rasterizer::HostView{host});
        }

        void detach() noexcept {
            this->renderer->detach();
        }

        void before_imgui_shutdown() noexcept {
            this->renderer->before_imgui_shutdown();
        }

        void after_imgui_created() {
            this->renderer->after_imgui_created();
        }

        [[nodiscard]] FrameResult begin_frame(Spectra& host, const FrameContext& frame) {
            if (this->scene_controller->apply_pending_scene()) this->renderer->set_scene_workspace(this->scene_controller->active_workspace(), this->camera_workspace);
            this->scene_controller->update_active_scene(frame.delta_seconds);
            const rasterizer::FrameContext rasterizer_frame{
                .frame_index   = frame.frame_slot_index,
                .image_index   = frame.image_index,
                .frame_number  = frame.frame_number,
                .delta_seconds = frame.delta_seconds,
            };
            rasterizer::FrameResult result = this->renderer->begin_frame(rasterizer::HostView{host}, rasterizer_frame);
            return FrameResult{
                .completion_semaphore = std::move(result.completion_semaphore),
                .close_requested      = result.close_requested,
                .window_detail        = std::move(result.window_detail),
            };
        }

        void record_frame(const vk::raii::CommandBuffer& commandBuffer) {
            this->renderer->record_frame(commandBuffer);
        }

    private:
        std::shared_ptr<AppSceneController> scene_controller{};
        std::shared_ptr<scene::Scene::CameraWorkspace> camera_workspace{};
        std::unique_ptr<rasterizer::Renderer> renderer{};
    };

    static_assert(RendererFor<PathtracerSpectraRenderer, Spectra>);
    static_assert(RendererFor<RasterizerSpectraRenderer, Spectra>);

    void RegisterRenderers(Spectra& app, std::shared_ptr<const scene::PbrtScene> pbrtScene, AppSceneRegistry sceneRegistry, std::shared_ptr<scene::Scene::CameraWorkspace> cameraWorkspace) {
        if (pbrtScene == nullptr) throw std::runtime_error("Renderer registration requires a PBRT scene snapshot");
        if (cameraWorkspace == nullptr) throw std::runtime_error("Renderer registration requires a scene camera workspace");
        app.register_renderer(RasterizerSpectraRenderer{std::move(sceneRegistry), cameraWorkspace});
        app.register_renderer(PathtracerSpectraRenderer{std::move(pbrtScene), std::move(cameraWorkspace)});
    }
} // namespace spectra::app

int main(const int argc, char**) {
    try {
        if (argc != 1) throw std::runtime_error("usage: spectra_gui");

        std::shared_ptr<const spectra::scene::PbrtScene> pbrt_scene = std::make_shared<spectra::scene::PbrtScene>(spectra::scene::PbrtScene::parse(spectra::app::CornellBoxSceneId));
        spectra::app::AppSceneRegistry scene_registry = spectra::app::MakeSceneRegistry();
        std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace = std::make_shared<spectra::scene::Scene::CameraWorkspace>();

        spectra::Spectra app{"Spectra"};
        spectra::app::RegisterRenderers(app, std::move(pbrt_scene), std::move(scene_registry), std::move(camera_workspace));
        app.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
