#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

import std;
import spectra;
import spectra.pathtracer;
import spectra.pathtracer.pbrt.panel;
import xayah.projects.bouncing_ball;
import xayah.projects.cloth;
import xayah.projects.pyro;
import xayah.projects.sparkles;
import spectra.rasterizer.renderer;
import spectra.scene;

namespace spectra::app {
    static_assert(pathtracer::PathtracerHost<Spectra>);
    static_assert(rasterizer::Host<Spectra>);
    static_assert(static_cast<std::underlying_type_t<DockSlot>>(pathtracer::PathtracerDockSlot::Center) == static_cast<std::underlying_type_t<DockSlot>>(DockSlot::Center));
    static_assert(static_cast<std::underlying_type_t<DockSlot>>(pathtracer::PathtracerDockSlot::Floating) == static_cast<std::underlying_type_t<DockSlot>>(DockSlot::Floating));
    static_assert(static_cast<std::underlying_type_t<DockSlot>>(rasterizer::DockSlot::Center) == static_cast<std::underlying_type_t<DockSlot>>(DockSlot::Center));
    static_assert(static_cast<std::underlying_type_t<DockSlot>>(rasterizer::DockSlot::Floating) == static_cast<std::underlying_type_t<DockSlot>>(DockSlot::Floating));

    [[nodiscard]] scene::Vector3 ToSceneVector3(const std::array<float, 3>& value) {
        return scene::Vector3{value[0], value[1], value[2]};
    }

    [[nodiscard]] scene::SceneDocument MakeRasterizerSimulationDocument(std::string name, std::string title, std::string source) {
        return scene::SceneDocument{
            .revision        = scene::SceneRevision{1},
            .name            = std::move(name),
            .title           = std::move(title),
            .source          = std::move(source),
            .framesPerSecond = 60.0,
        };
    }

    [[nodiscard]] int ToPyroFrameIndex(const std::uint64_t frameIndex) {
        if (frameIndex > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Pyro frame index exceeds int range");
        return static_cast<int>(frameIndex);
    }

    class BouncingBallRasterizerProject final {
    public:
        [[nodiscard]] static std::string_view project_id() {
            return "project.bouncing_ball";
        }

        [[nodiscard]] static std::string_view project_title() {
            return "Bouncing Ball";
        }

        [[nodiscard]] scene::SceneDocument create_document() const {
            scene::SceneDocument document = MakeRasterizerSimulationDocument("project.bouncing_ball", "Bouncing Ball", "project://bouncing_ball");
            document.materials.push_back(scene::SceneMaterial{
                .name      = "ball",
                .baseColor = scene::Vector4{0.95f, 0.28f, 0.18f, 1.0f},
                .roughness = 0.42f,
            });
            document.materials.push_back(scene::SceneMaterial{
                .name      = "floor",
                .baseColor = scene::Vector4{0.38f, 0.43f, 0.38f, 1.0f},
                .roughness = 0.74f,
            });
            document.lights.push_back(scene::SceneLight{
                .name      = "key",
                .kind      = scene::SceneLightKind::Directional,
                .transform = scene::Transform{.rotation = scene::Quaternion{0.35f, 0.0f, 0.0f, 0.94f}},
                .color     = scene::Vector3{1.0f, 0.97f, 0.92f},
                .intensity = 3.0f,
            });
            document.camera = scene::SceneCamera{
                .name      = "camera.main",
                .transform = scene::Transform{.position = scene::Vector3{4.4f, 2.7f, 5.8f}},
                .target    = scene::Vector3{0.0f, 1.25f, 0.0f},
                .verticalFovDegrees = 42.0f,
                .nearPlane = 0.05f,
                .farPlane  = 80.0f,
            };
            document.meshes.push_back(this->make_floor_mesh());
            document.meshes.push_back(this->make_mesh());
            document.rigidBodies.push_back(scene::SceneRigidBody{
                .name         = "ball.body",
                .meshName     = "ball.mesh",
                .materialName = "ball",
                .transform    = scene::Transform{.position = ToSceneVector3(this->solver.current_position())},
                .mass         = 1.0f,
            });
            return document;
        }

        [[nodiscard]] scene::SceneFrameSnapshot reset() {
            this->solver.reset();
            return this->make_frame(scene::SceneFrameInfo{.delta_seconds = 0.0, .time_seconds = 0.0, .frame_index = 0u});
        }

        [[nodiscard]] scene::SceneFrameSnapshot step(const scene::SceneFrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            return this->make_frame(frame);
        }

    private:
        xayah::projects::bouncing_ball::BouncingBallSolver solver{};

        [[nodiscard]] scene::SceneMesh make_floor_mesh() const {
            return scene::SceneMesh{
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
                .materialName = "floor",
                .dynamic      = false,
            };
        }

        [[nodiscard]] scene::SceneMesh make_mesh() const {
            scene::SceneMesh mesh{
                .name         = "ball.mesh",
                .materialName = "ball",
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

        [[nodiscard]] scene::SceneFrameSnapshot make_frame(const scene::SceneFrameInfo& frame) const {
            scene::SceneFrameSnapshot snapshot{
                .cursor = scene::MakeFrameCursor(frame),
            };
            snapshot.meshes.push_back(this->make_mesh());
            snapshot.rigidBodies.push_back(scene::SceneRigidBody{
                .name         = "ball.body",
                .meshName     = "ball.mesh",
                .materialName = "ball",
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

        [[nodiscard]] scene::SceneDocument create_document() const {
            scene::SceneDocument document = MakeRasterizerSimulationDocument("project.sparkles", "Sparkles", "project://sparkles");
            document.materials.push_back(scene::SceneMaterial{
                .name             = "sparkle",
                .baseColor        = scene::Vector4{1.0f, 0.78f, 0.24f, 1.0f},
                .emissionColor    = scene::Vector3{1.0f, 0.54f, 0.12f},
                .emissionStrength = 2.4f,
                .roughness        = 0.2f,
            });
            document.lights.push_back(scene::SceneLight{
                .name      = "environment",
                .kind      = scene::SceneLightKind::Environment,
                .color     = scene::Vector3{0.22f, 0.26f, 0.34f},
                .intensity = 0.8f,
            });
            document.camera = scene::SceneCamera{
                .name      = "camera.main",
                .transform = scene::Transform{.position = scene::Vector3{4.6f, 3.1f, 6.0f}},
                .target    = scene::Vector3{0.0f, 1.9f, 0.0f},
                .verticalFovDegrees = 42.0f,
                .nearPlane = 0.05f,
                .farPlane  = 90.0f,
            };
            document.particleSets.push_back(this->make_particles());
            return document;
        }

        [[nodiscard]] scene::SceneFrameSnapshot reset() {
            this->solver.reset();
            return this->make_frame(scene::SceneFrameInfo{.delta_seconds = 0.0, .time_seconds = 0.0, .frame_index = 0u});
        }

        [[nodiscard]] scene::SceneFrameSnapshot step(const scene::SceneFrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            return this->make_frame(frame);
        }

    private:
        xayah::projects::sparkles::SparklesSolver solver{};

        [[nodiscard]] scene::SceneParticleSet make_particles() const {
            scene::SceneParticleSet particles{
                .name         = "sparkles.particles",
                .materialName = "sparkle",
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

        [[nodiscard]] scene::SceneFrameSnapshot make_frame(const scene::SceneFrameInfo& frame) const {
            scene::SceneFrameSnapshot snapshot{
                .cursor = scene::MakeFrameCursor(frame),
            };
            snapshot.particleSets.push_back(this->make_particles());
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

        [[nodiscard]] scene::SceneDocument create_document() const {
            scene::SceneDocument document = MakeRasterizerSimulationDocument("project.cloth", "Cloth", "project://cloth");
            document.materials.push_back(scene::SceneMaterial{
                .name      = "cloth",
                .baseColor = scene::Vector4{0.18f, 0.42f, 0.88f, 1.0f},
                .roughness = 0.64f,
            });
            document.materials.push_back(scene::SceneMaterial{
                .name      = "cloth.floor",
                .baseColor = scene::Vector4{0.34f, 0.38f, 0.36f, 1.0f},
                .roughness = 0.78f,
            });
            document.materials.push_back(scene::SceneMaterial{
                .name      = "cloth.collider",
                .baseColor = scene::Vector4{0.90f, 0.44f, 0.28f, 1.0f},
                .roughness = 0.46f,
            });
            document.lights.push_back(scene::SceneLight{
                .name      = "key",
                .kind      = scene::SceneLightKind::Directional,
                .transform = scene::Transform{.rotation = scene::Quaternion{0.45f, 0.18f, 0.0f, 0.87f}},
                .color     = scene::Vector3{0.92f, 0.96f, 1.0f},
                .intensity = 3.6f,
            });
            document.camera = scene::SceneCamera{
                .name      = "camera.main",
                .transform = scene::Transform{.position = scene::Vector3{3.8f, 2.8f, 5.2f}},
                .target    = scene::Vector3{0.0f, 1.05f, 0.0f},
                .verticalFovDegrees = 42.0f,
                .nearPlane = 0.05f,
                .farPlane  = 90.0f,
            };
            document.meshes.push_back(this->make_floor_mesh());
            document.meshes.push_back(this->make_collider_mesh());
            document.meshes.push_back(this->make_mesh());
            document.cloths.push_back(scene::SceneCloth{
                .name         = "cloth.body",
                .meshName     = "cloth.mesh",
                .materialName = "cloth",
                .dynamic      = true,
            });
            document.colliders.push_back(scene::SceneCollider{
                .name      = "cloth.collider",
                .meshName  = "cloth.collider.mesh",
                .transform = scene::Transform{.position = ToSceneVector3(this->collider.center)},
                .friction  = 0.54f,
            });
            return document;
        }

        [[nodiscard]] scene::SceneFrameSnapshot reset() {
            this->solver.reset();
            return this->make_frame(scene::SceneFrameInfo{.delta_seconds = 0.0, .time_seconds = 0.0, .frame_index = 0u});
        }

        [[nodiscard]] scene::SceneFrameSnapshot step(const scene::SceneFrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            return this->make_frame(frame);
        }

    private:
        xayah::projects::cloth::ClothConfig config{};
        xayah::projects::cloth::ClothSphereCollider collider{};
        xayah::projects::cloth::ClothSolver solver;

        [[nodiscard]] scene::SceneMesh make_floor_mesh() const {
            return scene::SceneMesh{
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
                .materialName = "cloth.floor",
                .dynamic      = false,
            };
        }

        [[nodiscard]] scene::SceneMesh make_collider_mesh() const {
            scene::SceneMesh mesh{
                .name         = "cloth.collider.mesh",
                .materialName = "cloth.collider",
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

        [[nodiscard]] scene::SceneMesh make_mesh() const {
            scene::SceneMesh mesh{
                .name         = "cloth.mesh",
                .materialName = "cloth",
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

        [[nodiscard]] scene::SceneFrameSnapshot make_frame(const scene::SceneFrameInfo& frame) const {
            scene::SceneFrameSnapshot snapshot{
                .cursor = scene::MakeFrameCursor(frame),
            };
            snapshot.meshes.push_back(this->make_mesh());
            snapshot.cloths.push_back(scene::SceneCloth{
                .name         = "cloth.body",
                .meshName     = "cloth.mesh",
                .materialName = "cloth",
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

        [[nodiscard]] scene::SceneDocument create_document() const {
            scene::SceneDocument document = MakeRasterizerSimulationDocument("project.pyro", "Pyro", "project://pyro");
            document.materials.push_back(scene::SceneMaterial{
                .name             = "smoke",
                .baseColor        = scene::Vector4{0.58f, 0.62f, 0.68f, 0.72f},
                .emissionColor    = scene::Vector3{0.95f, 0.48f, 0.18f},
                .emissionStrength = 0.3f,
                .roughness        = 0.9f,
            });
            document.lights.push_back(scene::SceneLight{
                .name      = "environment",
                .kind      = scene::SceneLightKind::Environment,
                .color     = scene::Vector3{0.24f, 0.26f, 0.30f},
                .intensity = 0.7f,
            });
            document.camera = scene::SceneCamera{
                .name      = "camera.main",
                .transform = scene::Transform{.position = scene::Vector3{2.35f, 1.65f, 2.8f}},
                .target    = scene::Vector3{0.6f, 0.86f, 0.6f},
                .verticalFovDegrees = 39.0f,
                .nearPlane = 0.03f,
                .farPlane  = 80.0f,
            };
            document.volumes.push_back(this->make_volume_metadata(0u));
            return document;
        }

        [[nodiscard]] scene::SceneFrameSnapshot reset() {
            this->solver.reset();
            return this->make_frame(scene::SceneFrameInfo{.delta_seconds = 0.0, .time_seconds = 0.0, .frame_index = 0u});
        }

        [[nodiscard]] scene::SceneFrameSnapshot step(const scene::SceneFrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            return this->make_frame(frame);
        }

    private:
        xayah::projects::pyro::PyroSolver solver{};
        std::array<std::uint32_t, 3> resolution{64u, 96u, 64u};
        float cell_size{0.01875f};

        [[nodiscard]] scene::SceneVolumeChannel make_volume_channel(std::string name, const scene::SceneVolumeChannelLayout layout, const std::array<std::uint32_t, 3> dimensions, std::vector<float> values) const {
            return scene::SceneVolumeChannel{
                .name       = std::move(name),
                .layout     = layout,
                .dimensions = dimensions,
                .values     = std::move(values),
            };
        }

        [[nodiscard]] scene::SceneVolumeGrid make_volume_metadata(const std::uint64_t) const {
            return scene::SceneVolumeGrid{
                .name         = "pyro.volume",
                .kind         = scene::SceneVolumeKind::GasDensity,
                .dimensions   = this->resolution,
                .origin       = scene::Vector3{0.0f, 0.0f, 0.0f},
                .voxelSize    = scene::Vector3{this->cell_size, this->cell_size, this->cell_size},
                .materialName = "smoke",
                .dynamic      = true,
            };
        }

        [[nodiscard]] scene::SceneVolumeGrid make_volume(const xayah::projects::pyro::PyroFrame& frame) const {
            scene::SceneVolumeGrid volume = this->make_volume_metadata(static_cast<std::uint64_t>(frame.frame_index));
            volume.dimensions = frame.resolution;
            volume.voxelSize = scene::Vector3{frame.cell_size, frame.cell_size, frame.cell_size};
            volume.channels.push_back(this->make_volume_channel("density", scene::SceneVolumeChannelLayout::CellCentered, frame.resolution, frame.density));
            volume.channels.push_back(this->make_volume_channel("temperature", scene::SceneVolumeChannelLayout::CellCentered, frame.resolution, frame.temperature));
            volume.channels.push_back(this->make_volume_channel("velocity_x", scene::SceneVolumeChannelLayout::FaceX, std::array<std::uint32_t, 3>{frame.resolution[0] + 1u, frame.resolution[1], frame.resolution[2]}, frame.velocity_x));
            volume.channels.push_back(this->make_volume_channel("velocity_y", scene::SceneVolumeChannelLayout::FaceY, std::array<std::uint32_t, 3>{frame.resolution[0], frame.resolution[1] + 1u, frame.resolution[2]}, frame.velocity_y));
            volume.channels.push_back(this->make_volume_channel("velocity_z", scene::SceneVolumeChannelLayout::FaceZ, std::array<std::uint32_t, 3>{frame.resolution[0], frame.resolution[1], frame.resolution[2] + 1u}, frame.velocity_z));
            return volume;
        }

        [[nodiscard]] scene::SceneFrameSnapshot make_frame(const scene::SceneFrameInfo& frame) {
            const xayah::projects::pyro::PyroFrame pyroFrame = this->solver.read_frame(ToPyroFrameIndex(frame.frame_index));
            this->resolution                                   = pyroFrame.resolution;
            this->cell_size                                    = pyroFrame.cell_size;
            scene::SceneFrameSnapshot snapshot{
                .cursor = scene::MakeFrameCursor(frame),
            };
            snapshot.volumes.push_back(this->make_volume(pyroFrame));
            return snapshot;
        }
    };

    static_assert(scene::SceneSimulationAdapter<BouncingBallRasterizerProject>);
    static_assert(scene::SceneSimulationAdapter<SparklesRasterizerProject>);
    static_assert(scene::SceneSimulationAdapter<ClothRasterizerProject>);
    static_assert(scene::SceneSimulationAdapter<PyroRasterizerProject>);

    [[nodiscard]] scene::SceneSourceRegistry MakeSceneSourceRegistry() {
        scene::SceneSourceRegistry registry{};
        registry.register_static_scene(std::string{scene::CornellBoxSceneId}, "Cornell Box", [] { return scene::LoadPreviewSceneDocumentFromPbrt(scene::CornellBoxSceneId); });
        registry.register_simulation<BouncingBallRasterizerProject>();
        registry.register_simulation<SparklesRasterizerProject>();
        registry.register_simulation<ClothRasterizerProject>();
        registry.register_simulation<PyroRasterizerProject>();
        return registry;
    }

    void DrawRasterizerSceneControlPanel(scene::SceneSession& session) {
        const std::size_t selected_index = session.selected_index();
        const scene::SceneSourceEntry& selected_entry = session.entry(selected_index);
        ImGui::SeparatorText("Scene");
        if (ImGui::BeginCombo("Scene", selected_entry.title.c_str())) {
            for (std::size_t index = 0; index < session.size(); ++index) {
                const scene::SceneSourceEntry& entry = session.entry(index);
                const bool selected = index == selected_index;
                if (ImGui::Selectable(entry.title.c_str(), selected)) session.request_activate(index);
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("%s", selected_entry.id.c_str());
        ImGui::TextDisabled("%s", selected_entry.kind == scene::SceneSourceKind::Static ? "Static" : "Simulation");
        if (session.pending_switch()) ImGui::TextDisabled("Switching on next frame");
    }

    class PathtracerSpectraRenderer final {
    public:
        PathtracerSpectraRenderer(std::shared_ptr<scene::PbrtSceneBrowserSession> sceneBrowser, std::shared_ptr<scene::SceneCameraWorkspace> cameraWorkspace) : scene_browser(std::move(sceneBrowser)), camera_workspace(std::move(cameraWorkspace)) {
            if (this->scene_browser == nullptr) throw std::runtime_error("Pathtracer adapter requires a PBRT scene browser session");
            if (this->camera_workspace == nullptr) throw std::runtime_error("Pathtracer adapter requires a scene camera workspace");
            this->renderer = std::make_unique<pathtracer::PathtracerRenderer>(this->scene_browser->workspace(), this->camera_workspace);
            this->scene_panel = std::make_unique<pathtracer::PbrtScenePanel>(this->scene_browser);
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
            this->scene_panel->attach(host);
            this->renderer->attach(pathtracer::PathtracerHostView{host});
        }

        void detach() noexcept {
            this->renderer->detach();
            this->scene_panel->detach();
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
        std::shared_ptr<scene::PbrtSceneBrowserSession> scene_browser{};
        std::shared_ptr<scene::SceneCameraWorkspace> camera_workspace{};
        std::unique_ptr<pathtracer::PathtracerRenderer> renderer{};
        std::unique_ptr<pathtracer::PbrtScenePanel> scene_panel{};
    };

    class RasterizerSpectraRenderer final {
    public:
        RasterizerSpectraRenderer(scene::SceneSourceRegistry registry, std::shared_ptr<scene::SceneCameraWorkspace> cameraWorkspace) : scene_session(std::make_shared<scene::SceneSession>(std::move(registry))), camera_workspace(std::move(cameraWorkspace)), renderer(std::make_unique<rasterizer::Renderer>(this->scene_session->active_workspace(), this->camera_workspace)) {
            if (this->scene_session == nullptr) throw std::runtime_error("Rasterizer adapter requires a scene session");
            if (this->camera_workspace == nullptr) throw std::runtime_error("Rasterizer adapter requires a scene camera workspace");
            this->renderer->set_control_panel_extension([sceneSession = this->scene_session] { DrawRasterizerSceneControlPanel(*sceneSession); });
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
            if (this->scene_session->apply_pending_scene()) this->renderer->set_scene_workspace(this->scene_session->active_workspace(), this->camera_workspace);
            this->scene_session->update_active_scene(frame.delta_seconds);
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
        std::shared_ptr<scene::SceneSession> scene_session{};
        std::shared_ptr<scene::SceneCameraWorkspace> camera_workspace{};
        std::unique_ptr<rasterizer::Renderer> renderer{};
    };

    static_assert(RendererFor<PathtracerSpectraRenderer, Spectra>);
    static_assert(RendererFor<RasterizerSpectraRenderer, Spectra>);

    void RegisterRenderers(Spectra& app, std::shared_ptr<scene::PbrtSceneBrowserSession> pbrtSceneBrowser, scene::SceneSourceRegistry sceneSourceRegistry, std::shared_ptr<scene::SceneCameraWorkspace> cameraWorkspace) {
        if (pbrtSceneBrowser == nullptr) throw std::runtime_error("Renderer registration requires a PBRT scene browser session");
        if (cameraWorkspace == nullptr) throw std::runtime_error("Renderer registration requires a scene camera workspace");
        app.register_renderer(RasterizerSpectraRenderer{std::move(sceneSourceRegistry), cameraWorkspace});
        app.register_renderer(PathtracerSpectraRenderer{std::move(pbrtSceneBrowser), std::move(cameraWorkspace)});
    }
} // namespace spectra::app

int main(const int argc, char**) {
    try {
        if (argc != 1) throw std::runtime_error("usage: spectra_gui");

        std::shared_ptr<spectra::scene::PbrtSceneBrowserSession> pbrt_scene_browser = std::make_shared<spectra::scene::PbrtSceneBrowserSession>(std::string{spectra::scene::CornellBoxSceneId});
        spectra::scene::SceneSourceRegistry scene_source_registry = spectra::app::MakeSceneSourceRegistry();
        std::shared_ptr<spectra::scene::SceneCameraWorkspace> camera_workspace = std::make_shared<spectra::scene::SceneCameraWorkspace>();

        spectra::Spectra app{"Spectra"};
        spectra::app::RegisterRenderers(app, std::move(pbrt_scene_browser), std::move(scene_source_registry), std::move(camera_workspace));
        app.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
