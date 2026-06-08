module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vulkan/vulkan_raii.hpp>

export module spectra.rasterizer.frame;

import std;

namespace spectra::rasterizer {
    export struct FrameContext {
        std::uint32_t frame_index{};
        std::uint32_t image_index{};
        std::uint64_t frame_number{};
        double delta_seconds{};
    };

    export struct FrameResult {
        std::optional<vk::Semaphore> completion_semaphore{};
        bool close_requested{false};
        std::optional<std::string> window_detail{};
    };

    export template <typename Frame>
    concept FrameContextLike = requires(const Frame& frame) {
        { frame.frame_slot_index } -> std::convertible_to<std::uint32_t>;
        { frame.image_index } -> std::convertible_to<std::uint32_t>;
        { frame.frame_number } -> std::convertible_to<std::uint64_t>;
        { frame.delta_seconds } -> std::convertible_to<double>;
    };
} // namespace spectra::rasterizer
