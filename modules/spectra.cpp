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
        std::uint32_t glfw_extension_count = 0;
        const char** glfw_extensions       = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
        if (glfw_extensions == nullptr) throw std::runtime_error("Failed to get GLFW Vulkan instance extensions");

        {
            const vk::ApplicationInfo application_info{
                std::string{app_name}.c_str(),
                VK_MAKE_VERSION(1, 0, 0),
                std::string{engine_name}.c_str(),
                VK_MAKE_VERSION(1, 0, 0),
                vk::ApiVersion14,
            };
            const vk::InstanceCreateInfo instance_create_info{
                {},
                &application_info,
                0,
                nullptr,
                glfw_extension_count,
                glfw_extensions,
            };
            this->context.instance = vk::raii::Instance{this->context.context, instance_create_info};
        }

        std::print("Hello Spectra");
    }
} // namespace xayah
