module;
#include <vulkan/vulkan_raii.hpp>

export module render_context;
import std;

namespace xayah {
    export struct BoundingBoxBounds {
        std::array<float, 3> minimum{};
        std::array<float, 3> maximum{};
    };

    export struct RenderCreateContext {
        const vk::raii::Device* device{nullptr};
        vk::Format color_format{vk::Format::eUndefined};
        vk::Format depth_format{vk::Format::eUndefined};
        vk::ImageAspectFlags depth_aspect{};
        std::uint32_t frame_count{0};
    };

    export struct RenderFrameContext {
        const vk::raii::PhysicalDevice* physical_device{nullptr};
        const vk::raii::Device* device{nullptr};
        const vk::raii::CommandBuffer* command_buffer{nullptr};
        std::uint32_t frame_index{0};
        std::uint32_t frame_count{0};
        std::array<float, 16> view_projection{};
        std::array<float, 3> camera_position{};
        std::array<float, 3> camera_right{};
        std::array<float, 3> camera_up{};
    };

    export std::vector<std::uint32_t> read_spirv(const std::filesystem::path& path);
    export std::array<float, 16> multiply_matrix(const std::array<float, 16>& left, const std::array<float, 16>& right);
    export std::array<float, 16> inverse_affine_matrix(const std::array<float, 16>& matrix);
    export std::array<float, 3> transform_point(const std::array<float, 16>& matrix, const std::array<float, 3>& point);
    export std::uint32_t memory_type_index(const vk::raii::PhysicalDevice& physical_device, std::uint32_t memory_type_bits, vk::MemoryPropertyFlags required_properties);
    export void ensure_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& memory, vk::DeviceSize& buffer_size, vk::DeviceSize requested_size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags required_properties);
    export void write_buffer(const vk::raii::DeviceMemory& memory, vk::DeviceSize buffer_size, const void* data, vk::DeviceSize write_size);
} // namespace xayah
