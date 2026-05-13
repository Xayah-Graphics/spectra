module;
#include <vulkan/vulkan_raii.hpp>

export module scene_object;
import std;

namespace xayah {
    export struct Transform {
        std::array<float, 3> translation{0.0f, 0.0f, 0.0f};
        std::array<float, 3> rotation_degrees{0.0f, 0.0f, 0.0f};
        std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    };

    export enum class SceneObjectKind : std::uint32_t {
        volume    = 0,
        mesh      = 1,
        particles = 2,
    };

    export struct SceneObjectRef {
        SceneObjectKind kind{SceneObjectKind::volume};
        std::size_t index{0};
    };

    export struct BoundingBoxBounds {
        std::array<float, 3> minimum{};
        std::array<float, 3> maximum{};
    };

    export struct SceneRenderCreateContext {
        const vk::raii::Device* device{nullptr};
        vk::Format color_format{vk::Format::eUndefined};
        vk::Format depth_format{vk::Format::eUndefined};
        vk::ImageAspectFlags depth_aspect{};
        std::uint32_t frame_count{0};
    };

    export struct SceneRenderFrameContext {
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

    export template <typename Object>
    concept SceneObject = requires(Object object, const Object const_object) {
        { object.id } -> std::convertible_to<std::uint64_t>;
        { object.name } -> std::convertible_to<std::string>;
        { object.visible } -> std::convertible_to<bool>;
        { object.transform } -> std::convertible_to<Transform>;
        { const_object.kind() } -> std::same_as<SceneObjectKind>;
        { const_object.validate() } -> std::same_as<void>;
        { object.destroy_render_resources() } -> std::same_as<void>;
        { object.draw_inspector_ui() } -> std::same_as<void>;
        { const_object.bounds() } -> std::same_as<BoundingBoxBounds>;
        { const_object.make_snapshot() };
        { object.apply_snapshot(const_object.make_snapshot()) } -> std::same_as<void>;
    };

    export std::vector<std::uint32_t> read_spirv(const std::filesystem::path& path);
    export std::array<float, 16> multiply_matrix(const std::array<float, 16>& left, const std::array<float, 16>& right);
    export std::array<float, 16> transform_matrix(const Transform& transform);
    export std::array<float, 16> inverse_affine_matrix(const std::array<float, 16>& matrix);
    export std::array<float, 16> normal_matrix(const std::array<float, 16>& matrix);
    export std::array<float, 3> transform_point(const std::array<float, 16>& matrix, const std::array<float, 3>& point);
    export float maximum_scale(const Transform& transform);
    export std::uint32_t memory_type_index(const vk::raii::PhysicalDevice& physical_device, std::uint32_t memory_type_bits, vk::MemoryPropertyFlags required_properties);
    export void ensure_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& memory, vk::DeviceSize& buffer_size, vk::DeviceSize requested_size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags required_properties);
    export void write_buffer(const vk::raii::DeviceMemory& memory, vk::DeviceSize buffer_size, const void* data, vk::DeviceSize write_size);
} // namespace xayah
