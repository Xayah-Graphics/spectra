export module spectra.runtime.resources;

import spectra.runtime.graphics;
import std;
import vulkan;

namespace spectra {
    export struct GpuResources;

    export [[nodiscard]] constexpr vk::DeviceSize align_device_size(const vk::DeviceSize value, const vk::DeviceSize alignment) noexcept {
        return (value + alignment - 1u) & ~(alignment - 1u);
    }

    struct GpuAllocation {
        GpuAllocation() = default;
        ~GpuAllocation();
        GpuAllocation(GpuAllocation&& other) noexcept;
        GpuAllocation& operator=(GpuAllocation&& other) noexcept;
        GpuAllocation(const GpuAllocation&)            = delete;
        GpuAllocation& operator=(const GpuAllocation&) = delete;

    private:
        friend GpuResources;

        GpuResources* resources{};
        std::uint32_t block_index{};
        vk::DeviceSize offset{};
        vk::DeviceSize size{};
    };

    export struct DescriptorHandle {
        std::uint32_t slot_index{std::numeric_limits<std::uint32_t>::max()};
        std::uint32_t reserved{};

        [[nodiscard]] explicit operator bool() const noexcept {
            return this->slot_index != std::numeric_limits<std::uint32_t>::max();
        }
    };

    export struct GpuBuffer {
        vk::raii::Buffer buffer{nullptr};
        vk::DeviceAddress address{};
        vk::DeviceSize size{};
        vk::DeviceSize external_memory_size{};
        void* mapped{};

        GpuBuffer() = default;
        ~GpuBuffer();
        GpuBuffer(GpuBuffer&& other) noexcept;
        GpuBuffer& operator=(GpuBuffer&& other) noexcept;
        GpuBuffer(const GpuBuffer&)            = delete;
        GpuBuffer& operator=(const GpuBuffer&) = delete;

    private:
        friend GpuResources;

        GpuAllocation allocation{};
        vk::raii::DeviceMemory external_memory{nullptr};
    };

    export struct GpuImage {
        vk::raii::Image image{nullptr};
        vk::raii::ImageView view{nullptr};
        vk::Extent2D extent{};
        vk::Format format{};
        vk::ImageAspectFlags aspect{};
        std::uint32_t mip_levels{1};

        GpuImage() = default;
        ~GpuImage();
        GpuImage(GpuImage&& other) noexcept;
        GpuImage& operator=(GpuImage&& other) noexcept;
        GpuImage(const GpuImage&)            = delete;
        GpuImage& operator=(const GpuImage&) = delete;

    private:
        friend GpuResources;

        GpuAllocation allocation{};
    };

    export struct GpuExternalTimelineSemaphore {
        vk::raii::Semaphore semaphore{nullptr};

        GpuExternalTimelineSemaphore()                                                   = default;
        GpuExternalTimelineSemaphore(GpuExternalTimelineSemaphore&&) noexcept            = default;
        GpuExternalTimelineSemaphore& operator=(GpuExternalTimelineSemaphore&&) noexcept = default;
        GpuExternalTimelineSemaphore(const GpuExternalTimelineSemaphore&)                = delete;
        GpuExternalTimelineSemaphore& operator=(const GpuExternalTimelineSemaphore&)     = delete;
    };

    export struct ExternalHandle {
        ExternalHandle() = default;
        explicit ExternalHandle(std::uint64_t value) noexcept;
        ExternalHandle(ExternalHandle&& other) noexcept;
        ~ExternalHandle();
        ExternalHandle& operator=(ExternalHandle&& other) noexcept;
        ExternalHandle(const ExternalHandle&)            = delete;
        ExternalHandle& operator=(const ExternalHandle&) = delete;

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
        [[nodiscard]] DescriptorHandle acquire_resource_descriptor();
        [[nodiscard]] DescriptorHandle acquire_sampler_descriptor();
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

    private:
        friend GpuAllocation;

        enum class ResourceClass : std::uint8_t {
            Buffer,
            OptimalImage,
        };

        struct AllocationRequest {
            vk::MemoryRequirements requirements{};
            vk::MemoryPropertyFlags properties{};
            ResourceClass resource_class{ResourceClass::Buffer};
            bool device_address{};
            bool requires_dedicated{};
            bool prefers_dedicated{};
            vk::Buffer dedicated_buffer{};
            vk::Image dedicated_image{};
        };

        struct AllocationRange {
            vk::DeviceSize offset{};
            vk::DeviceSize size{};
        };

        struct AllocationBlock {
            vk::raii::DeviceMemory memory{nullptr};
            vk::DeviceSize size{};
            std::uint32_t memory_type{};
            bool device_address{};
            bool dedicated{};
            ResourceClass resource_class{ResourceClass::Buffer};
            void* mapped{};
            std::vector<AllocationRange> free_ranges{};

            ~AllocationBlock();
        };

        [[nodiscard]] GpuAllocation allocate(const AllocationRequest& request);
        void release_allocation(std::uint32_t block_index, vk::DeviceSize offset, vk::DeviceSize size) noexcept;
        [[nodiscard]] std::uint32_t find_memory_type(std::uint32_t type_bits, vk::MemoryPropertyFlags properties) const;
        [[nodiscard]] std::optional<vk::DeviceSize> allocate_range(AllocationBlock& block, vk::DeviceSize size, vk::DeviceSize alignment);
        [[nodiscard]] GpuAllocation make_allocation(std::uint32_t block_index, vk::DeviceSize offset, vk::DeviceSize size) noexcept;

        struct {
            VulkanGraphics& graphics;
        } context;
        struct {
            std::vector<std::unique_ptr<AllocationBlock>> blocks{};
        } allocation;
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
        vk::raii::CommandPool immediate_command_pool{nullptr};
        void create_descriptor_heaps();
    };
} // namespace spectra
