export module spectra.runtime:resources;

import :graphics;
import std;
import vulkan;

namespace spectra {
    struct GpuAllocator;
    export struct GpuResources;

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
        std::uint32_t block_index{};
        vk::DeviceSize offset{};
        vk::DeviceSize size{};
    };

    export struct DescriptorHandle {
        std::uint32_t slot_index{};
        std::uint32_t reserved{};

        auto operator<=>(const DescriptorHandle&) const = default;
    };

    export struct GpuBuffer {
    private:
        friend GpuResources;

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

    export struct GpuImage {
    private:
        friend GpuResources;

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

    export struct GpuExternalTimelineSemaphore {
        vk::raii::Semaphore semaphore{nullptr};

        GpuExternalTimelineSemaphore()                                                   = default;
        GpuExternalTimelineSemaphore(GpuExternalTimelineSemaphore&&) noexcept            = default;
        GpuExternalTimelineSemaphore& operator=(GpuExternalTimelineSemaphore&&) noexcept = default;
        GpuExternalTimelineSemaphore(const GpuExternalTimelineSemaphore&)                = delete;
        GpuExternalTimelineSemaphore& operator=(const GpuExternalTimelineSemaphore&)     = delete;
    };

    export enum class ExternalHandleType : std::uint32_t {
        None,
        OpaqueWin32,
        OpaqueFileDescriptor,
    };

    export struct ExternalHandle {
        ExternalHandle() = default;
        ExternalHandle(ExternalHandleType type, std::uint64_t value) noexcept;
        ~ExternalHandle();
        ExternalHandle(ExternalHandle&& other) noexcept;
        ExternalHandle& operator=(ExternalHandle&& other) noexcept;
        ExternalHandle(const ExternalHandle&)            = delete;
        ExternalHandle& operator=(const ExternalHandle&) = delete;

        ExternalHandleType type{ExternalHandleType::None};
        std::uint64_t value{};
    };

    export struct GpuResources {
        explicit GpuResources(VulkanGraphics& graphics);
        ~GpuResources();

        GpuResources(const GpuResources&)            = delete;
        GpuResources(GpuResources&&)                 = delete;
        GpuResources& operator=(const GpuResources&) = delete;
        GpuResources& operator=(GpuResources&&)      = delete;

        [[nodiscard]] GpuBuffer create_buffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memory_properties, bool mapped);
        [[nodiscard]] GpuImage create_image_2d(vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor, std::uint32_t mip_levels = 1);

        [[nodiscard]] DescriptorHandle allocate_resource_descriptor();
        [[nodiscard]] DescriptorHandle allocate_sampler_descriptor();
        void reclaim_resource_descriptor(std::uint32_t slot_index) noexcept;
        void reclaim_sampler_descriptor(std::uint32_t slot_index) noexcept;
        void write_storage_image_descriptor(DescriptorHandle handle, const GpuImage& image, vk::ImageLayout layout);
        void write_sampled_image_descriptor(DescriptorHandle handle, const GpuImage& image, vk::ImageLayout layout);
        void write_buffer_descriptor(DescriptorHandle handle, vk::DescriptorType type, const GpuBuffer& buffer);
        void write_buffer_descriptor(DescriptorHandle handle, vk::DescriptorType type, vk::DeviceAddress address, vk::DeviceSize size);
        void write_sampler_descriptor(DescriptorHandle handle, const vk::SamplerCreateInfo& sampler);

        [[nodiscard]] GpuBuffer create_external_buffer(vk::DeviceSize size, vk::BufferUsageFlags usage);
        [[nodiscard]] ExternalHandle export_buffer_memory_handle(const GpuBuffer& buffer) const;
        [[nodiscard]] GpuExternalTimelineSemaphore create_external_simulation_timeline();
        [[nodiscard]] ExternalHandle export_timeline_semaphore_handle(const GpuExternalTimelineSemaphore& timeline) const;
        void wait_external_timeline(const GpuExternalTimelineSemaphore& timeline, std::uint64_t value) const;
        void signal_external_timeline(const GpuExternalTimelineSemaphore& timeline, std::uint64_t value) const;

        void submit_immediate(std::move_only_function<void(const vk::raii::CommandBuffer&)> record);
        void bind_descriptor_heaps(const vk::raii::CommandBuffer& command_buffer) const noexcept;
        void push_data(const vk::raii::CommandBuffer& command_buffer, std::span<const std::byte> data, std::uint32_t offset = 0) const noexcept;

        struct {
            VulkanGraphics& graphics;
        } context;

    private:
        std::unique_ptr<GpuAllocator> allocator{};

    public:
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

    private:
        void create_descriptor_heaps();

        vk::raii::CommandPool immediate_command_pool{nullptr};
    };
} // namespace spectra
