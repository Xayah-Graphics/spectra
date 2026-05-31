#ifndef XAYAH_SPECTRA_SPECTRA_H
#define XAYAH_SPECTRA_SPECTRA_H

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <imgui.h>
#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace xayah {
    class Spectra;
    struct SpectraFrameState;
    class SpectraPanelContext;

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
        std::move_only_function<void(SpectraPanelContext&)> draw{};
    };

    class SpectraContext {
    public:
        SpectraContext() = default;

        [[nodiscard]] const vk::raii::PhysicalDevice& physical_device() const;
        [[nodiscard]] const vk::raii::Device& device() const;
        [[nodiscard]] std::uint32_t frame_count() const;
        [[nodiscard]] vk::Extent2D swapchain_extent() const;
        [[nodiscard]] vk::Format swapchain_format() const;
        void register_panel(SpectraPanel panel) const;
        void request_close() const;
        void set_window_detail(std::string detail) const;

    private:
        friend class Spectra;
        friend class SpectraFrameContext;
        friend class SpectraPanelContext;
        friend class SpectraRecordContext;
        explicit SpectraContext(Spectra& spectra);

        Spectra* spectra = nullptr;
    };

    class SpectraFrameContext {
    public:
        [[nodiscard]] SpectraContext app() const;
        [[nodiscard]] std::uint32_t frame_index() const;
        [[nodiscard]] std::uint32_t image_index() const;
        [[nodiscard]] float delta_seconds() const;
        void request_external_completion(vk::Semaphore semaphore) const;
        void request_close() const;
        void set_window_detail(std::string detail) const;

    private:
        friend class Spectra;

        SpectraFrameContext(Spectra& spectra, SpectraFrameState& frame);

        Spectra* spectra = nullptr;
        SpectraFrameState* frame = nullptr;
    };

    class SpectraRecordContext {
    public:
        [[nodiscard]] SpectraContext app() const;
        [[nodiscard]] std::uint32_t frame_index() const;
        [[nodiscard]] std::uint32_t image_index() const;
        [[nodiscard]] const vk::raii::CommandBuffer& command_buffer() const;

    private:
        friend class Spectra;

        SpectraRecordContext(Spectra& spectra, SpectraFrameState& frame, const vk::raii::CommandBuffer& command_buffer);

        Spectra* spectra = nullptr;
        SpectraFrameState* frame = nullptr;
        const vk::raii::CommandBuffer* command_buffer_value = nullptr;
    };

    class SpectraPanelContext {
    public:
        [[nodiscard]] SpectraContext app() const;
        [[nodiscard]] std::string_view panel_id() const;
        [[nodiscard]] std::string_view panel_title() const;
        void request_close() const;

    private:
        friend class Spectra;

        SpectraPanelContext(Spectra& spectra, SpectraPanel& panel);

        Spectra* spectra = nullptr;
        SpectraPanel* panel = nullptr;
    };

    class SpectraPlugin {
    public:
        virtual ~SpectraPlugin() = default;

        [[nodiscard]] virtual std::string_view name() const = 0;
        virtual void attach(SpectraContext& context) = 0;
        virtual void detach(SpectraContext& context) noexcept;
        virtual void before_imgui_shutdown(SpectraContext& context) noexcept;
        virtual void after_imgui_created(SpectraContext& context);
        virtual void begin_frame(SpectraFrameContext& context);
        virtual void record_frame(SpectraRecordContext& context);
    };

    class Spectra {
    public:
        explicit Spectra(const std::string_view& app_name = "Spectra", const std::string_view& engine_name = "Spectra Engine", std::uint32_t window_width = 1920, std::uint32_t window_height = 1080);
        ~Spectra() noexcept;

        Spectra(const Spectra& other)                = delete;
        Spectra(Spectra&& other) noexcept            = delete;
        Spectra& operator=(const Spectra& other)     = delete;
        Spectra& operator=(Spectra&& other) noexcept = delete;

        void register_plugin(std::unique_ptr<SpectraPlugin> plugin);
        void run();

    private:
        friend class SpectraContext;
        friend class SpectraFrameContext;
        friend class SpectraPanelContext;
        friend class SpectraRecordContext;

        void create_imgui();
        void notify_plugins_before_imgui_shutdown() noexcept;
        void notify_plugins_after_imgui_created();
        void destroy_imgui() noexcept;
        void detach_plugins_noexcept() noexcept;

        void render_loop();
        bool begin_frame(SpectraFrameState& frame);
        void record_frame(SpectraFrameState& frame);
        void end_frame(SpectraFrameState& frame);

        void draw_main_menu();
        void draw_menu_toolbar();
        void draw_dockspace();
        void draw_registered_panels();
        void update_window_title(float delta_seconds);

        void create_swapchain(vk::raii::SwapchainKHR old_swapchain = nullptr);
        void recreate_swapchain();

        struct SpectraState;
        std::unique_ptr<SpectraState> state{};
    };
} // namespace xayah

#endif
