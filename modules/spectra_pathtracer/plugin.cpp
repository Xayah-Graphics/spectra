module;
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <vulkan/vulkan_raii.hpp>

#include "session.h"

module spectra.pathtracer;
import std;
import spectra;

namespace {
    [[nodiscard]] xayah::spectra_pathtracer::HostContext spectra_pathtracer_host_context(const xayah::SpectraContext& context) {
        return {
            &context.physical_device(),
            &context.device(),
            context.frame_count(),
            context.swapchain_extent(),
        };
    }
}

namespace xayah::spectra_pathtracer {
    class SpectraPathtracerPlugin final : public xayah::SpectraPlugin {
    public:
        explicit SpectraPathtracerPlugin(std::filesystem::path scene_path) : session{std::make_unique<InteractiveSession>(std::move(scene_path))} {}
        ~SpectraPathtracerPlugin() noexcept override = default;

        [[nodiscard]] std::string_view name() const override {
            return "Spectra Pathtracer";
        }

        void attach(xayah::SpectraContext& context) override {
            this->session->attach(spectra_pathtracer_host_context(context));
            this->register_panels(context);
            context.set_window_detail(this->session->window_detail());
        }

        void detach(xayah::SpectraContext&) noexcept override {
            this->session->detach();
        }

        void before_imgui_shutdown(xayah::SpectraContext&) noexcept override {
            this->session->before_imgui_shutdown();
        }

        void after_imgui_created(xayah::SpectraContext&) override {
            this->session->after_imgui_created();
        }

        void begin_frame(xayah::SpectraFrameContext& context) override {
            this->session->update_host(spectra_pathtracer_host_context(context.app()));
            const FrameOutput output = this->session->begin_frame(FrameInput{context.frame_index(), context.image_index()});
            if (output.uses_external_completion) context.request_external_completion(output.completion_semaphore);
            if (this->session->close_requested()) {
                context.request_close();
                this->session->clear_close_request();
            }
            context.set_window_detail(this->session->window_detail());
        }

        void record_frame(xayah::SpectraRecordContext& context) override {
            this->session->record_frame(context.command_buffer());
        }

    private:
        void register_panels(xayah::SpectraContext& context) {
            xayah::SpectraPanel viewport{};
            viewport.id = "spectra_pathtracer.viewport";
            viewport.title = "Viewport";
            viewport.dock_slot = xayah::SpectraDockSlot::Center;
            viewport.window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;
            viewport.closable = false;
            viewport.show_in_menu = false;
            viewport.show_in_toolbar = false;
            viewport.zero_window_padding = true;
            viewport.draw = [this](xayah::SpectraPanelContext&) { this->session->draw_viewport_window(); };
            context.register_panel(std::move(viewport));

            xayah::SpectraPanel camera{};
            camera.id = "spectra_pathtracer.camera";
            camera.title = "Camera";
            camera.icon = ICON_MS_PHOTO_CAMERA;
            camera.shortcut_label = "F1";
            camera.shortcut_key = ImGuiKey_F1;
            camera.dock_slot = xayah::SpectraDockSlot::Left;
            camera.draw = [this](xayah::SpectraPanelContext&) { this->session->draw_camera_window(); };
            context.register_panel(std::move(camera));

            xayah::SpectraPanel scene_browser{};
            scene_browser.id = "spectra_pathtracer.scene_browser";
            scene_browser.title = "Scene Browser";
            scene_browser.icon = ICON_MS_ACCOUNT_TREE;
            scene_browser.shortcut_label = "F2";
            scene_browser.shortcut_key = ImGuiKey_F2;
            scene_browser.dock_slot = xayah::SpectraDockSlot::Right;
            scene_browser.draw = [this](xayah::SpectraPanelContext&) { this->session->draw_scene_browser_window(); };
            context.register_panel(std::move(scene_browser));

            xayah::SpectraPanel settings{};
            settings.id = "spectra_pathtracer.settings";
            settings.title = "Settings";
            settings.icon = ICON_MS_SETTINGS;
            settings.shortcut_label = "F3";
            settings.shortcut_key = ImGuiKey_F3;
            settings.dock_slot = xayah::SpectraDockSlot::Left;
            settings.draw = [this](xayah::SpectraPanelContext&) { this->session->draw_settings_window(); };
            context.register_panel(std::move(settings));

            xayah::SpectraPanel inspector{};
            inspector.id = "spectra_pathtracer.inspector";
            inspector.title = "Inspector";
            inspector.icon = ICON_MS_LIST_ALT;
            inspector.shortcut_label = "F4";
            inspector.shortcut_key = ImGuiKey_F4;
            inspector.dock_slot = xayah::SpectraDockSlot::RightBottom;
            inspector.draw = [this](xayah::SpectraPanelContext&) { this->session->draw_inspector_window(); };
            context.register_panel(std::move(inspector));

            xayah::SpectraPanel environment{};
            environment.id = "spectra_pathtracer.environment";
            environment.title = "Environment";
            environment.icon = ICON_MS_PUBLIC;
            environment.shortcut_label = "F5";
            environment.shortcut_key = ImGuiKey_F5;
            environment.dock_slot = xayah::SpectraDockSlot::LeftBottom;
            environment.draw = [this](xayah::SpectraPanelContext&) { this->session->draw_environment_window(); };
            context.register_panel(std::move(environment));

            xayah::SpectraPanel tonemapper{};
            tonemapper.id = "spectra_pathtracer.tonemapper";
            tonemapper.title = "Tonemapper";
            tonemapper.icon = ICON_MS_TONALITY;
            tonemapper.shortcut_label = "F6";
            tonemapper.shortcut_key = ImGuiKey_F6;
            tonemapper.dock_slot = xayah::SpectraDockSlot::LeftBottom;
            tonemapper.draw = [this](xayah::SpectraPanelContext&) { this->session->draw_tonemapper_window(); };
            context.register_panel(std::move(tonemapper));

            xayah::SpectraPanel statistics{};
            statistics.id = "spectra_pathtracer.statistics";
            statistics.title = "Statistics";
            statistics.icon = ICON_MS_ANALYTICS;
            statistics.dock_slot = xayah::SpectraDockSlot::Bottom;
            statistics.show_in_toolbar = false;
            statistics.draw = [this](xayah::SpectraPanelContext&) { this->session->draw_statistics_window(); };
            context.register_panel(std::move(statistics));
        }

        std::unique_ptr<InteractiveSession> session{};
    };
} // namespace xayah::spectra_pathtracer

namespace xayah {
    std::unique_ptr<SpectraPlugin> create_spectra_pathtracer_plugin(std::filesystem::path scene_path) {
        return std::make_unique<xayah::spectra_pathtracer::SpectraPathtracerPlugin>(std::move(scene_path));
    }
} // namespace xayah
