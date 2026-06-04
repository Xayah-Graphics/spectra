#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

import std;
import spectra;
import spectra.pathtracer;
import spectra.pathtracer.pbrt.library;
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
        explicit RasterizerSpectraRenderer(std::shared_ptr<rasterizer::SceneWorkspace> sceneWorkspace) : renderer(std::make_unique<rasterizer::RasterizerRenderer>(std::move(sceneWorkspace))) {}

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
        std::unique_ptr<rasterizer::RasterizerRenderer> renderer{};
    };

    static_assert(SpectraRendererForHost<PathtracerSpectraRenderer, Spectra>);
    static_assert(SpectraRendererForHost<RasterizerSpectraRenderer, Spectra>);

    [[nodiscard]] rasterizer::SceneDocument MakeDefaultRasterizerScene() {
        rasterizer::SceneDocument scene{
            .revision        = rasterizer::SceneRevision{1},
            .name            = "rasterizer.workspace",
            .title           = "Rasterizer Workspace",
            .source          = "generated://spectra_gui/rasterizer.workspace",
            .framesPerSecond = 24.0,
        };
        scene.materials.push_back(rasterizer::SceneMaterial{
            .name      = "default",
            .baseColor = rasterizer::SceneVector4{0.72f, 0.76f, 0.78f, 1.0f},
            .roughness = 0.55f,
        });
        scene.lights.push_back(rasterizer::SceneLight{
            .name      = "key",
            .kind      = rasterizer::SceneLightKind::Directional,
            .transform = rasterizer::SceneTransform{.rotation = rasterizer::SceneQuaternion{0.35f, 0.0f, 0.0f, 0.94f}},
            .color     = rasterizer::SceneVector3{1.0f, 0.97f, 0.92f},
            .intensity = 3.0f,
        });
        return scene;
    }

    void RegisterRenderers(Spectra& app, std::shared_ptr<pathtracer::PbrtSceneLibrary> pbrtSceneLibrary, std::shared_ptr<rasterizer::SceneWorkspace> rasterizerSceneWorkspace) {
        app.register_renderer(PathtracerSpectraRenderer{std::move(pbrtSceneLibrary)});
        app.register_renderer(RasterizerSpectraRenderer{std::move(rasterizerSceneWorkspace)});
    }
} // namespace spectra::app

int main(const int argc, char**) {
    try {
        if (argc != 1) throw std::runtime_error("usage: spectra_gui");

        std::shared_ptr<spectra::pathtracer::PbrtSceneLibrary> pbrt_scene_library = std::make_shared<spectra::pathtracer::PbrtSceneLibrary>();
        std::shared_ptr<spectra::rasterizer::SceneWorkspace> rasterizer_scene_workspace = std::make_shared<spectra::rasterizer::SceneWorkspace>(spectra::app::MakeDefaultRasterizerScene());

        spectra::Spectra app{"Spectra"};
        spectra::app::RegisterRenderers(app, std::move(pbrt_scene_library), std::move(rasterizer_scene_workspace));
        app.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
