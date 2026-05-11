module;
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan_raii.hpp>
module spectra;
import std;

namespace xayah {
    Spectra::Spectra(const std::string_view& app_name, const std::string_view& engine_name) {
        if (!glfwInit()) throw std::runtime_error("Failed to initialize GLFW");
        std::print("Hello Spectra");
    }
} // namespace xayah
