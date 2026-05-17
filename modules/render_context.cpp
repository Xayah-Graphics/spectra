module;
#include <cstring>
#include <vulkan/vulkan_raii.hpp>

module render_context;
import std;

namespace xayah {
    std::vector<std::uint32_t> read_spirv(const std::filesystem::path& path) {
        std::ifstream file{path, std::ios::binary | std::ios::ate};
        if (!file) throw std::runtime_error(std::string{"Failed to open SPIR-V shader: "} + path.string());

        const std::streamoff byte_count = file.tellg();
        if (byte_count <= 0 || byte_count % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) throw std::runtime_error(std::string{"Invalid SPIR-V shader size: "} + path.string());

        std::vector<std::uint32_t> code(static_cast<std::size_t>(byte_count) / sizeof(std::uint32_t));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(code.data()), byte_count);
        if (!file) throw std::runtime_error(std::string{"Failed to read SPIR-V shader: "} + path.string());
        return code;
    }

    std::array<float, 16> multiply_matrix(const std::array<float, 16>& left, const std::array<float, 16>& right) {
        std::array<float, 16> result{};
        for (std::uint32_t column = 0; column < 4; ++column) {
            for (std::uint32_t row = 0; row < 4; ++row) {
                result[column * 4 + row] = left[0 * 4 + row] * right[column * 4 + 0] + left[1 * 4 + row] * right[column * 4 + 1] + left[2 * 4 + row] * right[column * 4 + 2] + left[3 * 4 + row] * right[column * 4 + 3];
            }
        }
        return result;
    }

    std::array<float, 16> inverse_affine_matrix(const std::array<float, 16>& matrix) {
        const float a00         = matrix[0];
        const float a01         = matrix[4];
        const float a02         = matrix[8];
        const float a10         = matrix[1];
        const float a11         = matrix[5];
        const float a12         = matrix[9];
        const float a20         = matrix[2];
        const float a21         = matrix[6];
        const float a22         = matrix[10];
        const float determinant = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
        if (std::abs(determinant) <= 0.000001f) throw std::runtime_error("Cannot invert singular transform");

        const float inv_det = 1.0f / determinant;
        const float b00     = (a11 * a22 - a12 * a21) * inv_det;
        const float b01     = (a02 * a21 - a01 * a22) * inv_det;
        const float b02     = (a01 * a12 - a02 * a11) * inv_det;
        const float b10     = (a12 * a20 - a10 * a22) * inv_det;
        const float b11     = (a00 * a22 - a02 * a20) * inv_det;
        const float b12     = (a02 * a10 - a00 * a12) * inv_det;
        const float b20     = (a10 * a21 - a11 * a20) * inv_det;
        const float b21     = (a01 * a20 - a00 * a21) * inv_det;
        const float b22     = (a00 * a11 - a01 * a10) * inv_det;
        const float tx      = matrix[12];
        const float ty      = matrix[13];
        const float tz      = matrix[14];

        return {
            b00,
            b10,
            b20,
            0.0f,
            b01,
            b11,
            b21,
            0.0f,
            b02,
            b12,
            b22,
            0.0f,
            -(b00 * tx + b01 * ty + b02 * tz),
            -(b10 * tx + b11 * ty + b12 * tz),
            -(b20 * tx + b21 * ty + b22 * tz),
            1.0f,
        };
    }

    std::array<float, 3> transform_point(const std::array<float, 16>& matrix, const std::array<float, 3>& point) {
        return {
            matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12],
            matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13],
            matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14],
        };
    }

    std::uint32_t memory_type_index(const vk::raii::PhysicalDevice& physical_device, const std::uint32_t memory_type_bits, const vk::MemoryPropertyFlags required_properties) {
        const vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            const bool supported = (memory_type_bits & (1u << index)) != 0;
            const bool matching  = (memory_properties.memoryTypes[index].propertyFlags & required_properties) == required_properties;
            if (supported && matching) return index;
        }
        throw std::runtime_error("No matching Vulkan memory type for buffer");
    }

    void ensure_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& memory, vk::DeviceSize& buffer_size, const vk::DeviceSize requested_size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags required_properties) {
        if (requested_size == 0) throw std::runtime_error("Cannot create zero-size buffer");
        if (*buffer && *memory && buffer_size == requested_size) return;

        const vk::BufferCreateInfo buffer_create_info{{}, requested_size, usage, vk::SharingMode::eExclusive};
        vk::raii::Buffer next_buffer{device, buffer_create_info};

        const vk::MemoryRequirements memory_requirements = next_buffer.getMemoryRequirements();
        const std::uint32_t type_index                   = memory_type_index(physical_device, memory_requirements.memoryTypeBits, required_properties);
        const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, type_index};
        vk::raii::DeviceMemory next_memory{device, allocate_info};
        next_buffer.bindMemory(*next_memory, 0);

        buffer      = std::move(next_buffer);
        memory      = std::move(next_memory);
        buffer_size = requested_size;
    }

    void write_buffer(const vk::raii::DeviceMemory& memory, const vk::DeviceSize buffer_size, const void* data, const vk::DeviceSize write_size) {
        if (!*memory) throw std::runtime_error("Cannot write unallocated buffer");
        if (write_size > buffer_size) throw std::runtime_error("Buffer write exceeds allocation size");
        void* mapped = memory.mapMemory(0, write_size);
        std::memcpy(mapped, data, static_cast<std::size_t>(write_size));
        memory.unmapMemory();
    }
} // namespace xayah
