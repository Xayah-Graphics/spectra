#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

import std;
import spectra;
import spectra.pathtracer;
import spectra.rasterizer.renderer;
import spectra.rasterizer.visualization;
import spectra.scene;
import xayah.projects.bouncing_ball.visualization;
import xayah.projects.cloth.visualization;
import xayah.projects.pyro.visualization;
import xayah.projects.sparkles.visualization;

namespace {
    inline constexpr std::string_view CornellBoxSceneId = "cornell-box/cornell-box.pbrt";
    inline constexpr std::array<std::string_view, 13> ImplementedExampleSceneIds{
        "example-00-baseline-cornell.pbrt",
        "example-01-analytic-shapes.pbrt",
        "example-02-plymesh.pbrt",
        "example-03-object-instances.pbrt",
        "example-04-patches-subdivision.pbrt",
        "example-05-curves-hair.pbrt",
        "example-06-material-basic-bsdfs.pbrt",
        "example-07-material-advanced-bsdfs.pbrt",
        "example-08-texture-basic.pbrt",
        "example-09-texture-procedural.pbrt",
        "example-10-lights.pbrt",
        "example-11-medium-basic.pbrt",
        "example-12-medium-cloud-nanovdb.pbrt",
    };

    struct CliOptions {
        std::string scene_id{CornellBoxSceneId};
    };

    static_assert(spectra::pathtracer::PathtracerHost<spectra::Spectra>);
    static_assert(spectra::rasterizer::Host<spectra::Spectra>);
    static_assert(static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::pathtracer::PathtracerDockSlot::Center) == static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::DockSlot::Center));
    static_assert(static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::pathtracer::PathtracerDockSlot::Floating) == static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::DockSlot::Floating));
    static_assert(static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::rasterizer::DockSlot::Center) == static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::DockSlot::Center));
    static_assert(static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::rasterizer::DockSlot::Floating) == static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::DockSlot::Floating));
    static_assert(spectra::rasterizer::VisualizationSource<xayah::projects::bouncing_ball::Visualization>);
    static_assert(spectra::rasterizer::VisualizationSource<xayah::projects::sparkles::Visualization>);
    static_assert(spectra::rasterizer::VisualizationSource<xayah::projects::cloth::Visualization>);
    static_assert(spectra::rasterizer::VisualizationSource<xayah::projects::pyro::Visualization>);

    void draw_rasterizer_scene_control_panel(spectra::rasterizer::VisualizationController& controller) {
        const std::size_t selected_index = controller.selected_index();
        const spectra::rasterizer::VisualizationEntry& selected_entry = controller.entry(selected_index);
        ImGui::SeparatorText("Visualization");
        if (ImGui::BeginCombo("Visualization", selected_entry.title.c_str())) {
            for (std::size_t index = 0; index < controller.size(); ++index) {
                const spectra::rasterizer::VisualizationEntry& entry = controller.entry(index);
                const bool selected = index == selected_index;
                if (ImGui::Selectable(entry.title.c_str(), selected)) controller.request_activate(index);
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("%s", selected_entry.id.c_str());
        ImGui::TextDisabled("%s", selected_entry.kind == spectra::rasterizer::VisualizationKind::Static ? "Static" : "Dynamic");
        if (controller.pending_switch()) ImGui::TextDisabled("Switching on next frame");
    }

    class PathtracerRendererAdapter final {
    public:
        PathtracerRendererAdapter(std::shared_ptr<spectra::rasterizer::VisualizationController> visualization_controller, std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace) : visualization_controller(std::move(visualization_controller)), camera_workspace(std::move(camera_workspace)) {
            if (this->visualization_controller == nullptr) throw std::runtime_error("Pathtracer adapter requires a visualization controller");
            if (this->camera_workspace == nullptr) throw std::runtime_error("Pathtracer adapter requires a scene camera workspace");
            this->active_workspace = this->visualization_controller->active_workspace();
            this->renderer = std::make_unique<spectra::pathtracer::PathtracerRenderer>(this->active_workspace, this->camera_workspace);
        }

        PathtracerRendererAdapter(const PathtracerRendererAdapter& other) = delete;
        PathtracerRendererAdapter(PathtracerRendererAdapter&& other) noexcept = default;
        PathtracerRendererAdapter& operator=(const PathtracerRendererAdapter& other) = delete;
        PathtracerRendererAdapter& operator=(PathtracerRendererAdapter&& other) noexcept = default;
        ~PathtracerRendererAdapter() noexcept = default;

        [[nodiscard]] static std::string_view name() {
            return spectra::pathtracer::PathtracerRenderer::name();
        }

        void attach(spectra::Spectra& host) {
            this->renderer->attach(spectra::pathtracer::PathtracerHostView{host});
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

        [[nodiscard]] spectra::FrameResult begin_frame(spectra::Spectra& host, const spectra::FrameContext& frame) {
            static_cast<void>(this->visualization_controller->apply_pending_visualization());
            this->sync_scene_workspace();
            this->visualization_controller->update_active_visualization(frame.delta_seconds);
            const spectra::pathtracer::PathtracerFrameInfo pathtracer_frame{
                .frame_index = frame.frame_slot_index,
                .image_index = frame.image_index,
            };
            spectra::pathtracer::PathtracerFrameResult result = this->renderer->begin_frame(spectra::pathtracer::PathtracerHostView{host}, pathtracer_frame);
            return spectra::FrameResult{
                .completion_semaphore = std::move(result.completion_semaphore),
                .close_requested      = result.close_requested,
                .window_detail        = std::move(result.window_detail),
            };
        }

        void record_frame(const vk::raii::CommandBuffer& command_buffer) {
            this->renderer->record_frame(command_buffer);
        }

    private:
        void sync_scene_workspace() {
            std::shared_ptr<spectra::scene::Scene> current_workspace = this->visualization_controller->active_workspace();
            if (this->active_workspace == current_workspace) return;
            this->renderer->set_scene_workspace(current_workspace, this->camera_workspace);
            this->active_workspace = std::move(current_workspace);
        }

        std::shared_ptr<spectra::rasterizer::VisualizationController> visualization_controller{};
        std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace{};
        std::shared_ptr<spectra::scene::Scene> active_workspace{};
        std::unique_ptr<spectra::pathtracer::PathtracerRenderer> renderer{};
    };

    class RasterizerRendererAdapter final {
    public:
        RasterizerRendererAdapter(std::shared_ptr<spectra::rasterizer::VisualizationController> visualization_controller, std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace) : visualization_controller(std::move(visualization_controller)), camera_workspace(std::move(camera_workspace)) {
            if (this->visualization_controller == nullptr) throw std::runtime_error("Rasterizer adapter requires a visualization controller");
            if (this->camera_workspace == nullptr) throw std::runtime_error("Rasterizer adapter requires a scene camera workspace");
            this->active_workspace = this->visualization_controller->active_workspace();
            this->renderer = std::make_unique<spectra::rasterizer::Renderer>(this->active_workspace, this->camera_workspace);
            this->renderer->set_control_panel_extension([visualization_controller = this->visualization_controller] { draw_rasterizer_scene_control_panel(*visualization_controller); });
        }

        RasterizerRendererAdapter(const RasterizerRendererAdapter& other) = delete;
        RasterizerRendererAdapter(RasterizerRendererAdapter&& other) noexcept = default;
        RasterizerRendererAdapter& operator=(const RasterizerRendererAdapter& other) = delete;
        RasterizerRendererAdapter& operator=(RasterizerRendererAdapter&& other) noexcept = default;
        ~RasterizerRendererAdapter() noexcept = default;

        [[nodiscard]] static std::string_view name() {
            return spectra::rasterizer::Renderer::name();
        }

        void attach(spectra::Spectra& host) {
            this->renderer->attach(spectra::rasterizer::HostView{host});
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

        [[nodiscard]] spectra::FrameResult begin_frame(spectra::Spectra& host, const spectra::FrameContext& frame) {
            static_cast<void>(this->visualization_controller->apply_pending_visualization());
            this->sync_scene_workspace();
            this->visualization_controller->update_active_visualization(frame.delta_seconds);
            const spectra::rasterizer::FrameContext rasterizer_frame{
                .frame_index   = frame.frame_slot_index,
                .image_index   = frame.image_index,
                .frame_number  = frame.frame_number,
                .delta_seconds = frame.delta_seconds,
            };
            spectra::rasterizer::FrameResult result = this->renderer->begin_frame(spectra::rasterizer::HostView{host}, rasterizer_frame);
            return spectra::FrameResult{
                .completion_semaphore = std::move(result.completion_semaphore),
                .close_requested      = result.close_requested,
                .window_detail        = std::move(result.window_detail),
            };
        }

        void record_frame(const vk::raii::CommandBuffer& command_buffer) {
            this->renderer->record_frame(command_buffer);
        }

    private:
        void sync_scene_workspace() {
            std::shared_ptr<spectra::scene::Scene> current_workspace = this->visualization_controller->active_workspace();
            if (this->active_workspace == current_workspace) return;
            this->renderer->set_scene_workspace(current_workspace, this->camera_workspace);
            this->active_workspace = std::move(current_workspace);
        }

        std::shared_ptr<spectra::rasterizer::VisualizationController> visualization_controller{};
        std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace{};
        std::shared_ptr<spectra::scene::Scene> active_workspace{};
        std::unique_ptr<spectra::rasterizer::Renderer> renderer{};
    };

    static_assert(spectra::RendererFor<PathtracerRendererAdapter, spectra::Spectra>);
    static_assert(spectra::RendererFor<RasterizerRendererAdapter, spectra::Spectra>);

    void register_project_visualizations(spectra::rasterizer::VisualizationRegistry& registry) {
        registry.register_source<xayah::projects::bouncing_ball::Visualization>();
        registry.register_source<xayah::projects::sparkles::Visualization>();
        registry.register_source<xayah::projects::cloth::Visualization>();
        registry.register_source<xayah::projects::pyro::Visualization>();
    }

    [[nodiscard]] std::filesystem::path scene_id_stem(std::string_view scene_id) {
        std::filesystem::path filename = std::filesystem::path{std::string{scene_id}}.filename();
        if (filename.extension() == ".pbrt") filename = filename.stem();
        return filename;
    }

    [[nodiscard]] std::string static_scene_title(std::string_view scene_id) {
        std::filesystem::path stem = scene_id_stem(scene_id);
        if (stem.empty()) throw std::runtime_error("Static visualization scene id has an empty filename stem");
        return stem.string();
    }

    [[nodiscard]] bool same_static_scene(std::string_view left, std::string_view right) {
        if (left == right) return true;
        return scene_id_stem(left) == scene_id_stem(right);
    }

    void register_example_visualizations(spectra::rasterizer::VisualizationRegistry& registry, const std::string& selected_scene_id) {
        for (const std::string_view example_scene_id : ImplementedExampleSceneIds) {
            if (same_static_scene(selected_scene_id, example_scene_id)) continue;
            const std::string id{example_scene_id};
            registry.register_static_visualization(id, static_scene_title(id), [id] { return std::make_shared<spectra::scene::Scene>(spectra::scene::Scene::parse_pbrt(id)); });
        }
    }

    [[nodiscard]] CliOptions parse_cli(const int argc, char** argv) {
        CliOptions options{};
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument{argv[index]};
            if (argument == "--scene") {
                ++index;
                if (index >= argc) throw std::runtime_error("usage: spectra_gui [--scene <scene-id>]");
                options.scene_id = argv[index];
                if (options.scene_id.empty()) throw std::runtime_error("spectra_gui --scene requires a non-empty scene id");
            } else if (argument == "--help" || argument == "-h") {
                throw std::runtime_error("usage: spectra_gui [--scene <scene-id>]");
            } else {
                throw std::runtime_error("usage: spectra_gui [--scene <scene-id>]");
            }
        }
        return options;
    }

    [[nodiscard]] spectra::rasterizer::VisualizationRegistry make_visualization_registry(std::shared_ptr<spectra::scene::Scene> scene, std::string scene_id) {
        if (scene == nullptr) throw std::runtime_error("Visualization registry requires a preview source scene");
        spectra::rasterizer::VisualizationRegistry registry{};
        const std::string title = scene->info().title;
        registry.register_static_visualization(std::move(scene_id), title, [scene = std::move(scene)] { return scene; });
        const std::string selected_scene_id = registry.entry(0u).id;
        register_example_visualizations(registry, selected_scene_id);
        register_project_visualizations(registry);
        return registry;
    }

    void register_renderers(spectra::Spectra& application, spectra::rasterizer::VisualizationRegistry visualizations, std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace) {
        if (camera_workspace == nullptr) throw std::runtime_error("Renderer registration requires a scene camera workspace");
        std::shared_ptr<spectra::rasterizer::VisualizationController> visualization_controller = std::make_shared<spectra::rasterizer::VisualizationController>(std::move(visualizations));
        application.register_renderer(RasterizerRendererAdapter{visualization_controller, camera_workspace});
        application.register_renderer(PathtracerRendererAdapter{std::move(visualization_controller), std::move(camera_workspace)});
    }
} // namespace

int main(const int argc, char** argv) {
    try {
        const CliOptions options = parse_cli(argc, argv);
        std::shared_ptr<spectra::scene::Scene> scene = std::make_shared<spectra::scene::Scene>(spectra::scene::Scene::parse_pbrt(options.scene_id));
        spectra::rasterizer::VisualizationRegistry visualizations = make_visualization_registry(scene, options.scene_id);
        std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace = std::make_shared<spectra::scene::Scene::CameraWorkspace>();

        spectra::Spectra app{"Spectra"};
        register_renderers(app, std::move(visualizations), std::move(camera_workspace));
        app.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
