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

        {
            std::uint32_t glfw_extension_count = 0;
            const char** glfw_extensions       = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
            if (glfw_extensions == nullptr) throw std::runtime_error("Failed to get GLFW Vulkan instance extensions");
            std::vector<const char*> required_extensions{glfw_extensions, glfw_extensions + glfw_extension_count};
            required_extensions.push_back(vk::EXTDebugUtilsExtensionName);

            constexpr std::array<const char*, 1> required_layers{"VK_LAYER_KHRONOS_validation"};

            const std::vector<vk::LayerProperties> available_layers = this->context.context.enumerateInstanceLayerProperties();
            for (const char* required_layer : required_layers) {
                if (const auto found = std::ranges::find(available_layers, std::string_view{required_layer}, [](const vk::LayerProperties& layer) { return std::string_view{layer.layerName.data()}; }); found == available_layers.end()) throw std::runtime_error(std::string{"Required Vulkan layer not supported: "} + required_layer);
            }
            const std::vector<vk::ExtensionProperties> available_extensions = this->context.context.enumerateInstanceExtensionProperties();
            for (const char* required_extension : required_extensions) {
                if (const auto found = std::ranges::find(available_extensions, std::string_view{required_extension}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); found == available_extensions.end()) throw std::runtime_error(std::string{"Required Vulkan instance extension not supported: "} + required_extension);
            }

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
                static_cast<std::uint32_t>(required_layers.size()),
                required_layers.data(),
                static_cast<std::uint32_t>(required_extensions.size()),
                required_extensions.data(),
            };
            this->context.instance = vk::raii::Instance{this->context.context, instance_create_info};
        }

        std::print("Hello Spectra");
    }
} // namespace xayah
