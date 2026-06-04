module;

#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

export module xayah.spectra.contract;

import std;

export namespace xayah {
    enum class SpectraDockSlot {
        Center,
        Left,
        LeftBottom,
        Right,
        RightBottom,
        Bottom,
        Floating,
    };

    struct SpectraPanel {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        SpectraDockSlot dock_slot{SpectraDockSlot::Floating};
        ImGuiWindowFlags window_flags{0};
        bool visible{true};
        bool closable{true};
        bool show_in_menu{true};
        bool show_in_toolbar{true};
        bool zero_window_padding{false};
        std::move_only_function<void()> draw{};
    };

    struct SpectraFrameInfo {
        std::uint32_t frame_index{};
        std::uint32_t image_index{};
    };

    struct SpectraFrameResult {
        std::optional<vk::Semaphore> completion_semaphore{};
        bool close_requested{false};
        std::optional<std::string> window_detail{};
    };

    struct SpectraRendererAvailability {
        bool available{true};
        std::string detail{};
    };

    template <typename Panel>
    concept SpectraPanelLike = requires(Panel panel) {
        std::string{std::move(panel.id)};
        std::string{std::move(panel.title)};
        std::string{std::move(panel.icon)};
        std::string{std::move(panel.shortcut_label)};
        static_cast<ImGuiKey>(panel.shortcut_key);
        static_cast<std::underlying_type_t<SpectraDockSlot>>(panel.dock_slot);
        static_cast<ImGuiWindowFlags>(panel.window_flags);
        { panel.visible } -> std::convertible_to<bool>;
        { panel.closable } -> std::convertible_to<bool>;
        { panel.show_in_menu } -> std::convertible_to<bool>;
        { panel.show_in_toolbar } -> std::convertible_to<bool>;
        { panel.zero_window_padding } -> std::convertible_to<bool>;
        std::move_only_function<void()>{std::move(panel.draw)};
    };

    template <typename Frame>
    concept SpectraFrameInfoLike = requires(const Frame& frame) {
        { frame.frame_index } -> std::convertible_to<std::uint32_t>;
        { frame.image_index } -> std::convertible_to<std::uint32_t>;
    };

    template <typename Result>
    concept SpectraFrameResultLike = requires(Result result) {
        std::optional<vk::Semaphore>{std::move(result.completion_semaphore)};
        { result.close_requested } -> std::convertible_to<bool>;
        std::optional<std::string>{std::move(result.window_detail)};
    };

    template <typename Host>
    concept SpectraSceneHost = requires(Host& host, SpectraPanel panel) {
        { host.register_panel(std::move(panel)) } -> std::same_as<void>;
    };

    template <typename Renderer, typename Host>
    concept SpectraRendererForHost = std::movable<std::remove_cvref_t<Renderer>> && requires(std::remove_cvref_t<Renderer>& renderer, const std::remove_cvref_t<Renderer>& constRenderer, Host& host, const SpectraFrameInfo& frame, const vk::raii::CommandBuffer& commandBuffer) {
        { constRenderer.name() } -> std::convertible_to<std::string_view>;
        { renderer.attach(host) } -> std::same_as<void>;
        { renderer.detach(host) } noexcept -> std::same_as<void>;
        { renderer.before_imgui_shutdown(host) } noexcept -> std::same_as<void>;
        { renderer.after_imgui_created(host) } -> std::same_as<void>;
        { renderer.begin_frame(host, frame) } -> SpectraFrameResultLike;
        { renderer.record_frame(commandBuffer) } -> std::same_as<void>;
    };
} // namespace xayah
