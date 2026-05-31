#include "pathtracer.h"

#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
    struct PanelDefinition {
        const char* id{};
        const char* title{};
        const char* icon{};
        const char* shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        xayah::SpectraDockSlot dock_slot{xayah::SpectraDockSlot::Floating};
        ImGuiWindowFlags window_flags{0};
        bool closable{true};
        bool show_in_menu{true};
        bool show_in_toolbar{true};
        bool zero_window_padding{false};
        void (xayah::pathtracer::InteractiveSession::*draw)();
    };

} // namespace

namespace xayah {
    SpectraPathtracer::SpectraPathtracer(std::filesystem::path scene_path) : session{std::move(scene_path)} {
    }

    SpectraPathtracer::~SpectraPathtracer() noexcept = default;

    std::string_view SpectraPathtracer::name() const {
        return "Spectra Pathtracer";
    }

    void SpectraPathtracer::attach(xayah::Spectra& spectra) {
        this->session.attach(xayah::pathtracer::HostContext{&spectra.physical_device(), &spectra.device(), spectra.frame_count(), spectra.swapchain_extent()});
        this->register_panels(spectra);
        spectra.set_window_detail(this->session.window_detail());
    }

    void SpectraPathtracer::detach(xayah::Spectra&) noexcept {
        this->session.detach();
    }

    void SpectraPathtracer::before_imgui_shutdown(xayah::Spectra&) noexcept {
        this->session.before_imgui_shutdown();
    }

    void SpectraPathtracer::after_imgui_created(xayah::Spectra&) {
        this->session.after_imgui_created();
    }

    xayah::SpectraFrameResult SpectraPathtracer::begin_frame(xayah::Spectra& spectra, const xayah::SpectraFrameInfo& frame) {
        this->session.update_host(xayah::pathtracer::HostContext{&spectra.physical_device(), &spectra.device(), spectra.frame_count(), spectra.swapchain_extent()});
        const xayah::pathtracer::FrameOutput output = this->session.begin_frame(xayah::pathtracer::FrameInput{frame.frame_index, frame.image_index});
        xayah::SpectraFrameResult result{};
        result.completion_semaphore = output.completion_semaphore;
        result.close_requested = output.close_requested;
        result.window_detail = this->session.window_detail();
        return result;
    }

    void SpectraPathtracer::record_frame(const vk::raii::CommandBuffer& command_buffer) {
        this->session.record_frame(command_buffer);
    }

    void SpectraPathtracer::register_panels(xayah::Spectra& spectra) {
        constexpr std::array panels{
            PanelDefinition{
                "pathtracer.viewport",
                "Viewport",
                nullptr,
                nullptr,
                ImGuiKey_None,
                xayah::SpectraDockSlot::Center,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground,
                false,
                false,
                false,
                true,
                &xayah::pathtracer::InteractiveSession::draw_viewport_window,
            },
            PanelDefinition{
                "pathtracer.camera",
                "Camera",
                ICON_MS_PHOTO_CAMERA,
                "F1",
                ImGuiKey_F1,
                xayah::SpectraDockSlot::Left,
                0,
                true,
                true,
                true,
                false,
                &xayah::pathtracer::InteractiveSession::draw_camera_window,
            },
            PanelDefinition{
                "pathtracer.scene_browser",
                "Scene Browser",
                ICON_MS_ACCOUNT_TREE,
                "F2",
                ImGuiKey_F2,
                xayah::SpectraDockSlot::Right,
                0,
                true,
                true,
                true,
                false,
                &xayah::pathtracer::InteractiveSession::draw_scene_browser_window,
            },
            PanelDefinition{
                "pathtracer.settings",
                "Settings",
                ICON_MS_SETTINGS,
                "F3",
                ImGuiKey_F3,
                xayah::SpectraDockSlot::Left,
                0,
                true,
                true,
                true,
                false,
                &xayah::pathtracer::InteractiveSession::draw_settings_window,
            },
            PanelDefinition{
                "pathtracer.inspector",
                "Inspector",
                ICON_MS_LIST_ALT,
                "F4",
                ImGuiKey_F4,
                xayah::SpectraDockSlot::RightBottom,
                0,
                true,
                true,
                true,
                false,
                &xayah::pathtracer::InteractiveSession::draw_inspector_window,
            },
            PanelDefinition{
                "pathtracer.environment",
                "Environment",
                ICON_MS_PUBLIC,
                "F5",
                ImGuiKey_F5,
                xayah::SpectraDockSlot::LeftBottom,
                0,
                true,
                true,
                true,
                false,
                &xayah::pathtracer::InteractiveSession::draw_environment_window,
            },
            PanelDefinition{
                "pathtracer.tonemapper",
                "Tonemapper",
                ICON_MS_TONALITY,
                "F6",
                ImGuiKey_F6,
                xayah::SpectraDockSlot::LeftBottom,
                0,
                true,
                true,
                true,
                false,
                &xayah::pathtracer::InteractiveSession::draw_tonemapper_window,
            },
            PanelDefinition{
                "pathtracer.statistics",
                "Statistics",
                ICON_MS_ANALYTICS,
                nullptr,
                ImGuiKey_None,
                xayah::SpectraDockSlot::Bottom,
                0,
                true,
                true,
                false,
                false,
                &xayah::pathtracer::InteractiveSession::draw_statistics_window,
            },
        };

        for (const PanelDefinition& definition : panels) {
            if (definition.id == nullptr || definition.title == nullptr || definition.draw == nullptr) throw std::runtime_error("Invalid Spectra pathtracer panel definition");
            xayah::SpectraPanel panel{};
            panel.id                  = definition.id;
            panel.title               = definition.title;
            panel.icon                = definition.icon == nullptr ? "" : definition.icon;
            panel.shortcut_label      = definition.shortcut_label == nullptr ? "" : definition.shortcut_label;
            panel.shortcut_key        = definition.shortcut_key;
            panel.dock_slot           = definition.dock_slot;
            panel.window_flags        = definition.window_flags;
            panel.closable            = definition.closable;
            panel.show_in_menu        = definition.show_in_menu;
            panel.show_in_toolbar     = definition.show_in_toolbar;
            panel.zero_window_padding = definition.zero_window_padding;
            panel.draw                = [this, draw = definition.draw] { (this->session.*draw)(); };
            spectra.register_panel(std::move(panel));
        }
    }
} // namespace xayah
