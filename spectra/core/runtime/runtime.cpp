module spectra.runtime;

import std;
import vulkan;

namespace spectra::runtime {
    VulkanRuntime::VulkanRuntime(VulkanInstance& instance, const vk::SurfaceKHR surface) : device(instance, surface), resources(this->device), frames(this->device, this->resources) {}

    VulkanRuntime::~VulkanRuntime() {
        static_cast<void>(this->device.logical.getDispatcher()->vkDeviceWaitIdle(*this->device.logical));
    }

    std::vector<std::uint32_t> load_spirv(const std::filesystem::path& path) {
        std::ifstream input{path, std::ios::binary | std::ios::ate};
        if (!input) throw std::runtime_error(std::format("Cannot open Spectra shader: {}", path.string()));
        const std::streamsize byte_count = input.tellg();
        if (byte_count <= 0 || byte_count % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0) throw std::runtime_error(std::format("Spectra shader has an invalid SPIR-V size: {}", path.string()));
        std::vector<std::uint32_t> words(static_cast<std::size_t>(byte_count) / sizeof(std::uint32_t));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(words.data()), byte_count);
        if (!input) throw std::runtime_error(std::format("Cannot read Spectra shader: {}", path.string()));
        return words;
    }

    void record_default_graphics_state(const vk::raii::CommandBuffer& command_buffer) {
        command_buffer.setRasterizerDiscardEnable(vk::False);
        command_buffer.setPolygonModeEXT(vk::PolygonMode::eFill);
        command_buffer.setRasterizationSamplesEXT(vk::SampleCountFlagBits::e1);
        command_buffer.setAlphaToCoverageEnableEXT(vk::False);
        command_buffer.setDepthBiasEnable(vk::False);
        command_buffer.setStencilTestEnable(vk::False);
        constexpr vk::SampleMask sample_mask = 1;
        command_buffer.setSampleMaskEXT(vk::SampleCountFlagBits::e1, sample_mask);
    }
} // namespace spectra::runtime
