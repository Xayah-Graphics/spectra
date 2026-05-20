#pragma once

namespace xayah {
    void transition_image_layout(const vk::raii::CommandBuffer& command_buffer, vk::Image image, vk::ImageLayout old_layout, vk::ImageLayout new_layout, vk::ImageAspectFlags aspect, vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access, vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access);
    [[nodiscard]] std::uint32_t find_memory_type_index(const vk::raii::PhysicalDevice& physical_device, std::uint32_t memory_type_bits, vk::MemoryPropertyFlags required_properties);
    VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* callback_data, void*);
    [[nodiscard]] std::array<float, 16> identity_matrix_array();
    void validate_matrix_array(const std::array<float, 16>& values);
    [[nodiscard]] std::array<float, 16> multiply_matrix_arrays(const std::array<float, 16>& lhs, const std::array<float, 16>& rhs);
    [[nodiscard]] std::array<float, 16> translation_matrix_array(float x, float y, float z);
    [[nodiscard]] std::array<float, 16> rotation_x_matrix_array(float degrees);
    [[nodiscard]] std::array<float, 16> rotation_y_matrix_array(float degrees);
    [[nodiscard]] std::array<float, 16> matrix_array_from_transform(const pbrt::Transform& transform);
    [[nodiscard]] pbrt::Transform transform_from_matrix_array(const std::array<float, 16>& values);
    [[nodiscard]] std::array<float, 16> transpose_matrix_array(const std::array<float, 16>& matrix);
    [[nodiscard]] std::array<float, 16> inverse_matrix_array(const std::array<float, 16>& matrix);
    [[nodiscard]] std::array<float, 16> normal_from_local_matrix_array(const std::array<float, 16>& object_from_local);
    [[nodiscard]] std::array<float, 3> transform_point_array(const std::array<float, 16>& matrix, const std::array<float, 3>& point);
    [[nodiscard]] std::array<float, 16> raster_perspective_matrix(float fov_degrees, float aspect);
    [[nodiscard]] float raster_camera_fov_degrees(const SpectraScene& scene);
    [[nodiscard]] std::array<float, 16> raster_view_projection_matrix(const SpectraScene& scene, const std::array<float, 16>& camera_from_world, const std::array<float, 16>& moving_from_camera);
    [[nodiscard]] ImVec4 imgui_srgb(float red, float green, float blue, float alpha);
    void load_imgui_fonts();
    void apply_imgui_style(bool viewports);

    struct SpectraPbrtInteractiveSession {

        struct FrameResource {
            vk::raii::Buffer interop_buffer{nullptr};
            vk::raii::DeviceMemory interop_memory{nullptr};
            vk::DeviceSize interop_allocation_size{0};
            vk::DeviceSize interop_buffer_size{0};
            vk::raii::Semaphore cuda_complete_semaphore{nullptr};
            cudaExternalMemory_t cuda_external_memory{};
            cudaExternalSemaphore_t cuda_external_semaphore{};
            float* cuda_pixels{nullptr};

            vk::raii::DeviceMemory image_memory{nullptr};
            vk::raii::Image image{nullptr};
            vk::raii::ImageView image_view{nullptr};
            vk::raii::Sampler sampler{nullptr};
            VkDescriptorSet imgui_descriptor{VK_NULL_HANDLE};
            vk::ImageLayout image_layout{vk::ImageLayout::eUndefined};
        };

        struct RenderFrameResult {
            std::uint64_t sample_pixels{0};
            bool rendered_sample{false};
            bool reset_accumulation{false};
        };

        std::filesystem::path scene_path{};
        std::unique_ptr<pbrt::WavefrontPathIntegrator> integrator{};
        pbrt::Bounds2i pixel_bounds{};
        pbrt::Vector2i resolution{};
        vk::Format display_format{vk::Format::eR32G32B32A32Sfloat};
        pbrt::Transform render_from_camera{};
        pbrt::Transform camera_from_render{};
        pbrt::Transform camera_from_world{};
        pbrt::Float exposure{1.0f};
        pbrt::Float initial_move_scale{1.0f};
        int sample_index{0};
        int max_samples{0};
        int target_samples{0};
        bool reset_requested{false};
        std::uint32_t active_frame_index{0};
        const vk::raii::PhysicalDevice* physical_device{nullptr};
        const vk::raii::Device* device{nullptr};
        std::uint32_t frame_count{0};
        std::vector<FrameResource> frames{};

        SpectraPbrtInteractiveSession(const SpectraScene& spectra_scene, pbrt::BasicScene& backend_scene, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count);

        ~SpectraPbrtInteractiveSession() noexcept;

        void destroy_resources_noexcept() noexcept;

        SpectraPbrtInteractiveSession(const SpectraPbrtInteractiveSession& other)                = delete;
        SpectraPbrtInteractiveSession(SpectraPbrtInteractiveSession&& other) noexcept            = delete;
        SpectraPbrtInteractiveSession& operator=(const SpectraPbrtInteractiveSession& other)     = delete;
        SpectraPbrtInteractiveSession& operator=(SpectraPbrtInteractiveSession&& other) noexcept = delete;

        [[nodiscard]] int current_sample() const;

        [[nodiscard]] int sampler_sample_count() const;

        [[nodiscard]] int target_sample_count() const;

        [[nodiscard]] float current_exposure() const;

        [[nodiscard]] float camera_initial_move_scale() const;

        [[nodiscard]] std::array<int, 2> film_resolution() const;

        [[nodiscard]] std::array<float, 16> camera_from_world_matrix() const;

        [[nodiscard]] std::uint64_t film_pixel_count() const;

        [[nodiscard]] float completion_ratio() const;

        [[nodiscard]] VkDescriptorSet active_descriptor() const;

        [[nodiscard]] vk::Semaphore active_cuda_complete_semaphore() const;

        void set_target_sample_count(const int target_sample_count);

        void set_exposure(const float value);

        void request_reset_accumulation();

        void release_imgui_descriptors() noexcept;

        void create_imgui_descriptors();

        void destroy_frame_resources_noexcept() noexcept;

        void render_one_sample(const pbrt::Transform& camera_motion);

        void rerender_after_reset(const std::uint32_t frame_index, const pbrt::Transform& camera_motion);

        [[nodiscard]] RenderFrameResult render_frame(const std::uint32_t frame_index, const std::array<float, 16>& moving_from_camera_matrix);

        void record_copy(const vk::raii::CommandBuffer& command_buffer);

    private:
        void validate_cuda_vulkan_device(const vk::raii::PhysicalDevice& physical_device) const;

        void create_frame_resources(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count);

        void create_interop_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, FrameResource& frame, const vk::DeviceSize rgba_bytes);

        void create_cuda_complete_semaphore(const vk::raii::Device& device, FrameResource& frame);

        void create_display_image(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, FrameResource& frame, const vk::Format display_format);

    };

    struct SpectraVulkanRasterizer {

        struct FrameResource {
            vk::raii::DeviceMemory color_memory{nullptr};
            vk::raii::Image color_image{nullptr};
            vk::raii::ImageView color_image_view{nullptr};
            vk::raii::Sampler color_sampler{nullptr};
            vk::ImageLayout color_layout{vk::ImageLayout::eUndefined};
            vk::raii::DeviceMemory depth_memory{nullptr};
            vk::raii::Image depth_image{nullptr};
            vk::raii::ImageView depth_image_view{nullptr};
            vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
            VkDescriptorSet imgui_descriptor{VK_NULL_HANDLE};
        };

        struct BufferResource {
            vk::raii::Buffer buffer{nullptr};
            vk::raii::DeviceMemory memory{nullptr};
            vk::DeviceSize size{0};
        };

        struct SpectraRasterDrawGpu {
            std::array<float, 16> object_from_local{};
            std::array<float, 16> normal_from_local{};
            std::uint32_t material_index{0};
            std::array<std::uint32_t, 7> padding{};
        };

        struct SpectraRasterMaterialGpu {
            std::array<float, 4> base_color_roughness{};
        };

        struct SpectraRasterPushConstants {
            std::array<float, 16> view_projection{};
            std::uint32_t draw_index{0};
            std::array<std::uint32_t, 7> padding{};
        };
        static_assert(sizeof(SpectraRasterDrawGpu) == 160);
        static_assert(sizeof(SpectraRasterPushConstants) == 96);

        const SpectraScene* scene{nullptr};
        const SpectraRasterScene* raster_scene{nullptr};
        const vk::raii::PhysicalDevice* physical_device{nullptr};
        const vk::raii::Device* device{nullptr};
        const vk::raii::Queue* graphics_queue{nullptr};
        const vk::raii::CommandPool* command_pool{nullptr};
        vk::Extent2D extent{};
        vk::Format color_format{vk::Format::eR8G8B8A8Unorm};
        vk::Format depth_format{vk::Format::eD32Sfloat};
        std::uint32_t active_frame_index{0};
        std::size_t draw_count{0};
        std::size_t triangle_count{0};
        float initial_move_scale{1.0f};
        BufferResource vertex_buffer{};
        BufferResource index_buffer{};
        BufferResource draw_buffer{};
        BufferResource material_buffer{};
        vk::raii::DescriptorSetLayout descriptor_set_layout{nullptr};
        vk::raii::DescriptorPool descriptor_pool{nullptr};
        vk::raii::DescriptorSets descriptor_sets{nullptr};
        vk::raii::PipelineLayout pipeline_layout{nullptr};
        vk::raii::ShaderModule vertex_shader{nullptr};
        vk::raii::ShaderModule fragment_shader{nullptr};
        vk::raii::Pipeline pipeline{nullptr};
        std::vector<FrameResource> frames{};

        SpectraVulkanRasterizer(const SpectraScene& scene, const SpectraRasterScene& raster_scene, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const vk::raii::Queue& graphics_queue, const vk::raii::CommandPool& command_pool, const std::uint32_t frame_count);

        ~SpectraVulkanRasterizer() noexcept;

        SpectraVulkanRasterizer(const SpectraVulkanRasterizer& other)                = delete;
        SpectraVulkanRasterizer(SpectraVulkanRasterizer&& other) noexcept            = delete;
        SpectraVulkanRasterizer& operator=(const SpectraVulkanRasterizer& other)     = delete;
        SpectraVulkanRasterizer& operator=(SpectraVulkanRasterizer&& other) noexcept = delete;

        [[nodiscard]] VkDescriptorSet active_descriptor() const;

        [[nodiscard]] float camera_initial_move_scale() const;

        void render_frame(const std::uint32_t frame_index);

        void record_draw(const vk::raii::CommandBuffer& command_buffer, const std::array<float, 16>& camera_from_world, const std::array<float, 16>& moving_from_camera);

        void release_imgui_descriptors() noexcept;

        void create_imgui_descriptors();

        void destroy_resources_noexcept() noexcept;

        [[nodiscard]] std::size_t vertex_count() const;

        [[nodiscard]] std::size_t index_count() const;

        [[nodiscard]] std::size_t material_count() const;

        [[nodiscard]] std::size_t diagnostic_count() const;

    private:
        void validate_formats() const;

        [[nodiscard]] BufferResource create_buffer(const vk::DeviceSize size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags memory_properties) const;

        void submit_upload(const vk::raii::Buffer& staging_buffer, const vk::raii::Buffer& destination_buffer, const vk::DeviceSize size) const;

        template <typename T>
        [[nodiscard]] BufferResource upload_vector_buffer(const std::vector<T>& values, const vk::BufferUsageFlags usage) const;

        [[nodiscard]] std::vector<SpectraRasterMaterialGpu> build_gpu_materials() const;

        [[nodiscard]] std::vector<SpectraRasterDrawGpu> build_gpu_draws();

        void create_scene_buffers();

        void create_image_resource(FrameResource& frame, const vk::Format format, const vk::ImageUsageFlags usage, const vk::ImageAspectFlags aspect, vk::raii::DeviceMemory& memory, vk::raii::Image& image, vk::raii::ImageView& image_view) const;

        void create_frame_resources(const std::uint32_t frame_count);

        void create_descriptors();

        void create_pipeline();

        void record_geometry(const vk::raii::CommandBuffer& command_buffer, const std::array<float, 16>& camera_from_world, const std::array<float, 16>& moving_from_camera) const;

    };

} // namespace xayah
