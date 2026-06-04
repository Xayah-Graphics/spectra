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
        case pathtracer::PathtracerDockSlot::Left: return SpectraDockSlot::Left;
        case pathtracer::PathtracerDockSlot::LeftBottom: return SpectraDockSlot::LeftBottom;
        case pathtracer::PathtracerDockSlot::Right: return SpectraDockSlot::Right;
        case pathtracer::PathtracerDockSlot::RightBottom: return SpectraDockSlot::RightBottom;
        case pathtracer::PathtracerDockSlot::Bottom: return SpectraDockSlot::Bottom;
        case pathtracer::PathtracerDockSlot::Floating: return SpectraDockSlot::Floating;
        }
        throw std::runtime_error("Unknown pathtracer dock slot");
    }

    [[nodiscard]] SpectraDockSlot ToSpectraDockSlot(const rasterizer::RasterizerDockSlot dockSlot) {
        switch (dockSlot) {
        case rasterizer::RasterizerDockSlot::Center: return SpectraDockSlot::Center;
        case rasterizer::RasterizerDockSlot::Left: return SpectraDockSlot::Left;
        case rasterizer::RasterizerDockSlot::LeftBottom: return SpectraDockSlot::LeftBottom;
        case rasterizer::RasterizerDockSlot::Right: return SpectraDockSlot::Right;
        case rasterizer::RasterizerDockSlot::RightBottom: return SpectraDockSlot::RightBottom;
        case rasterizer::RasterizerDockSlot::Bottom: return SpectraDockSlot::Bottom;
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
        std::uint64_t frame_index{};
    };

    template <typename Adapter>
    concept RasterizerProjectAdapter = std::default_initializable<Adapter> && requires(Adapter& adapter, const Adapter& constAdapter, rasterizer::SceneWorkspace& workspace, const RasterizerProjectFrameInfo& frame) {
        { Adapter::project_id() } -> std::convertible_to<std::string_view>;
        { Adapter::project_title() } -> std::convertible_to<std::string_view>;
        { constAdapter.create_document() } -> std::same_as<rasterizer::SceneDocument>;
        { adapter.reset(workspace) } -> std::same_as<void>;
        { adapter.step(workspace, frame) } -> std::same_as<void>;
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
        virtual void reset(rasterizer::SceneWorkspace& workspace)                      = 0;
        virtual void step(rasterizer::SceneWorkspace& workspace, const RasterizerProjectFrameInfo& frame) = 0;
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

        void reset(rasterizer::SceneWorkspace& workspace) override {
            this->adapter.reset(workspace);
        }

        void step(rasterizer::SceneWorkspace& workspace, const RasterizerProjectFrameInfo& frame) override {
            this->adapter.step(workspace, frame);
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

        [[nodiscard]] std::unique_ptr<RasterizerProjectRuntime> create_first_runtime() {
            if (this->entries.empty()) throw std::runtime_error("Rasterizer project registry is empty");
            return this->entries.front().create_runtime();
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
                .cursor          = frame.cursor,
            });
        }
        edit.replaceFrame(std::move(frame));
        const rasterizer::SceneEditBatch batch = workspace.commit(std::move(edit));
        if (!rasterizer::HasSceneDirtyFlag(batch.dirty, rasterizer::SceneDirtyFlags::Frame)) throw std::runtime_error("Rasterizer project frame commit did not mark the frame dirty");
    }

    [[nodiscard]] rasterizer::FrameCursor MakeFrameCursor(const RasterizerProjectFrameInfo& info) {
        return rasterizer::FrameCursor{
            .frameIndex   = info.frame_index,
            .timeSeconds  = static_cast<double>(info.frame_index) * info.delta_seconds,
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
            document.lights.push_back(rasterizer::SceneLight{
                .name      = "key",
                .kind      = rasterizer::SceneLightKind::Directional,
                .transform = rasterizer::SceneTransform{.rotation = rasterizer::SceneQuaternion{0.35f, 0.0f, 0.0f, 0.94f}},
                .color     = rasterizer::SceneVector3{1.0f, 0.97f, 0.92f},
                .intensity = 3.0f,
            });
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

        void reset(rasterizer::SceneWorkspace& workspace) {
            this->solver.reset();
            CommitRasterizerFrame(workspace, this->make_frame(RasterizerProjectFrameInfo{.delta_seconds = 0.0, .frame_index = 0u}), true);
        }

        void step(rasterizer::SceneWorkspace& workspace, const RasterizerProjectFrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            CommitRasterizerFrame(workspace, this->make_frame(frame), false);
        }

    private:
        xayah::projects::bouncing_ball::BouncingBallSolver solver{};

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
            document.particleSets.push_back(this->make_particles());
            return document;
        }

        void reset(rasterizer::SceneWorkspace& workspace) {
            this->solver.reset();
            CommitRasterizerFrame(workspace, this->make_frame(RasterizerProjectFrameInfo{.delta_seconds = 0.0, .frame_index = 0u}), true);
        }

        void step(rasterizer::SceneWorkspace& workspace, const RasterizerProjectFrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            CommitRasterizerFrame(workspace, this->make_frame(frame), false);
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
            for (const xayah::projects::sparkles::SparklesParticle& particle : visibleParticles) {
                particles.positions.push_back(ToRasterizerVector3(particle.position));
                particles.radii.push_back(particle.radius);
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
            document.lights.push_back(rasterizer::SceneLight{
                .name      = "key",
                .kind      = rasterizer::SceneLightKind::Directional,
                .transform = rasterizer::SceneTransform{.rotation = rasterizer::SceneQuaternion{0.45f, 0.18f, 0.0f, 0.87f}},
                .color     = rasterizer::SceneVector3{0.92f, 0.96f, 1.0f},
                .intensity = 3.6f,
            });
            document.meshes.push_back(this->make_mesh());
            document.cloths.push_back(rasterizer::SceneCloth{
                .name         = "cloth.body",
                .meshName     = "cloth.mesh",
                .materialName = "cloth",
                .dynamic      = true,
            });
            return document;
        }

        void reset(rasterizer::SceneWorkspace& workspace) {
            this->solver.reset();
            CommitRasterizerFrame(workspace, this->make_frame(RasterizerProjectFrameInfo{.delta_seconds = 0.0, .frame_index = 0u}), true);
        }

        void step(rasterizer::SceneWorkspace& workspace, const RasterizerProjectFrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            CommitRasterizerFrame(workspace, this->make_frame(frame), false);
        }

    private:
        xayah::projects::cloth::ClothSolver solver{xayah::projects::cloth::ClothConfig{}, xayah::projects::cloth::ClothSphereCollider{}};

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
            document.volumes.push_back(this->make_volume_metadata(0u));
            return document;
        }

        void reset(rasterizer::SceneWorkspace& workspace) {
            this->solver.reset();
            CommitRasterizerFrame(workspace, this->make_frame(RasterizerProjectFrameInfo{.delta_seconds = 0.0, .frame_index = 0u}), true);
        }

        void step(rasterizer::SceneWorkspace& workspace, const RasterizerProjectFrameInfo& frame) {
            this->solver.step(static_cast<float>(frame.delta_seconds));
            CommitRasterizerFrame(workspace, this->make_frame(frame), false);
        }

    private:
        xayah::projects::pyro::PyroSolver solver{};
        std::array<std::uint32_t, 3> resolution{64u, 96u, 64u};
        float cell_size{0.01875f};

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

        [[nodiscard]] rasterizer::SceneFrameSnapshot make_frame(const RasterizerProjectFrameInfo& frame) {
            const xayah::projects::pyro::PyroFrame pyroFrame = this->solver.read_frame(ToPyroFrameIndex(frame.frame_index));
            this->resolution                                   = pyroFrame.resolution;
            this->cell_size                                    = pyroFrame.cell_size;
            rasterizer::SceneFrameSnapshot snapshot{
                .cursor = MakeFrameCursor(frame),
            };
            snapshot.volumes.push_back(this->make_volume_metadata(frame.frame_index));
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
        RasterizerSpectraRenderer(std::shared_ptr<rasterizer::SceneWorkspace> sceneWorkspace, std::unique_ptr<RasterizerProjectRuntime> projectRuntime) : scene_workspace(std::move(sceneWorkspace)), project_runtime(std::move(projectRuntime)), renderer(std::make_unique<rasterizer::RasterizerRenderer>(this->scene_workspace)) {
            if (this->scene_workspace == nullptr) throw std::runtime_error("Rasterizer adapter requires a scene workspace");
            if (this->project_runtime == nullptr) throw std::runtime_error("Rasterizer adapter requires a project runtime");
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
            if (frame.frame_index != this->last_project_frame_index) {
                const std::shared_ptr<const rasterizer::SceneDocument> document = this->scene_workspace->document();
                if (document->framesPerSecond <= 0.0) throw std::runtime_error("Rasterizer project scene frame rate must be positive");
                this->project_runtime->step(*this->scene_workspace, RasterizerProjectFrameInfo{
                                                                      .delta_seconds = 1.0 / document->framesPerSecond,
                                                                      .frame_index   = frame.frame_index,
                                                                  });
                this->last_project_frame_index = frame.frame_index;
            }
            rasterizer::RasterizerFrameResult result = this->renderer->begin_frame(rasterizerHost, rasterizer::RasterizerFrameInfo{
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
        std::shared_ptr<rasterizer::SceneWorkspace> scene_workspace{};
        std::unique_ptr<RasterizerProjectRuntime> project_runtime{};
        std::uint32_t last_project_frame_index{0};
        std::unique_ptr<rasterizer::RasterizerRenderer> renderer{};
    };

    static_assert(SpectraRendererForHost<PathtracerSpectraRenderer, Spectra>);
    static_assert(SpectraRendererForHost<RasterizerSpectraRenderer, Spectra>);

    void RegisterRenderers(Spectra& app, std::shared_ptr<pathtracer::PbrtSceneLibrary> pbrtSceneLibrary, std::shared_ptr<rasterizer::SceneWorkspace> rasterizerSceneWorkspace, std::unique_ptr<RasterizerProjectRuntime> rasterizerProjectRuntime) {
        app.register_renderer(PathtracerSpectraRenderer{std::move(pbrtSceneLibrary)});
        app.register_renderer(RasterizerSpectraRenderer{std::move(rasterizerSceneWorkspace), std::move(rasterizerProjectRuntime)});
    }
} // namespace spectra::app

int main(const int argc, char**) {
    try {
        if (argc != 1) throw std::runtime_error("usage: spectra_gui");

        std::shared_ptr<spectra::pathtracer::PbrtSceneLibrary> pbrt_scene_library = std::make_shared<spectra::pathtracer::PbrtSceneLibrary>();
        spectra::app::RasterizerProjectRegistry rasterizer_project_registry = spectra::app::MakeRasterizerProjectRegistry();
        std::unique_ptr<spectra::app::RasterizerProjectRuntime> rasterizer_project_runtime = rasterizer_project_registry.create_first_runtime();
        spectra::rasterizer::SceneDocument rasterizer_project_document = rasterizer_project_runtime->create_document();
        std::shared_ptr<spectra::rasterizer::SceneWorkspace> rasterizer_scene_workspace = std::make_shared<spectra::rasterizer::SceneWorkspace>(std::move(rasterizer_project_document));
        rasterizer_project_runtime->reset(*rasterizer_scene_workspace);

        spectra::Spectra app{"Spectra"};
        spectra::app::RegisterRenderers(app, std::move(pbrt_scene_library), std::move(rasterizer_scene_workspace), std::move(rasterizer_project_runtime));
        app.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
