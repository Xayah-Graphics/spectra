module;
#include <vulkan/vulkan_raii.hpp>

module scene_object;
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

    namespace {
        std::array<float, 16> multiply_transform_matrix(const std::array<float, 16>& left, const std::array<float, 16>& right) {
            std::array<float, 16> result{};
            for (std::uint32_t row = 0; row < 4; ++row) {
                const std::uint32_t base = row * 4;
                result[base + 0]         = left[base + 0] * right[0] + left[base + 1] * right[4] + left[base + 2] * right[8] + left[base + 3] * right[12];
                result[base + 1]         = left[base + 0] * right[1] + left[base + 1] * right[5] + left[base + 2] * right[9] + left[base + 3] * right[13];
                result[base + 2]         = left[base + 0] * right[2] + left[base + 1] * right[6] + left[base + 2] * right[10] + left[base + 3] * right[14];
                result[base + 3]         = left[base + 0] * right[3] + left[base + 1] * right[7] + left[base + 2] * right[11] + left[base + 3] * right[15];
            }
            return result;
        }
    } // namespace

    std::array<float, 16> transform_matrix(const Transform& transform) {
        constexpr float degrees_to_radians = 0.017453292519943295769f;
        const float x                      = transform.rotation_degrees[0] * degrees_to_radians;
        const float y                      = transform.rotation_degrees[1] * degrees_to_radians;
        const float z                      = transform.rotation_degrees[2] * degrees_to_radians;
        const float cx                     = std::cos(x);
        const float sx                     = std::sin(x);
        const float cy                     = std::cos(y);
        const float sy                     = std::sin(y);
        const float cz                     = std::cos(z);
        const float sz                     = std::sin(z);

        const std::array rotation_x{
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            cx,
            sx,
            0.0f,
            0.0f,
            -sx,
            cx,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        const std::array rotation_y{
            cy,
            0.0f,
            -sy,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            sy,
            0.0f,
            cy,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        const std::array rotation_z{
            cz,
            sz,
            0.0f,
            0.0f,
            -sz,
            cz,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };

        std::array<float, 16> matrix = multiply_transform_matrix(multiply_transform_matrix(rotation_x, rotation_y), rotation_z);
        for (std::uint32_t row = 0; row < 3; ++row) {
            matrix[0 * 4 + row] *= transform.scale[0];
            matrix[1 * 4 + row] *= transform.scale[1];
            matrix[2 * 4 + row] *= transform.scale[2];
        }
        matrix[12] = transform.translation[0];
        matrix[13] = transform.translation[1];
        matrix[14] = transform.translation[2];
        matrix[15] = 1.0f;
        return matrix;
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
        if (std::abs(determinant) <= 0.000001f) throw std::runtime_error("Cannot invert singular scene object transform");

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

    std::array<float, 16> normal_matrix(const std::array<float, 16>& matrix) {
        const std::array<float, 16> inverse = inverse_affine_matrix(matrix);
        return {
            inverse[0],
            inverse[4],
            inverse[8],
            0.0f,
            inverse[1],
            inverse[5],
            inverse[9],
            0.0f,
            inverse[2],
            inverse[6],
            inverse[10],
            0.0f,
            0.0f,
            0.0f,
            0.0f,
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

    float maximum_scale(const Transform& transform) {
        return std::max(transform.scale[0], std::max(transform.scale[1], transform.scale[2]));
    }

    std::uint32_t memory_type_index(const vk::raii::PhysicalDevice& physical_device, const std::uint32_t memory_type_bits, const vk::MemoryPropertyFlags required_properties) {
        const vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            const bool supported = (memory_type_bits & (1u << index)) != 0;
            const bool matching  = (memory_properties.memoryTypes[index].propertyFlags & required_properties) == required_properties;
            if (supported && matching) return index;
        }
        throw std::runtime_error("No matching memory type for buffer");
    }

    void ensure_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& memory, vk::DeviceSize& buffer_size, const vk::DeviceSize requested_size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags required_properties) {
        if (requested_size == 0) throw std::runtime_error("Cannot create zero-size buffer");
        if (*buffer && *memory && buffer_size == requested_size) return;

        vk::raii::Buffer next_buffer{nullptr};
        vk::raii::DeviceMemory next_memory{nullptr};
        const vk::BufferCreateInfo buffer_create_info{{}, requested_size, usage, vk::SharingMode::eExclusive};
        next_buffer = vk::raii::Buffer{device, buffer_create_info};

        const vk::MemoryRequirements memory_requirements = next_buffer.getMemoryRequirements();
        const std::uint32_t type_index                   = memory_type_index(physical_device, memory_requirements.memoryTypeBits, required_properties);
        const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, type_index};
        next_memory = vk::raii::DeviceMemory{device, allocate_info};
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
