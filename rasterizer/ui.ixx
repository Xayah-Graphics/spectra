module;

#include <imgui.h>

export module spectra.rasterizer.ui;

import std;

namespace spectra::rasterizer {
    export enum class DockSlot : std::uint8_t {
        Center = 0,
        Floating = 1,
    };

    export struct Panel {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string owner_renderer{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        DockSlot dock_slot{DockSlot::Floating};
        ImGuiWindowFlags window_flags{0};
        bool visible{true};
        bool closable{true};
        bool zero_window_padding{false};
        std::move_only_function<void()> draw{};
    };

    export struct SidebarTab {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string owner_renderer{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        std::move_only_function<void()> draw{};
    };

    export struct ToolbarAction {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string owner_renderer{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        std::move_only_function<bool()> active{};
        std::move_only_function<void()> trigger{};
    };
} // namespace spectra::rasterizer
