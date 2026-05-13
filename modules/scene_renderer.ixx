module;
#include <vulkan/vulkan_raii.hpp>

export module scene_renderer;
export import scene;
import std;

namespace xayah {
    export class SceneRenderer {
    public:
        SceneRenderer();
        ~SceneRenderer() noexcept;

        SceneRenderer(const SceneRenderer& other)                = delete;
        SceneRenderer(SceneRenderer&& other) noexcept            = delete;
        SceneRenderer& operator=(const SceneRenderer& other)     = delete;
        SceneRenderer& operator=(SceneRenderer&& other) noexcept = delete;

        void create(const vk::raii::Device& device, vk::Format color_format, vk::Format depth_format, vk::ImageAspectFlags depth_aspect, std::uint32_t frame_count, const Scene& scene);
        void destroy() noexcept;
        void recreate(const vk::raii::Device& device, vk::Format color_format, vk::Format depth_format, vk::ImageAspectFlags depth_aspect, std::uint32_t frame_count, const Scene& scene);
        void render(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_index, std::uint32_t frame_count, const std::array<float, 16>& view_projection, const std::array<float, 3>& camera_position, const std::array<float, 3>& camera_right, const std::array<float, 3>& camera_up, Scene& scene);
        [[nodiscard]] bool active() const;

    private:
        void render_meshes(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_index, std::uint32_t frame_count, const std::array<float, 16>& view_projection, Scene& scene);
        void render_particles(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_index, std::uint32_t frame_count, const std::array<float, 16>& view_projection, const std::array<float, 3>& camera_right, const std::array<float, 3>& camera_up, Scene& scene);
        void render_volumes(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_index, std::uint32_t frame_count, const std::array<float, 16>& view_projection, const std::array<float, 3>& camera_position, Scene& scene);
        void render_bounding_boxes(const vk::raii::CommandBuffer& command_buffer, const std::array<float, 16>& view_projection, const Scene& scene);
        void create_bounding_box_renderer(const vk::raii::Device& device, vk::Format color_format, vk::Format depth_format, vk::ImageAspectFlags depth_aspect);
        void destroy_bounding_box_renderer() noexcept;
        void create_mesh_renderer(const vk::raii::Device& device, vk::Format color_format, vk::Format depth_format, vk::ImageAspectFlags depth_aspect, std::uint32_t frame_count, const Scene& scene);
        void destroy_mesh_renderer() noexcept;
        void create_particles_renderer(const vk::raii::Device& device, vk::Format color_format, vk::Format depth_format, vk::ImageAspectFlags depth_aspect, std::uint32_t frame_count, const Scene& scene);
        void destroy_particles_renderer() noexcept;
        void create_volume_renderer(const vk::raii::Device& device, vk::Format color_format, vk::Format depth_format, vk::ImageAspectFlags depth_aspect, std::uint32_t frame_count, const Scene& scene);
        void destroy_volume_renderer() noexcept;

        struct MeshDrawResources {
            vk::raii::Buffer vertex_buffer{nullptr};
            vk::raii::DeviceMemory vertex_memory{nullptr};
            vk::DeviceSize vertex_size{0};
            vk::raii::Buffer parameters_buffer{nullptr};
            vk::raii::DeviceMemory parameters_memory{nullptr};
            vk::DeviceSize parameters_size{0};
        };

        struct ParticleDrawResources {
            vk::raii::Buffer vertex_buffer{nullptr};
            vk::raii::DeviceMemory vertex_memory{nullptr};
            vk::DeviceSize vertex_size{0};
        };

        struct VolumeDrawResources {
            vk::raii::Buffer x_data_buffer{nullptr};
            vk::raii::DeviceMemory x_data_memory{nullptr};
            vk::DeviceSize x_data_size{0};
            vk::raii::Buffer y_data_buffer{nullptr};
            vk::raii::DeviceMemory y_data_memory{nullptr};
            vk::DeviceSize y_data_size{0};
            vk::raii::Buffer z_data_buffer{nullptr};
            vk::raii::DeviceMemory z_data_memory{nullptr};
            vk::DeviceSize z_data_size{0};
            vk::raii::Buffer parameters_buffer{nullptr};
            vk::raii::DeviceMemory parameters_memory{nullptr};
            vk::DeviceSize parameters_size{0};
        };

        struct {
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::Pipeline pipeline{nullptr};
        } bounding_box_renderer{};

        struct {
            vk::raii::DescriptorSetLayout descriptor_layout{nullptr};
            vk::raii::DescriptorPool descriptor_pool{nullptr};
            vk::raii::DescriptorSets descriptor_sets{nullptr};
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::Pipeline surface_pipeline{nullptr};
            vk::raii::Pipeline wireframe_pipeline{nullptr};
            std::vector<MeshDrawResources> frame_resources{};
        } mesh_renderer{};

        struct {
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::Pipeline pipeline{nullptr};
            std::vector<ParticleDrawResources> frame_resources{};
        } particles_renderer{};

        struct {
            vk::raii::DescriptorSetLayout descriptor_layout{nullptr};
            vk::raii::DescriptorPool descriptor_pool{nullptr};
            vk::raii::DescriptorSets descriptor_sets{nullptr};
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::Pipeline pipeline{nullptr};
            std::vector<VolumeDrawResources> frame_resources{};
        } volume_renderer{};
    };
} // namespace xayah
