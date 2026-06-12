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

    static_assert(spectra::pathtracer::PathtracerHost<spectra::Spectra>);
    static_assert(spectra::rasterizer::Host<spectra::Spectra>);
    static_assert(static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::pathtracer::PathtracerDockSlot::Center) == static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::DockSlot::Center));
    static_assert(static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::pathtracer::PathtracerDockSlot::Floating) == static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::DockSlot::Floating));
    static_assert(static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::rasterizer::DockSlot::Center) == static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::DockSlot::Center));
    static_assert(static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::rasterizer::DockSlot::Floating) == static_cast<std::underlying_type_t<spectra::DockSlot>>(spectra::DockSlot::Floating));
    static_assert(spectra::rasterizer::VisualizationSource<xayah::projects::bouncing_ball::BouncingBallVisualization>);
    static_assert(spectra::rasterizer::VisualizationSource<xayah::projects::sparkles::SparklesVisualization>);
    static_assert(spectra::rasterizer::VisualizationSource<xayah::projects::cloth::ClothVisualization>);
    static_assert(spectra::rasterizer::VisualizationSource<xayah::projects::pyro::PyroVisualization>);

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
        PathtracerRendererAdapter(std::shared_ptr<const spectra::scene::PbrtScene> pbrt_scene, std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace) : pbrt_scene(std::move(pbrt_scene)), camera_workspace(std::move(camera_workspace)) {
            if (this->pbrt_scene == nullptr) throw std::runtime_error("Pathtracer adapter requires a PBRT scene");
            if (this->camera_workspace == nullptr) throw std::runtime_error("Pathtracer adapter requires a scene camera workspace");
            this->renderer = std::make_unique<spectra::pathtracer::PathtracerRenderer>(this->pbrt_scene, this->camera_workspace);
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
        std::shared_ptr<const spectra::scene::PbrtScene> pbrt_scene{};
        std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace{};
        std::unique_ptr<spectra::pathtracer::PathtracerRenderer> renderer{};
    };

    class RasterizerRendererAdapter final {
    public:
        RasterizerRendererAdapter(spectra::rasterizer::VisualizationRegistry registry, std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace) : visualization_controller(std::make_shared<spectra::rasterizer::VisualizationController>(std::move(registry))), camera_workspace(std::move(camera_workspace)), renderer(std::make_unique<spectra::rasterizer::Renderer>(this->visualization_controller->active_workspace(), this->camera_workspace)) {
            if (this->visualization_controller == nullptr) throw std::runtime_error("Rasterizer adapter requires a visualization controller");
            if (this->camera_workspace == nullptr) throw std::runtime_error("Rasterizer adapter requires a scene camera workspace");
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
            if (this->visualization_controller->apply_pending_visualization()) this->renderer->set_scene_workspace(this->visualization_controller->active_workspace(), this->camera_workspace);
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
        std::shared_ptr<spectra::rasterizer::VisualizationController> visualization_controller{};
        std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace{};
        std::unique_ptr<spectra::rasterizer::Renderer> renderer{};
    };

    static_assert(spectra::RendererFor<PathtracerRendererAdapter, spectra::Spectra>);
    static_assert(spectra::RendererFor<RasterizerRendererAdapter, spectra::Spectra>);

    void register_project_visualizations(spectra::rasterizer::VisualizationRegistry& registry) {
        registry.register_source<xayah::projects::bouncing_ball::BouncingBallVisualization>();
        registry.register_source<xayah::projects::sparkles::SparklesVisualization>();
        registry.register_source<xayah::projects::cloth::ClothVisualization>();
        registry.register_source<xayah::projects::pyro::PyroVisualization>();
    }

    [[nodiscard]] spectra::rasterizer::VisualizationRegistry make_visualization_registry(std::shared_ptr<const spectra::scene::PbrtScene> pbrt_scene) {
        if (pbrt_scene == nullptr) throw std::runtime_error("Visualization registry requires a PBRT preview source scene");
        spectra::rasterizer::VisualizationRegistry registry{};
        registry.register_static_visualization(std::string{CornellBoxSceneId}, "Cornell Box", [pbrt_scene = std::move(pbrt_scene)] { return pbrt_scene->make_preview_document(); });
        register_project_visualizations(registry);
        return registry;
    }

    void register_renderers(spectra::Spectra& application, std::shared_ptr<const spectra::scene::PbrtScene> pbrt_scene, spectra::rasterizer::VisualizationRegistry visualizations, std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace) {
        if (pbrt_scene == nullptr) throw std::runtime_error("Renderer registration requires a PBRT scene snapshot");
        if (camera_workspace == nullptr) throw std::runtime_error("Renderer registration requires a scene camera workspace");
        application.register_renderer(RasterizerRendererAdapter{std::move(visualizations), camera_workspace});
        application.register_renderer(PathtracerRendererAdapter{std::move(pbrt_scene), std::move(camera_workspace)});
    }
} // namespace

int main(const int argc, char**) {
    try {
        if (argc != 1) throw std::runtime_error("usage: spectra_gui");

        std::shared_ptr<const spectra::scene::PbrtScene> pbrt_scene = std::make_shared<spectra::scene::PbrtScene>(spectra::scene::PbrtScene::parse(CornellBoxSceneId));
        spectra::rasterizer::VisualizationRegistry visualizations = make_visualization_registry(pbrt_scene);
        std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace = std::make_shared<spectra::scene::Scene::CameraWorkspace>();

        spectra::Spectra app{"Spectra"};
        register_renderers(app, std::move(pbrt_scene), std::move(visualizations), std::move(camera_workspace));
        app.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
