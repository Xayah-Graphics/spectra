#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

import std;
import spectra;
import spectra.pathtracer;
import spectra.pathtracer.pbrt.library;
import xayah.projects.bouncing_ball;
import xayah.projects.cloth;
import xayah.projects.pyro;
import xayah.projects.sparkles;
import spectra.rasterizer;

namespace spectra::app {
    [[nodiscard]] SpectraDockSlot ToSpectraDockSlot(const pathtracer::PathtracerDockSlot dockSlot) {
        switch (dockSlot) {
        case pathtracer::PathtracerDockSlot::Center: return SpectraDockSlot::Center;
        case pathtracer::PathtracerDockSlot::Floating: return SpectraDockSlot::Floating;
        case pathtracer::PathtracerDockSlot::Left:
        case pathtracer::PathtracerDockSlot::LeftBottom:
        case pathtracer::PathtracerDockSlot::Right:
        case pathtracer::PathtracerDockSlot::RightBottom:
        case pathtracer::PathtracerDockSlot::Bottom: throw std::runtime_error("Pathtracer non-center dock panels are not supported by Spectra; use sidebar tabs instead");
        }
        throw std::runtime_error("Unknown pathtracer dock slot");
    }

    [[nodiscard]] SpectraDockSlot ToSpectraDockSlot(const rasterizer::RasterizerDockSlot dockSlot) {
        switch (dockSlot) {
        case rasterizer::RasterizerDockSlot::Center: return SpectraDockSlot::Center;
        case rasterizer::RasterizerDockSlot::Floating: return SpectraDockSlot::Floating;
        }
        throw std::runtime_error("Unknown rasterizer dock slot");
    }

    [[nodiscard]] SpectraPanel ToSpectraPanel(pathtracer::PathtracerPanel panel) {
        return SpectraPanel{
            .id                  = std::move(panel.id),
            .title               = std::move(panel.title),
            .icon                = std::move(panel.icon),
            .owner_renderer      = std::string{pathtracer::PathtracerRenderer::target_name()},
            .shortcut_label      = std::move(panel.shortcut_label),
            .shortcut_key        = panel.shortcut_key,
            .dock_slot           = ToSpectraDockSlot(panel.dock_slot),
            .window_flags        = panel.window_flags,
            .visible             = panel.visible,
            .closable            = panel.closable,
            .zero_window_padding = panel.zero_window_padding,
            .draw                = std::move(panel.draw),
        };
    }

    [[nodiscard]] SpectraSidebarTab ToSpectraSidebarTab(pathtracer::PathtracerSidebarTab tab) {
        return SpectraSidebarTab{
            .id             = std::move(tab.id),
            .title          = std::move(tab.title),
            .icon           = std::move(tab.icon),
            .owner_renderer = std::string{pathtracer::PathtracerRenderer::target_name()},
            .shortcut_label = std::move(tab.shortcut_label),
            .shortcut_key   = tab.shortcut_key,
            .draw           = std::move(tab.draw),
        };
    }

    [[nodiscard]] SpectraToolbarAction ToSpectraToolbarAction(pathtracer::PathtracerToolbarAction action) {
        return SpectraToolbarAction{
            .id             = std::move(action.id),
            .title          = std::move(action.title),
            .icon           = std::move(action.icon),
            .owner_renderer = std::string{pathtracer::PathtracerRenderer::target_name()},
            .shortcut_label = std::move(action.shortcut_label),
            .shortcut_key   = action.shortcut_key,
            .active         = std::move(action.active),
            .trigger        = std::move(action.trigger),
        };
    }

    struct RasterizerProjectFrameInfo {
        double delta_seconds{};
        double time_seconds{};
        std::uint64_t frame_index{};
    };

    template <typename Adapter>
    concept RasterizerProjectAdapter = std::default_initializable<Adapter> && requires(Adapter& adapter, const Adapter& constAdapter, const RasterizerProjectFrameInfo& frame) {
        { Adapter::project_id() } -> std::convertible_to<std::string_view>;
        { Adapter::project_title() } -> std::convertible_to<std::string_view>;
        { constAdapter.create_document() } -> std::same_as<rasterizer::SceneDocument>;
        { adapter.reset() } -> std::same_as<rasterizer::SceneFrameSnapshot>;
        { adapter.step(frame) } -> std::same_as<rasterizer::SceneFrameSnapshot>;
    };

    class RasterizerProjectRuntime {
    public:
        RasterizerProjectRuntime()                                                     = default;
        RasterizerProjectRuntime(const RasterizerProjectRuntime& other)                 = delete;
        RasterizerProjectRuntime(RasterizerProjectRuntime&& other) noexcept             = default;
        RasterizerProjectRuntime& operator=(const RasterizerProjectRuntime& other)      = delete;
        RasterizerProjectRuntime& operator=(RasterizerProjectRuntime&& other) noexcept  = default;
        virtual ~RasterizerProjectRuntime() noexcept                                   = default;

        [[nodiscard]] virtual std::string_view id() const                              = 0;
        [[nodiscard]] virtual std::string_view title() const                           = 0;
        [[nodiscard]] virtual rasterizer::SceneDocument create_document() const         = 0;
        [[nodiscard]] virtual rasterizer::SceneFrameSnapshot reset()                    = 0;
        [[nodiscard]] virtual rasterizer::SceneFrameSnapshot step(const RasterizerProjectFrameInfo& frame) = 0;
    };

    template <RasterizerProjectAdapter Adapter>
    class RasterizerProjectRuntimeModel final : public RasterizerProjectRuntime {
    public:
        RasterizerProjectRuntimeModel() = default;

        [[nodiscard]] std::string_view id() const override {
            return Adapter::project_id();
        }

        [[nodiscard]] std::string_view title() const override {
            return Adapter::project_title();
        }

        [[nodiscard]] rasterizer::SceneDocument create_document() const override {
            return this->adapter.create_document();
        }

        [[nodiscard]] rasterizer::SceneFrameSnapshot reset() override {
            return this->adapter.reset();
        }

        [[nodiscard]] rasterizer::SceneFrameSnapshot step(const RasterizerProjectFrameInfo& frame) override {
            return this->adapter.step(frame);
        }

    private:
        Adapter adapter{};
    };

    struct RasterizerProjectEntry {
        std::string id{};
        std::string title{};
        std::move_only_function<std::unique_ptr<RasterizerProjectRuntime>()> create_runtime{};
    };

    class RasterizerProjectRegistry final {
    public:
        template <RasterizerProjectAdapter Adapter>
        void register_project() {
            const std::string id{Adapter::project_id()};
            for (const RasterizerProjectEntry& entry : this->entries) {
                if (entry.id == id) throw std::runtime_error("Duplicate rasterizer project id: " + id);
            }
            this->entries.push_back(RasterizerProjectEntry{
                .id             = id,
                .title          = std::string{Adapter::project_title()},
                .create_runtime = [] { return std::make_unique<RasterizerProjectRuntimeModel<Adapter>>(); },
            });
        }

        [[nodiscard]] std::unique_ptr<RasterizerProjectRuntime> create_runtime(const std::size_t index) {
            if (this->entries.empty()) throw std::runtime_error("Rasterizer project registry is empty");
            if (index >= this->entries.size()) throw std::runtime_error("Rasterizer project index is out of range");
            return this->entries.at(index).create_runtime();
        }

        [[nodiscard]] const RasterizerProjectEntry& entry(const std::size_t index) const {
            if (index >= this->entries.size()) throw std::runtime_error("Rasterizer project index is out of range");
            return this->entries.at(index);
        }

        [[nodiscard]] std::size_t size() const {
            return this->entries.size();
        }

    private:
        std::vector<RasterizerProjectEntry> entries{};
    };

    [[nodiscard]] rasterizer::SceneVector3 ToRasterizerVector3(const std::array<float, 3>& value) {
        return rasterizer::SceneVector3{value[0], value[1], value[2]};
    }

    [[nodiscard]] rasterizer::SceneDocument MakeRasterizerProjectDocument(std::string name, std::string title, std::string source) {
        return rasterizer::SceneDocument{
            .revision        = rasterizer::SceneRevision{1},
            .name            = std::move(name),
            .title           = std::move(title),
            .source          = std::move(source),
            .framesPerSecond = 60.0,
        };
    }

    void CommitRasterizerFrame(rasterizer::SceneWorkspace& workspace, rasterizer::SceneFrameSnapshot frame, const bool resetTimeline) {
        rasterizer::SceneEditBuilder edit{};
        if (resetTimeline) {
            const std::shared_ptr<const rasterizer::SceneDocument> document = workspace.document();
            edit.replaceTimeline(rasterizer::SimulationTimeline{
                .mode            = rasterizer::SimulationTimelineMode::Live,
                .framesPerSecond = document->framesPerSecond,
                .playing         = true,
                .loop            = true,
                .cursor          = frame.cursor,
            });
        }
        edit.replaceFrame(std::move(frame));
        const rasterizer::SceneEditBatch batch = workspace.commit(std::move(edit));
        if (!rasterizer::HasSceneDirtyFlag(batch.dirty, rasterizer::SceneDirtyFlags::Frame)) throw std::runtime_error("Rasterizer project frame commit did not mark the frame dirty");
    }

    void CommitRasterizerTimelineAndFrame(rasterizer::SceneWorkspace& workspace, rasterizer::SimulationTimeline timeline, rasterizer::SceneFrameSnapshot frame) {
        timeline.cursor       = frame.cursor;
        timeline.currentFrame = frame;
        rasterizer::SceneEditBuilder edit{};
        edit.replaceTimeline(std::move(timeline));
        edit.replaceFrame(std::move(frame));
        const rasterizer::SceneEditBatch batch = workspace.commit(std::move(edit));
        if (!rasterizer::HasSceneDirtyFlag(batch.dirty, rasterizer::SceneDirtyFlags::Timeline)) throw std::runtime_error("Rasterizer project timeline commit did not mark the timeline dirty");
        if (!rasterizer::HasSceneDirtyFlag(batch.dirty, rasterizer::SceneDirtyFlags::Frame)) throw std::runtime_error("Rasterizer project frame commit did not mark the frame dirty");
    }

    void CommitRasterizerTimeline(rasterizer::SceneWorkspace& workspace, rasterizer::SimulationTimeline timeline) {
        rasterizer::SceneEditBuilder edit{};
        edit.replaceTimeline(std::move(timeline));
        const rasterizer::SceneEditBatch batch = workspace.commit(std::move(edit));
        if (!rasterizer::HasSceneDirtyFlag(batch.dirty, rasterizer::SceneDirtyFlags::Timeline)) throw std::runtime_error("Rasterizer project timeline commit did not mark the timeline dirty");
    }

    [[nodiscard]] rasterizer::FrameCursor MakeFrameCursor(const RasterizerProjectFrameInfo& info) {
        return rasterizer::FrameCursor{
            .frameIndex   = info.frame_index,
            .timeSeconds  = info.time_seconds,
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

        [[nodiscard]] rasterizer::SceneDocument create_document() const {
            rasterizer::SceneDocument document = MakeRasterizerProjectDocument("project.bouncing_ball", "Bouncing Ball", "project://bouncing_ball");
            document.materials.push_back(rasterizer::SceneMaterial{
                .name      = "ball",
                .baseColor = rasterizer::SceneVector4{0.95f, 0.28f, 0.18f, 1.0f},
                .roughness = 0.42f,
            });
            document.materials.push_back(rasterizer::SceneMaterial{
                .name      = "floor",
                .baseColor = rasterizer::SceneVector4{0.38f, 0.43f, 0.38f, 1.0f},
                .roughness = 0.74f,
            });
            document.lights.push_back(rasterizer::SceneLight{
                .name      = "key",
                .kind      = rasterizer::SceneLightKind::Directional,
                .transform = rasterizer::SceneTransform{.rotation = rasterizer::SceneQuaternion{0.35f, 0.0f, 0.0f, 0.94f}},
                .color     = rasterizer::SceneVector3{1.0f, 0.97f, 0.92f},
                .intensity = 3.0f,
            });
            document.camera = rasterizer::SceneCamera{
                .name      = "camera.main",
                .transform = rasterizer::SceneTransform{.position = rasterizer::SceneVector3{4.4f, 2.7f, 5.8f}},
                .target    = rasterizer::SceneVector3{0.0f, 1.25f, 0.0f},
                .verticalFovDegrees = 42.0f,
                .nearPlane = 0.05f,
                .farPlane  = 80.0f,
            };
            document.meshes.push_back(this->make_floor_mesh());
            document.meshes.push_back(this->make_mesh());
            document.rigidBodies.push_back(rasterizer::SceneRigidBody{
                .name         = "ball.body",
                .meshName     = "ball.mesh",
                .materialName = "ball",
                .transform    = rasterizer::SceneTransform{.position = ToRasterizerVector3(this->solver.current_position())},
                .mass         = 1.0f,
            });
            return document;
        }

        [[nodiscard]] rasterizer::SceneFrameSnapshot reset() {
            this->solver.reset();
            return this->make_frame(RasterizerProjectFrameInfo{.delta_seconds = 0.0, .time_seconds = 0.0, .frame_index = 0u});
        }

        [[nodiscard]] rasterizer::SceneFrameSnapshot step(const RasterizerProjectFrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            return this->make_frame(frame);
        }

    private:
        xayah::projects::bouncing_ball::BouncingBallSolver solver{};

        [[nodiscard]] rasterizer::SceneMesh make_floor_mesh() const {
            return rasterizer::SceneMesh{
                .name         = "floor.mesh",
                .positions    = {
                    rasterizer::SceneVector3{-5.5f, 0.0f, -5.5f},
                    rasterizer::SceneVector3{5.5f, 0.0f, -5.5f},
                    rasterizer::SceneVector3{5.5f, 0.0f, 5.5f},
                    rasterizer::SceneVector3{-5.5f, 0.0f, 5.5f},
                },
                .normals      = {
                    rasterizer::SceneVector3{0.0f, 1.0f, 0.0f},
                    rasterizer::SceneVector3{0.0f, 1.0f, 0.0f},
                    rasterizer::SceneVector3{0.0f, 1.0f, 0.0f},
                    rasterizer::SceneVector3{0.0f, 1.0f, 0.0f},
                },
                .indices      = {0u, 1u, 2u, 0u, 2u, 3u},
                .materialName = "floor",
                .dynamic      = false,
            };
        }

        [[nodiscard]] rasterizer::SceneMesh make_mesh() const {
            rasterizer::SceneMesh mesh{
                .name         = "ball.mesh",
                .materialName = "ball",
                .transform    = rasterizer::SceneTransform{.position = ToRasterizerVector3(this->solver.current_position())},
                .dynamic      = true,
            };
            const std::vector<xayah::projects::bouncing_ball::BouncingBallVertex>& vertices = this->solver.mesh_vertices();
            mesh.positions.reserve(vertices.size());
            mesh.normals.reserve(vertices.size());
            for (const xayah::projects::bouncing_ball::BouncingBallVertex& vertex : vertices) {
                mesh.positions.push_back(ToRasterizerVector3(vertex.position));
                mesh.normals.push_back(ToRasterizerVector3(vertex.normal));
            }
            mesh.indices = this->solver.mesh_indices();
            return mesh;
        }

        [[nodiscard]] rasterizer::SceneFrameSnapshot make_frame(const RasterizerProjectFrameInfo& frame) const {
            rasterizer::SceneFrameSnapshot snapshot{
                .cursor = MakeFrameCursor(frame),
            };
            snapshot.meshes.push_back(this->make_mesh());
            snapshot.rigidBodies.push_back(rasterizer::SceneRigidBody{
                .name         = "ball.body",
                .meshName     = "ball.mesh",
                .materialName = "ball",
                .transform    = rasterizer::SceneTransform{.position = ToRasterizerVector3(this->solver.current_position())},
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

        [[nodiscard]] rasterizer::SceneDocument create_document() const {
            rasterizer::SceneDocument document = MakeRasterizerProjectDocument("project.sparkles", "Sparkles", "project://sparkles");
            document.materials.push_back(rasterizer::SceneMaterial{
                .name             = "sparkle",
                .baseColor        = rasterizer::SceneVector4{1.0f, 0.78f, 0.24f, 1.0f},
                .emissionColor    = rasterizer::SceneVector3{1.0f, 0.54f, 0.12f},
                .emissionStrength = 2.4f,
                .roughness        = 0.2f,
            });
            document.lights.push_back(rasterizer::SceneLight{
                .name      = "environment",
                .kind      = rasterizer::SceneLightKind::Environment,
                .color     = rasterizer::SceneVector3{0.22f, 0.26f, 0.34f},
                .intensity = 0.8f,
            });
            document.camera = rasterizer::SceneCamera{
                .name      = "camera.main",
                .transform = rasterizer::SceneTransform{.position = rasterizer::SceneVector3{4.6f, 3.1f, 6.0f}},
                .target    = rasterizer::SceneVector3{0.0f, 1.9f, 0.0f},
                .verticalFovDegrees = 42.0f,
                .nearPlane = 0.05f,
                .farPlane  = 90.0f,
            };
            document.particleSets.push_back(this->make_particles());
            return document;
        }

        [[nodiscard]] rasterizer::SceneFrameSnapshot reset() {
            this->solver.reset();
            return this->make_frame(RasterizerProjectFrameInfo{.delta_seconds = 0.0, .time_seconds = 0.0, .frame_index = 0u});
        }

        [[nodiscard]] rasterizer::SceneFrameSnapshot step(const RasterizerProjectFrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            return this->make_frame(frame);
        }

    private:
        xayah::projects::sparkles::SparklesSolver solver{};

        [[nodiscard]] rasterizer::SceneParticleSet make_particles() const {
            rasterizer::SceneParticleSet particles{
                .name         = "sparkles.particles",
                .materialName = "sparkle",
                .dynamic      = true,
            };
            const std::span<const xayah::projects::sparkles::SparklesParticle> visibleParticles = this->solver.particles();
            particles.positions.reserve(visibleParticles.size());
            particles.radii.reserve(visibleParticles.size());
            particles.colors.reserve(visibleParticles.size());
            for (const xayah::projects::sparkles::SparklesParticle& particle : visibleParticles) {
                particles.positions.push_back(ToRasterizerVector3(particle.position));
                particles.radii.push_back(particle.radius);
                particles.colors.push_back(rasterizer::SceneVector4{particle.color[0], particle.color[1], particle.color[2], 1.0f});
            }
            return particles;
        }

        [[nodiscard]] rasterizer::SceneFrameSnapshot make_frame(const RasterizerProjectFrameInfo& frame) const {
            rasterizer::SceneFrameSnapshot snapshot{
                .cursor = MakeFrameCursor(frame),
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

        [[nodiscard]] rasterizer::SceneDocument create_document() const {
            rasterizer::SceneDocument document = MakeRasterizerProjectDocument("project.cloth", "Cloth", "project://cloth");
            document.materials.push_back(rasterizer::SceneMaterial{
                .name      = "cloth",
                .baseColor = rasterizer::SceneVector4{0.18f, 0.42f, 0.88f, 1.0f},
                .roughness = 0.64f,
            });
            document.materials.push_back(rasterizer::SceneMaterial{
                .name      = "cloth.floor",
                .baseColor = rasterizer::SceneVector4{0.34f, 0.38f, 0.36f, 1.0f},
                .roughness = 0.78f,
            });
            document.materials.push_back(rasterizer::SceneMaterial{
                .name      = "cloth.collider",
                .baseColor = rasterizer::SceneVector4{0.90f, 0.44f, 0.28f, 1.0f},
                .roughness = 0.46f,
            });
            document.lights.push_back(rasterizer::SceneLight{
                .name      = "key",
                .kind      = rasterizer::SceneLightKind::Directional,
                .transform = rasterizer::SceneTransform{.rotation = rasterizer::SceneQuaternion{0.45f, 0.18f, 0.0f, 0.87f}},
                .color     = rasterizer::SceneVector3{0.92f, 0.96f, 1.0f},
                .intensity = 3.6f,
            });
            document.camera = rasterizer::SceneCamera{
                .name      = "camera.main",
                .transform = rasterizer::SceneTransform{.position = rasterizer::SceneVector3{3.8f, 2.8f, 5.2f}},
                .target    = rasterizer::SceneVector3{0.0f, 1.05f, 0.0f},
                .verticalFovDegrees = 42.0f,
                .nearPlane = 0.05f,
                .farPlane  = 90.0f,
            };
            document.meshes.push_back(this->make_floor_mesh());
            document.meshes.push_back(this->make_collider_mesh());
            document.meshes.push_back(this->make_mesh());
            document.cloths.push_back(rasterizer::SceneCloth{
                .name         = "cloth.body",
                .meshName     = "cloth.mesh",
                .materialName = "cloth",
                .dynamic      = true,
            });
            document.colliders.push_back(rasterizer::SceneCollider{
                .name      = "cloth.collider",
                .meshName  = "cloth.collider.mesh",
                .transform = rasterizer::SceneTransform{.position = ToRasterizerVector3(this->collider.center)},
                .friction  = 0.54f,
            });
            return document;
        }

        [[nodiscard]] rasterizer::SceneFrameSnapshot reset() {
            this->solver.reset();
            return this->make_frame(RasterizerProjectFrameInfo{.delta_seconds = 0.0, .time_seconds = 0.0, .frame_index = 0u});
        }

        [[nodiscard]] rasterizer::SceneFrameSnapshot step(const RasterizerProjectFrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            return this->make_frame(frame);
        }

    private:
        xayah::projects::cloth::ClothConfig config{};
        xayah::projects::cloth::ClothSphereCollider collider{};
        xayah::projects::cloth::ClothSolver solver;

        [[nodiscard]] rasterizer::SceneMesh make_floor_mesh() const {
            return rasterizer::SceneMesh{
                .name         = "cloth.floor.mesh",
                .positions    = {
                    rasterizer::SceneVector3{-3.8f, -0.02f, -3.2f},
                    rasterizer::SceneVector3{3.8f, -0.02f, -3.2f},
                    rasterizer::SceneVector3{3.8f, -0.02f, 3.2f},
                    rasterizer::SceneVector3{-3.8f, -0.02f, 3.2f},
                },
                .normals      = {
                    rasterizer::SceneVector3{0.0f, 1.0f, 0.0f},
                    rasterizer::SceneVector3{0.0f, 1.0f, 0.0f},
                    rasterizer::SceneVector3{0.0f, 1.0f, 0.0f},
                    rasterizer::SceneVector3{0.0f, 1.0f, 0.0f},
                },
                .indices      = {0u, 1u, 2u, 0u, 2u, 3u},
                .materialName = "cloth.floor",
                .dynamic      = false,
            };
        }

        [[nodiscard]] rasterizer::SceneMesh make_collider_mesh() const {
            rasterizer::SceneMesh mesh{
                .name         = "cloth.collider.mesh",
                .materialName = "cloth.collider",
                .transform    = rasterizer::SceneTransform{.position = ToRasterizerVector3(this->collider.center)},
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
                    const rasterizer::SceneVector3 normal{ring * std::cos(phi), y, ring * std::sin(phi)};
                    mesh.positions.push_back(rasterizer::SceneVector3{normal.x * this->collider.radius, normal.y * this->collider.radius, normal.z * this->collider.radius});
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

        [[nodiscard]] rasterizer::SceneMesh make_mesh() const {
            rasterizer::SceneMesh mesh{
                .name         = "cloth.mesh",
                .materialName = "cloth",
                .dynamic      = true,
            };
            const std::vector<xayah::projects::cloth::ClothVertex>& vertices = this->solver.mesh_vertices();
            mesh.positions.reserve(vertices.size());
            mesh.normals.reserve(vertices.size());
            for (const xayah::projects::cloth::ClothVertex& vertex : vertices) {
                mesh.positions.push_back(ToRasterizerVector3(vertex.position));
                mesh.normals.push_back(ToRasterizerVector3(vertex.normal));
            }
            mesh.indices = this->solver.mesh_indices();
            return mesh;
        }

        [[nodiscard]] rasterizer::SceneFrameSnapshot make_frame(const RasterizerProjectFrameInfo& frame) const {
            rasterizer::SceneFrameSnapshot snapshot{
                .cursor = MakeFrameCursor(frame),
            };
            snapshot.meshes.push_back(this->make_mesh());
            snapshot.cloths.push_back(rasterizer::SceneCloth{
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

        [[nodiscard]] rasterizer::SceneDocument create_document() const {
            rasterizer::SceneDocument document = MakeRasterizerProjectDocument("project.pyro", "Pyro", "project://pyro");
            document.materials.push_back(rasterizer::SceneMaterial{
                .name             = "smoke",
                .baseColor        = rasterizer::SceneVector4{0.58f, 0.62f, 0.68f, 0.72f},
                .emissionColor    = rasterizer::SceneVector3{0.95f, 0.48f, 0.18f},
                .emissionStrength = 0.3f,
                .roughness        = 0.9f,
            });
            document.lights.push_back(rasterizer::SceneLight{
                .name      = "environment",
                .kind      = rasterizer::SceneLightKind::Environment,
                .color     = rasterizer::SceneVector3{0.24f, 0.26f, 0.30f},
                .intensity = 0.7f,
            });
            document.camera = rasterizer::SceneCamera{
                .name      = "camera.main",
                .transform = rasterizer::SceneTransform{.position = rasterizer::SceneVector3{2.35f, 1.65f, 2.8f}},
                .target    = rasterizer::SceneVector3{0.6f, 0.86f, 0.6f},
                .verticalFovDegrees = 39.0f,
                .nearPlane = 0.03f,
                .farPlane  = 80.0f,
            };
            document.volumes.push_back(this->make_volume_metadata(0u));
            return document;
        }

        [[nodiscard]] rasterizer::SceneFrameSnapshot reset() {
            this->solver.reset();
            return this->make_frame(RasterizerProjectFrameInfo{.delta_seconds = 0.0, .time_seconds = 0.0, .frame_index = 0u});
        }

        [[nodiscard]] rasterizer::SceneFrameSnapshot step(const RasterizerProjectFrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            return this->make_frame(frame);
        }

    private:
        xayah::projects::pyro::PyroSolver solver{};
        std::array<std::uint32_t, 3> resolution{64u, 96u, 64u};
        float cell_size{0.01875f};

        [[nodiscard]] rasterizer::SceneVolumeChannel make_volume_channel(std::string name, const rasterizer::SceneVolumeChannelLayout layout, const std::array<std::uint32_t, 3> dimensions, std::vector<float> values) const {
            return rasterizer::SceneVolumeChannel{
                .name       = std::move(name),
                .layout     = layout,
                .dimensions = dimensions,
                .values     = std::move(values),
            };
        }

        [[nodiscard]] rasterizer::SceneVolumeGrid make_volume_metadata(const std::uint64_t) const {
            return rasterizer::SceneVolumeGrid{
                .name         = "pyro.volume",
                .kind         = rasterizer::SceneVolumeKind::GasDensity,
                .dimensions   = this->resolution,
                .origin       = rasterizer::SceneVector3{0.0f, 0.0f, 0.0f},
                .voxelSize    = rasterizer::SceneVector3{this->cell_size, this->cell_size, this->cell_size},
                .channelNames = {"density", "temperature", "velocity_x", "velocity_y", "velocity_z"},
                .materialName = "smoke",
                .dynamic      = true,
            };
        }

        [[nodiscard]] rasterizer::SceneVolumeGrid make_volume(const xayah::projects::pyro::PyroFrame& frame) const {
            rasterizer::SceneVolumeGrid volume = this->make_volume_metadata(static_cast<std::uint64_t>(frame.frame_index));
            volume.dimensions = frame.resolution;
            volume.voxelSize = rasterizer::SceneVector3{frame.cell_size, frame.cell_size, frame.cell_size};
            volume.channels.push_back(this->make_volume_channel("density", rasterizer::SceneVolumeChannelLayout::CellCentered, frame.resolution, frame.density));
            volume.channels.push_back(this->make_volume_channel("temperature", rasterizer::SceneVolumeChannelLayout::CellCentered, frame.resolution, frame.temperature));
            volume.channels.push_back(this->make_volume_channel("velocity_x", rasterizer::SceneVolumeChannelLayout::FaceX, std::array<std::uint32_t, 3>{frame.resolution[0] + 1u, frame.resolution[1], frame.resolution[2]}, frame.velocity_x));
            volume.channels.push_back(this->make_volume_channel("velocity_y", rasterizer::SceneVolumeChannelLayout::FaceY, std::array<std::uint32_t, 3>{frame.resolution[0], frame.resolution[1] + 1u, frame.resolution[2]}, frame.velocity_y));
            volume.channels.push_back(this->make_volume_channel("velocity_z", rasterizer::SceneVolumeChannelLayout::FaceZ, std::array<std::uint32_t, 3>{frame.resolution[0], frame.resolution[1], frame.resolution[2] + 1u}, frame.velocity_z));
            return volume;
        }

        [[nodiscard]] rasterizer::SceneFrameSnapshot make_frame(const RasterizerProjectFrameInfo& frame) {
            const xayah::projects::pyro::PyroFrame pyroFrame = this->solver.read_frame(ToPyroFrameIndex(frame.frame_index));
            this->resolution                                   = pyroFrame.resolution;
            this->cell_size                                    = pyroFrame.cell_size;
            rasterizer::SceneFrameSnapshot snapshot{
                .cursor = MakeFrameCursor(frame),
            };
            snapshot.volumes.push_back(this->make_volume(pyroFrame));
            return snapshot;
        }
    };

    static_assert(RasterizerProjectAdapter<BouncingBallRasterizerProject>);
    static_assert(RasterizerProjectAdapter<SparklesRasterizerProject>);
    static_assert(RasterizerProjectAdapter<ClothRasterizerProject>);
    static_assert(RasterizerProjectAdapter<PyroRasterizerProject>);

    [[nodiscard]] RasterizerProjectRegistry MakeRasterizerProjectRegistry() {
        RasterizerProjectRegistry registry{};
        registry.register_project<BouncingBallRasterizerProject>();
        registry.register_project<SparklesRasterizerProject>();
        registry.register_project<ClothRasterizerProject>();
        registry.register_project<PyroRasterizerProject>();
        return registry;
    }

    struct RasterizerProjectSlot {
        std::unique_ptr<RasterizerProjectRuntime> runtime{};
        std::shared_ptr<rasterizer::SceneWorkspace> workspace{};
        double simulation_accumulator_seconds{};
        double simulation_time_seconds{};
        std::uint64_t simulation_frame_index{};
        std::uint64_t observed_reset_request_serial{};
        std::uint64_t observed_clear_recording_request_serial{};
        std::optional<std::uint64_t> committed_playback_frame_index{};
    };

    class RasterizerProjectManager final {
    public:
        explicit RasterizerProjectManager(RasterizerProjectRegistry registry) : registry(std::move(registry)) {
            if (this->registry.size() == 0u) throw std::runtime_error("Rasterizer project manager requires at least one project");
            this->slots.resize(this->registry.size());
            this->ensure_slot(0u);
        }

        RasterizerProjectManager(const RasterizerProjectManager& other)                = delete;
        RasterizerProjectManager(RasterizerProjectManager&& other) noexcept            = default;
        RasterizerProjectManager& operator=(const RasterizerProjectManager& other)     = delete;
        RasterizerProjectManager& operator=(RasterizerProjectManager&& other) noexcept = default;
        ~RasterizerProjectManager() noexcept                                           = default;

        [[nodiscard]] std::shared_ptr<rasterizer::SceneWorkspace> active_workspace() {
            return this->ensure_slot(this->active_index).workspace;
        }

        void apply_pending_workspace(rasterizer::RasterizerRenderer& renderer) {
            if (!this->pending_active_index.has_value()) return;
            const std::size_t next_index = *this->pending_active_index;
            this->pending_active_index.reset();
            if (next_index >= this->slots.size()) throw std::runtime_error("Pending rasterizer project index is out of range");
            if (next_index == this->active_index) return;
            this->active_index = next_index;
            renderer.set_scene_workspace(this->active_workspace());
        }

        void draw_control_panel() {
            const std::size_t selected_index = this->pending_active_index.value_or(this->active_index);
            const RasterizerProjectEntry& selected_entry = this->registry.entry(selected_index);
            ImGui::SeparatorText("Project");
            if (ImGui::BeginCombo("Simulation", selected_entry.title.c_str())) {
                for (std::size_t index = 0; index < this->registry.size(); ++index) {
                    const RasterizerProjectEntry& entry = this->registry.entry(index);
                    const bool selected = index == selected_index;
                    if (ImGui::Selectable(entry.title.c_str(), selected)) {
                        if (index != this->active_index) this->pending_active_index = index;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("%s", selected_entry.id.c_str());
            if (this->pending_active_index.has_value() && *this->pending_active_index != this->active_index) ImGui::TextDisabled("Switching on next frame");
        }

        void drive_simulation(const SpectraFrameInfo& frame) {
            RasterizerProjectSlot& slot = this->ensure_slot(this->active_index);
            const std::shared_ptr<const rasterizer::SceneDocument> document = slot.workspace->document();
            if (document->framesPerSecond <= 0.0) throw std::runtime_error("Rasterizer project scene frame rate must be positive");
            const double fixed_delta_seconds = 1.0 / document->framesPerSecond;
            rasterizer::SimulationTimeline timeline = slot.workspace->timeline();
            if (timeline.framesPerSecond <= 0.0) throw std::runtime_error("Rasterizer project timeline frame rate must be positive");
            if (timeline.resetRequestSerial != slot.observed_reset_request_serial) {
                this->reset_project(slot, std::move(timeline));
                slot.observed_reset_request_serial = slot.workspace->timeline().resetRequestSerial;
                slot.committed_playback_frame_index.reset();
                return;
            }
            if (timeline.clearRecordingRequestSerial != slot.observed_clear_recording_request_serial) {
                timeline.recordedFrames.clear();
                timeline.selectedFrameIndex = 0;
                CommitRasterizerTimeline(*slot.workspace, std::move(timeline));
                slot.observed_clear_recording_request_serial = slot.workspace->timeline().clearRecordingRequestSerial;
                slot.committed_playback_frame_index.reset();
                return;
            }
            if (timeline.mode == rasterizer::SimulationTimelineMode::Playback) {
                if (timeline.recordedFrames.empty()) return;
                if (timeline.selectedFrameIndex >= timeline.recordedFrames.size()) throw std::runtime_error("Rasterizer playback selected frame is out of range");
                if (slot.committed_playback_frame_index.has_value() && *slot.committed_playback_frame_index == timeline.selectedFrameIndex) return;
                rasterizer::SceneFrameSnapshot selected_frame = timeline.recordedFrames.at(timeline.selectedFrameIndex);
                CommitRasterizerFrame(*slot.workspace, std::move(selected_frame), false);
                slot.committed_playback_frame_index = timeline.selectedFrameIndex;
                return;
            }
            slot.committed_playback_frame_index.reset();
            if (!timeline.playing) return;
            if (!std::isfinite(frame.delta_seconds) || frame.delta_seconds < 0.0) throw std::runtime_error("Rasterizer frame delta time is invalid");
            slot.simulation_accumulator_seconds += frame.delta_seconds;
            bool advanced = false;
            rasterizer::SceneFrameSnapshot snapshot{};
            while (slot.simulation_accumulator_seconds >= fixed_delta_seconds) {
                slot.simulation_accumulator_seconds -= fixed_delta_seconds;
                ++slot.simulation_frame_index;
                slot.simulation_time_seconds += fixed_delta_seconds;
                snapshot = slot.runtime->step(RasterizerProjectFrameInfo{
                    .delta_seconds = fixed_delta_seconds,
                    .time_seconds  = slot.simulation_time_seconds,
                    .frame_index   = slot.simulation_frame_index,
                });
                advanced = true;
            }
            if (!advanced) return;
            if (timeline.mode == rasterizer::SimulationTimelineMode::Record) {
                timeline.recordedFrames.push_back(snapshot);
                timeline.selectedFrameIndex = timeline.recordedFrames.size() - 1u;
                CommitRasterizerTimelineAndFrame(*slot.workspace, std::move(timeline), std::move(snapshot));
                return;
            }
            CommitRasterizerFrame(*slot.workspace, std::move(snapshot), false);
        }

    private:
        RasterizerProjectRegistry registry{};
        std::vector<RasterizerProjectSlot> slots{};
        std::size_t active_index{};
        std::optional<std::size_t> pending_active_index{};

        RasterizerProjectSlot& ensure_slot(const std::size_t index) {
            if (index >= this->slots.size()) throw std::runtime_error("Rasterizer project slot index is out of range");
            RasterizerProjectSlot& slot = this->slots.at(index);
            if (slot.runtime != nullptr && slot.workspace != nullptr) return slot;
            slot.runtime = this->registry.create_runtime(index);
            rasterizer::SceneDocument document = slot.runtime->create_document();
            slot.workspace = std::make_shared<rasterizer::SceneWorkspace>(std::move(document));
            rasterizer::SceneFrameSnapshot snapshot = slot.runtime->reset();
            const std::shared_ptr<const rasterizer::SceneDocument> scene = slot.workspace->document();
            rasterizer::SimulationTimeline timeline{
                .mode            = rasterizer::SimulationTimelineMode::Live,
                .framesPerSecond = scene->framesPerSecond,
                .playing         = true,
                .loop            = true,
                .cursor          = snapshot.cursor,
                .selectedFrameIndex = 0,
                .currentFrame    = snapshot,
            };
            CommitRasterizerTimelineAndFrame(*slot.workspace, std::move(timeline), std::move(snapshot));
            return slot;
        }

        void reset_project(RasterizerProjectSlot& slot, rasterizer::SimulationTimeline timeline) {
            slot.simulation_accumulator_seconds = 0.0;
            slot.simulation_time_seconds        = 0.0;
            slot.simulation_frame_index         = 0;
            rasterizer::SceneFrameSnapshot snapshot = slot.runtime->reset();
            timeline.cursor                         = snapshot.cursor;
            timeline.currentFrame                   = snapshot;
            timeline.selectedFrameIndex             = 0;
            CommitRasterizerTimelineAndFrame(*slot.workspace, std::move(timeline), std::move(snapshot));
        }
    };

    [[nodiscard]] SpectraPanel ToSpectraPanel(rasterizer::RasterizerPanel panel) {
        return SpectraPanel{
            .id                  = std::move(panel.id),
            .title               = std::move(panel.title),
            .icon                = std::move(panel.icon),
            .owner_renderer      = std::string{rasterizer::RasterizerRenderer::target_name()},
            .shortcut_label      = std::move(panel.shortcut_label),
            .shortcut_key        = panel.shortcut_key,
            .dock_slot           = ToSpectraDockSlot(panel.dock_slot),
            .window_flags        = panel.window_flags,
            .visible             = panel.visible,
            .closable            = panel.closable,
            .zero_window_padding = panel.zero_window_padding,
            .draw                = std::move(panel.draw),
        };
    }

    [[nodiscard]] SpectraSidebarTab ToSpectraSidebarTab(rasterizer::RasterizerSidebarTab tab) {
        return SpectraSidebarTab{
            .id             = std::move(tab.id),
            .title          = std::move(tab.title),
            .icon           = std::move(tab.icon),
            .owner_renderer = std::string{rasterizer::RasterizerRenderer::target_name()},
            .shortcut_label = std::move(tab.shortcut_label),
            .shortcut_key   = tab.shortcut_key,
            .draw           = std::move(tab.draw),
        };
    }

    class PathtracerSpectraHost final {
    public:
        explicit PathtracerSpectraHost(Spectra& host) : host(&host) {}

        [[nodiscard]] const vk::raii::PhysicalDevice& physical_device() const {
            return this->host->physical_device();
        }

        [[nodiscard]] const vk::raii::Device& device() const {
            return this->host->device();
        }

        [[nodiscard]] std::uint32_t frame_count() const {
            return this->host->frame_count();
        }

        [[nodiscard]] vk::Extent2D swapchain_extent() const {
            return this->host->swapchain_extent();
        }

        void register_panel(pathtracer::PathtracerPanel panel) const {
            this->host->register_panel(ToSpectraPanel(std::move(panel)));
        }

        void register_sidebar_tab(pathtracer::PathtracerSidebarTab tab) const {
            this->host->register_sidebar_tab(ToSpectraSidebarTab(std::move(tab)));
        }

        void register_toolbar_action(pathtracer::PathtracerToolbarAction action) const {
            this->host->register_toolbar_action(ToSpectraToolbarAction(std::move(action)));
        }

        void set_window_detail(std::string detail) const {
            this->host->set_window_detail(std::move(detail));
        }

    private:
        Spectra* host{};
    };

    class RasterizerSpectraHost final {
    public:
        explicit RasterizerSpectraHost(Spectra& host) : host(&host) {}

        [[nodiscard]] const vk::raii::PhysicalDevice& physical_device() const {
            return this->host->physical_device();
        }

        [[nodiscard]] const vk::raii::Device& device() const {
            return this->host->device();
        }

        [[nodiscard]] std::uint32_t frame_count() const {
            return this->host->frame_count();
        }

        [[nodiscard]] vk::Extent2D swapchain_extent() const {
            return this->host->swapchain_extent();
        }

        void register_panel(rasterizer::RasterizerPanel panel) const {
            this->host->register_panel(ToSpectraPanel(std::move(panel)));
        }

        void register_sidebar_tab(rasterizer::RasterizerSidebarTab tab) const {
            this->host->register_sidebar_tab(ToSpectraSidebarTab(std::move(tab)));
        }

        void set_window_detail(std::string detail) const {
            this->host->set_window_detail(std::move(detail));
        }

    private:
        Spectra* host{};
    };

    class PathtracerSpectraRenderer final {
    public:
        explicit PathtracerSpectraRenderer(std::shared_ptr<pathtracer::PbrtSceneLibrary> sceneLibrary) : scene_library(std::move(sceneLibrary)) {
            if (this->scene_library == nullptr) throw std::runtime_error("Pathtracer adapter requires a PBRT scene library");
            this->renderer = std::make_unique<pathtracer::PathtracerRenderer>(this->scene_library->scene_workspace());
        }

        PathtracerSpectraRenderer(const PathtracerSpectraRenderer& other)                = delete;
        PathtracerSpectraRenderer(PathtracerSpectraRenderer&& other) noexcept            = default;
        PathtracerSpectraRenderer& operator=(const PathtracerSpectraRenderer& other)     = delete;
        PathtracerSpectraRenderer& operator=(PathtracerSpectraRenderer&& other) noexcept = default;
        ~PathtracerSpectraRenderer() noexcept                                            = default;

        [[nodiscard]] std::string_view name() const {
            return pathtracer::PathtracerRenderer::target_name();
        }

        void attach(Spectra& host) {
            PathtracerSpectraHost pathtracerHost{host};
            this->scene_library->attach(pathtracerHost);
            this->renderer->attach(pathtracerHost);
        }

        void detach(Spectra& host) noexcept {
            PathtracerSpectraHost pathtracerHost{host};
            this->renderer->detach(pathtracerHost);
            this->scene_library->detach();
        }

        void before_imgui_shutdown(Spectra& host) noexcept {
            PathtracerSpectraHost pathtracerHost{host};
            this->renderer->before_imgui_shutdown(pathtracerHost);
        }

        void after_imgui_created(Spectra& host) {
            PathtracerSpectraHost pathtracerHost{host};
            this->renderer->after_imgui_created(pathtracerHost);
        }

        [[nodiscard]] SpectraFrameResult begin_frame(Spectra& host, const SpectraFrameInfo& frame) {
            PathtracerSpectraHost pathtracerHost{host};
            pathtracer::PathtracerFrameResult result = this->renderer->begin_frame(pathtracerHost, pathtracer::PathtracerFrameInfo{
                                                                                                        .frame_index = frame.frame_index,
                                                                                                        .image_index = frame.image_index,
                                                                                                    });
            return SpectraFrameResult{
                .completion_semaphore = std::move(result.completion_semaphore),
                .close_requested      = result.close_requested,
                .window_detail        = std::move(result.window_detail),
            };
        }

        void record_frame(const vk::raii::CommandBuffer& commandBuffer) {
            this->renderer->record_frame(commandBuffer);
        }

    private:
        std::shared_ptr<pathtracer::PbrtSceneLibrary> scene_library{};
        std::unique_ptr<pathtracer::PathtracerRenderer> renderer{};
    };

    class RasterizerSpectraRenderer final {
    public:
        explicit RasterizerSpectraRenderer(RasterizerProjectRegistry registry) : project_manager(std::make_shared<RasterizerProjectManager>(std::move(registry))), renderer(std::make_unique<rasterizer::RasterizerRenderer>(this->project_manager->active_workspace())) {
            if (this->project_manager == nullptr) throw std::runtime_error("Rasterizer adapter requires a project manager");
            this->renderer->set_control_panel_extension([projectManager = this->project_manager] { projectManager->draw_control_panel(); });
        }

        RasterizerSpectraRenderer(const RasterizerSpectraRenderer& other)                = delete;
        RasterizerSpectraRenderer(RasterizerSpectraRenderer&& other) noexcept            = default;
        RasterizerSpectraRenderer& operator=(const RasterizerSpectraRenderer& other)     = delete;
        RasterizerSpectraRenderer& operator=(RasterizerSpectraRenderer&& other) noexcept = default;
        ~RasterizerSpectraRenderer() noexcept                                            = default;

        [[nodiscard]] std::string_view name() const {
            return rasterizer::RasterizerRenderer::target_name();
        }

        void attach(Spectra& host) {
            RasterizerSpectraHost rasterizerHost{host};
            this->renderer->attach(rasterizerHost);
        }

        void detach(Spectra& host) noexcept {
            RasterizerSpectraHost rasterizerHost{host};
            this->renderer->detach(rasterizerHost);
        }

        void before_imgui_shutdown(Spectra& host) noexcept {
            RasterizerSpectraHost rasterizerHost{host};
            this->renderer->before_imgui_shutdown(rasterizerHost);
        }

        void after_imgui_created(Spectra& host) {
            RasterizerSpectraHost rasterizerHost{host};
            this->renderer->after_imgui_created(rasterizerHost);
        }

        [[nodiscard]] SpectraFrameResult begin_frame(Spectra& host, const SpectraFrameInfo& frame) {
            RasterizerSpectraHost rasterizerHost{host};
            this->project_manager->apply_pending_workspace(*this->renderer);
            this->project_manager->drive_simulation(frame);
            rasterizer::RasterizerFrameResult result = this->renderer->begin_frame(rasterizerHost, rasterizer::RasterizerFrameInfo{
                                                                                                      .frame_index = frame.frame_index,
                                                                                                      .image_index = frame.image_index,
                                                                                                      .frame_number = frame.frame_number,
                                                                                                      .delta_seconds = frame.delta_seconds,
                                                                                                  });
            return SpectraFrameResult{
                .completion_semaphore = std::move(result.completion_semaphore),
                .close_requested      = result.close_requested,
                .window_detail        = std::move(result.window_detail),
            };
        }

        void record_frame(const vk::raii::CommandBuffer& commandBuffer) {
            this->renderer->record_frame(commandBuffer);
        }

    private:
        std::shared_ptr<RasterizerProjectManager> project_manager{};
        std::unique_ptr<rasterizer::RasterizerRenderer> renderer{};
    };

    static_assert(SpectraRendererForHost<PathtracerSpectraRenderer, Spectra>);
    static_assert(SpectraRendererForHost<RasterizerSpectraRenderer, Spectra>);

    void RegisterRenderers(Spectra& app, std::shared_ptr<pathtracer::PbrtSceneLibrary> pbrtSceneLibrary, RasterizerProjectRegistry rasterizerProjectRegistry) {
        app.register_renderer(RasterizerSpectraRenderer{std::move(rasterizerProjectRegistry)});
        app.register_renderer(PathtracerSpectraRenderer{std::move(pbrtSceneLibrary)});
    }
} // namespace spectra::app

int main(const int argc, char**) {
    try {
        if (argc != 1) throw std::runtime_error("usage: spectra_gui");

        std::shared_ptr<spectra::pathtracer::PbrtSceneLibrary> pbrt_scene_library = std::make_shared<spectra::pathtracer::PbrtSceneLibrary>();
        spectra::app::RasterizerProjectRegistry rasterizer_project_registry = spectra::app::MakeRasterizerProjectRegistry();

        spectra::Spectra app{"Spectra"};
        spectra::app::RegisterRenderers(app, std::move(pbrt_scene_library), std::move(rasterizer_project_registry));
        app.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
