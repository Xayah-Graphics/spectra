export module spectra.editor.platform.presentation;

import spectra.editor.platform.window;
import spectra.runtime;
import std;
import vulkan;

namespace spectra {
    export inline constexpr std::array<const char*, 2> presentation_instance_extensions{
        vk::KHRSurfaceExtensionName,
        vk::KHRWin32SurfaceExtensionName,
    };

    export struct VulkanSurface {
        VulkanSurface(WindowPlatform& platform, VulkanInstance& instance);

        VulkanSurface(const VulkanSurface&)            = delete;
        VulkanSurface(VulkanSurface&&)                 = delete;
        VulkanSurface& operator=(const VulkanSurface&) = delete;
        VulkanSurface& operator=(VulkanSurface&&)      = delete;

        vk::raii::SurfaceKHR surface{nullptr};
    };

    export struct PresentationTarget {
        vk::Image image{};
        vk::ImageView view{};
        vk::Extent2D extent{};
        vk::ImageLayout image_layout{};
    };

    export struct PresentedFrameContext {
        FrameContext frame;
        PresentationTarget presentation_target{};
    };

    export struct VulkanPresentation {
        VulkanPresentation(WindowPlatform& platform, VulkanSurface& surface, VulkanGraphics& graphics, VulkanFrames& frames);
        ~VulkanPresentation();

        VulkanPresentation(const VulkanPresentation&)            = delete;
        VulkanPresentation(VulkanPresentation&&)                 = delete;
        VulkanPresentation& operator=(const VulkanPresentation&) = delete;
        VulkanPresentation& operator=(VulkanPresentation&&)      = delete;

        [[nodiscard]] std::optional<PresentedFrameContext> begin_frame();
        void present_frame();

        struct {
            WindowPlatform& platform;
            VulkanSurface& surface;
            VulkanGraphics& graphics;
            VulkanFrames& frames;
        } context;

        struct {
            vk::raii::SwapchainKHR swapchain{nullptr};
            std::vector<vk::Image> images{};
            std::vector<vk::raii::ImageView> views{};
            std::vector<vk::ImageLayout> layouts{};
            std::vector<vk::raii::Semaphore> image_available{};
            std::vector<vk::raii::Semaphore> render_finished{};
            vk::Extent2D extent{};
            std::uint32_t acquired_image_index{};
            bool acquired_suboptimal{};
        } presentation;

    private:
        void recreate_swapchain();
    };
} // namespace spectra
