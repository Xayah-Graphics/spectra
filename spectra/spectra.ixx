module;

#include <Windows.h>

#include <GLFW/glfw3.h>

export module spectra;

import vulkan;
import std;

namespace spectra {
    struct GpuAllocator;
    export struct Spectra;

    export struct DescriptorHandle {
        std::uint32_t index{};
        std::uint32_t reserved{};

        auto operator<=>(const DescriptorHandle&) const = default;
    };

    struct GpuAllocation {
        GpuAllocation() = default;
        ~GpuAllocation();
        GpuAllocation(GpuAllocation&& other) noexcept;
        GpuAllocation& operator=(GpuAllocation&& other) noexcept;
        GpuAllocation(const GpuAllocation&)            = delete;
        GpuAllocation& operator=(const GpuAllocation&) = delete;

    private:
        friend GpuAllocator;

        GpuAllocator* allocator{};
        std::uint32_t block{};
        vk::DeviceSize offset{};
        vk::DeviceSize size{};
    };

    export struct GpuBuffer {
    private:
        friend Spectra;

        GpuAllocation allocation{};
        vk::raii::DeviceMemory external_memory{nullptr};

    public:
        vk::raii::Buffer buffer{nullptr};
        vk::DeviceAddress address{};
        vk::DeviceSize size{};
        void* mapped{};

        GpuBuffer() = default;
        GpuBuffer(GpuBuffer&& other) noexcept;
        GpuBuffer& operator=(GpuBuffer&& other) noexcept;
        GpuBuffer(const GpuBuffer&)            = delete;
        GpuBuffer& operator=(const GpuBuffer&) = delete;
    };

    export struct GpuExternalTimeline {
        vk::raii::Semaphore semaphore{nullptr};

        GpuExternalTimeline()                                          = default;
        GpuExternalTimeline(GpuExternalTimeline&&) noexcept            = default;
        GpuExternalTimeline& operator=(GpuExternalTimeline&&) noexcept = default;
        GpuExternalTimeline(const GpuExternalTimeline&)                = delete;
        GpuExternalTimeline& operator=(const GpuExternalTimeline&)     = delete;
    };

    export struct GpuIdentity {
        std::array<std::uint8_t, 16> uuid{};
        std::array<std::uint8_t, 8> luid{};
        std::uint32_t node_mask{};
    };

    export struct GpuImage {
    private:
        friend Spectra;

        GpuAllocation allocation{};

    public:
        vk::raii::Image image{nullptr};
        vk::raii::ImageView view{nullptr};
        vk::Extent2D extent{};
        vk::Format format{};
        std::uint32_t mip_levels{1};

        GpuImage() = default;
        GpuImage(GpuImage&& other) noexcept;
        GpuImage& operator=(GpuImage&& other) noexcept;
        GpuImage(const GpuImage&)            = delete;
        GpuImage& operator=(const GpuImage&) = delete;
    };

    export struct PresentationTarget {
        vk::Image image{};
        vk::ImageView view{};
        vk::Extent2D extent{};
        vk::ImageLayout layout{};
    };

    export struct FrameContext {
        std::uint32_t index{};
        const vk::raii::CommandBuffer& command_buffer;
        PresentationTarget target{};
    };

    export struct GpuUploadSlice {
        vk::Buffer buffer{};
        vk::DeviceSize offset{};
        vk::DeviceSize size{};
    };

    export struct Spectra {
        static constexpr std::uint32_t frames_in_flight = 2;

        explicit Spectra(std::string_view application_name = "Spectra", vk::Extent2D initial_extent = {1920, 1080});
        ~Spectra();

        Spectra(const Spectra&)            = delete;
        Spectra(Spectra&&)                 = delete;
        Spectra& operator=(const Spectra&) = delete;
        Spectra& operator=(Spectra&&)      = delete;

        void poll_events() noexcept;
        void wait_events() noexcept;
        [[nodiscard]] bool take_close_request() noexcept;
        [[nodiscard]] std::vector<std::filesystem::path> take_dropped_paths() noexcept;

        void request_close() noexcept;

        [[nodiscard]] std::optional<FrameContext> begin_frame();
        [[nodiscard]] bool present_frame();
        void wait_idle() const;

        [[nodiscard]] GpuBuffer create_buffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memory_properties, bool mapped);
        [[nodiscard]] GpuBuffer create_external_buffer(vk::DeviceSize size, vk::BufferUsageFlags usage);
        [[nodiscard]] void* export_memory_handle(const GpuBuffer& buffer) const;
        [[nodiscard]] GpuExternalTimeline create_external_timeline();
        [[nodiscard]] void* export_semaphore_handle(const GpuExternalTimeline& timeline) const;
        [[nodiscard]] GpuIdentity identity() const noexcept;
        void wait_external_consumed(const GpuExternalTimeline& timeline, std::uint64_t value) const;
        void signal_external_host(const GpuExternalTimeline& timeline, std::uint64_t value) const;
        void wait_external(const GpuExternalTimeline& timeline, std::uint64_t value, vk::PipelineStageFlags2 stages);
        void signal_external(const GpuExternalTimeline& timeline, std::uint64_t value, vk::PipelineStageFlags2 stages);
        [[nodiscard]] GpuImage create_image_2d(vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor, std::uint32_t mip_levels = 1);
        [[nodiscard]] GpuUploadSlice stage_upload(std::span<const std::byte> data, vk::DeviceSize alignment = 16);
        void defer(std::move_only_function<void()> destruction);
        void immediate(std::move_only_function<void(const vk::raii::CommandBuffer&)> record);

        [[nodiscard]] DescriptorHandle allocate_resource_descriptor();
        [[nodiscard]] DescriptorHandle allocate_sampler_descriptor();
        void release_resource_descriptor(DescriptorHandle handle) noexcept;
        void release_sampler_descriptor(DescriptorHandle handle) noexcept;
        void write_storage_image(DescriptorHandle handle, const GpuImage& image, vk::ImageLayout layout);
        void write_sampled_image(DescriptorHandle handle, const GpuImage& image, vk::ImageLayout layout);
        void write_buffer(DescriptorHandle handle, vk::DescriptorType type, const GpuBuffer& buffer);
        void write_sampler(DescriptorHandle handle, const vk::SamplerCreateInfo& sampler);
        void bind_descriptor_heaps(const vk::raii::CommandBuffer& command_buffer) const noexcept;
        void push_data(const vk::raii::CommandBuffer& command_buffer, std::span<const std::byte> data, std::uint32_t offset = 0) const noexcept;

    private:
        struct GlfwLifetime {
            GlfwLifetime();
            ~GlfwLifetime();

            GlfwLifetime(const GlfwLifetime&)            = delete;
            GlfwLifetime(GlfwLifetime&&)                 = delete;
            GlfwLifetime& operator=(const GlfwLifetime&) = delete;
            GlfwLifetime& operator=(GlfwLifetime&&)      = delete;
        };

        static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
        void create_swapchain();

        struct {
            vk::raii::Context loader{};
            vk::raii::Instance instance{nullptr};
        } context;

        struct {
            GlfwLifetime glfw{};
            std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window{nullptr, glfwDestroyWindow};
            vk::raii::SurfaceKHR surface{nullptr};
            HWND native{};
            WNDPROC original_window_proc{};
            std::vector<std::filesystem::path> dropped_paths{};
            bool close_requested{};
        } platform;

        struct {
            vk::raii::PhysicalDevice physical_device{nullptr};
            vk::raii::Device logical_device{nullptr};
            vk::raii::Queue queue{nullptr};
            std::uint32_t queue_family_index{};
            vk::PhysicalDeviceAccelerationStructurePropertiesKHR acceleration_structure_properties{};
            vk::PhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_properties{};
            GpuIdentity identity{};
        } graphics;

        struct {
            std::unique_ptr<GpuAllocator> allocator{};
        } memory;

        struct {
            vk::raii::SwapchainKHR swapchain{nullptr};
            std::vector<vk::Image> images{};
            std::vector<vk::raii::ImageView> views{};
            std::vector<vk::ImageLayout> layouts{};
            std::vector<vk::raii::Semaphore> render_finished{};
            vk::Extent2D extent{};
            std::uint32_t acquired_image{};
            bool acquired_suboptimal{};
        } presentation;

        struct {
            GpuBuffer resource_heap{};
            GpuBuffer sampler_heap{};
            vk::PhysicalDeviceDescriptorHeapPropertiesEXT properties{};
            vk::DeviceSize resource_stride{};
            vk::DeviceSize sampler_stride{};
            std::uint32_t next_resource_index{};
            std::uint32_t next_sampler_index{};
            std::vector<std::uint32_t> free_resource_indices{};
            std::vector<std::uint32_t> free_sampler_indices{};
        } descriptors;

        struct {
            GpuBuffer buffer{};
            std::array<vk::DeviceSize, frames_in_flight> offsets{};
        } uploads;

        struct {
            vk::raii::CommandPool command_pool{nullptr};
        } immediate_submission;

        struct {
            vk::raii::CommandPool command_pool{nullptr};
            vk::raii::CommandBuffers command_buffers{nullptr};
            std::vector<vk::raii::Semaphore> image_available{};
            std::vector<vk::raii::Fence> fences{};
            std::array<std::vector<vk::SemaphoreSubmitInfo>, frames_in_flight> external_waits{};
            std::array<std::vector<vk::SemaphoreSubmitInfo>, frames_in_flight> external_signals{};
            std::uint32_t index{};
        } frames;

        struct {
            std::array<std::vector<std::move_only_function<void()>>, frames_in_flight> destructions{};
            std::array<std::vector<std::uint32_t>, frames_in_flight> resource_indices{};
            std::array<std::vector<std::uint32_t>, frames_in_flight> sampler_indices{};
        } deferred;

    public:
        const vk::raii::Device& device;
        const vk::PhysicalDeviceAccelerationStructurePropertiesKHR& acceleration_structure_properties;
        const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& ray_tracing_properties;
        const std::uint32_t& frame_index;
        GLFWwindow* window{};
        std::array<std::array<float, 4>, 2> drag_regions{};
    };
} // namespace spectra
