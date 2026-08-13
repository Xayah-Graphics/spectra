module;

#if defined(_WIN32)
#include <Windows.h>
#else
#include <unistd.h>
#endif

module spectra.runtime.resources;

import std;
import vulkan;

namespace spectra {
    namespace {
        constexpr vk::DeviceSize resource_heap_size       = 16u * 1024u * 1024u;
        constexpr vk::DeviceSize sampler_heap_size        = 1u * 1024u * 1024u;
        constexpr vk::DeviceSize device_memory_block_size = 256u * 1024u * 1024u;
        constexpr vk::DeviceSize host_memory_block_size   = 64u * 1024u * 1024u;

        [[nodiscard]] constexpr vk::DeviceSize align_down(const vk::DeviceSize value, const vk::DeviceSize alignment) noexcept {
            return value & ~(alignment - 1u);
        }

    } // namespace

    GpuAllocation::~GpuAllocation() {
        if (this->resources) this->resources->release_allocation(this->block_index, this->offset, this->size);
    }

    GpuAllocation::GpuAllocation(GpuAllocation&& other) noexcept : resources(std::exchange(other.resources, nullptr)), block_index(std::exchange(other.block_index, 0)), offset(std::exchange(other.offset, 0)), size(std::exchange(other.size, 0)) {}

    GpuAllocation& GpuAllocation::operator=(GpuAllocation&& other) noexcept {
        if (this == &other) return *this;
        if (this->resources) this->resources->release_allocation(this->block_index, this->offset, this->size);
        this->resources   = std::exchange(other.resources, nullptr);
        this->block_index = std::exchange(other.block_index, 0);
        this->offset      = std::exchange(other.offset, 0);
        this->size        = std::exchange(other.size, 0);
        return *this;
    }

    GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept : buffer(std::move(other.buffer)), address(std::exchange(other.address, 0)), size(std::exchange(other.size, 0)), mapped(std::exchange(other.mapped, nullptr)), allocation(std::move(other.allocation)), external_memory(std::move(other.external_memory)) {}

    GpuBuffer::~GpuBuffer() {
        this->buffer = nullptr;
    }

    GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
        if (this == &other) return *this;
        this->buffer          = nullptr;
        this->allocation      = std::move(other.allocation);
        this->external_memory = std::move(other.external_memory);
        this->buffer          = std::move(other.buffer);
        this->address         = std::exchange(other.address, 0);
        this->size            = std::exchange(other.size, 0);
        this->mapped          = std::exchange(other.mapped, nullptr);
        return *this;
    }

    GpuImage::GpuImage(GpuImage&& other) noexcept : image(std::move(other.image)), view(std::move(other.view)), extent(std::exchange(other.extent, {})), format(std::exchange(other.format, {})), aspect(std::exchange(other.aspect, {})), mip_levels(std::exchange(other.mip_levels, 1)), allocation(std::move(other.allocation)) {}

    GpuImage::~GpuImage() {
        this->view  = nullptr;
        this->image = nullptr;
    }

    GpuImage& GpuImage::operator=(GpuImage&& other) noexcept {
        if (this == &other) return *this;
        this->view       = nullptr;
        this->image      = nullptr;
        this->allocation = std::move(other.allocation);
        this->image      = std::move(other.image);
        this->view       = std::move(other.view);
        this->extent     = std::exchange(other.extent, {});
        this->format     = std::exchange(other.format, {});
        this->aspect     = std::exchange(other.aspect, {});
        this->mip_levels = std::exchange(other.mip_levels, 1);
        return *this;
    }


    ExternalHandle::ExternalHandle(const ExternalHandleType type, const std::uint64_t value) noexcept : type{type}, value{value} {}

    ExternalHandle::ExternalHandle(ExternalHandle&& other) noexcept : type{std::exchange(other.type, ExternalHandleType::None)}, value{std::exchange(other.value, 0)} {}

    ExternalHandle::~ExternalHandle() {
        if (this->type == ExternalHandleType::None) return;
#if defined(_WIN32)
        CloseHandle(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(this->value)));
#else
        close(static_cast<int>(this->value));
#endif
    }

    ExternalHandle& ExternalHandle::operator=(ExternalHandle&& other) noexcept {
        if (this == &other) return *this;
        ExternalHandle replacement{std::move(other)};
        std::swap(this->type, replacement.type);
        std::swap(this->value, replacement.value);
        return *this;
    }

    std::uint64_t ExternalHandle::release() noexcept {
        this->type = ExternalHandleType::None;
        return std::exchange(this->value, 0);
    }

    GpuResources::GpuResources(VulkanGraphics& graphics) : context{graphics} {
        this->descriptors.properties = graphics.descriptor_heap_properties;
        this->create_descriptor_heaps();
        this->immediate_command_pool = vk::raii::CommandPool{
            graphics.device,
            vk::CommandPoolCreateInfo{
                vk::CommandPoolCreateFlagBits::eTransient,
                graphics.queue_family_index,
            },
        };
    }

    GpuResources::~GpuResources() = default;

    GpuBuffer GpuResources::create_buffer(const vk::DeviceSize size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags memory_properties, const bool mapped) {
        const vk::raii::Device& device = this->context.graphics.device;
        GpuBuffer result{};
        result.size   = size;
        result.buffer = vk::raii::Buffer{
            device,
            vk::BufferCreateInfo{{}, size, usage, vk::SharingMode::eExclusive},
        };
        const auto requirements = device.getBufferMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(vk::BufferMemoryRequirementsInfo2{*result.buffer});
        result.allocation       = this->allocate({
                  .requirements       = requirements.get<vk::MemoryRequirements2>().memoryRequirements,
                  .properties         = memory_properties,
                  .resource_class     = ResourceClass::Buffer,
                  .device_address     = static_cast<bool>(usage & vk::BufferUsageFlagBits::eShaderDeviceAddress),
                  .requires_dedicated = static_cast<bool>(requirements.get<vk::MemoryDedicatedRequirements>().requiresDedicatedAllocation),
                  .prefers_dedicated  = static_cast<bool>(requirements.get<vk::MemoryDedicatedRequirements>().prefersDedicatedAllocation),
                  .dedicated_buffer   = *result.buffer,
        });
        result.buffer.bindMemory(*this->allocation.blocks[result.allocation.block_index]->memory, result.allocation.offset);
        if (static_cast<bool>(usage & vk::BufferUsageFlagBits::eShaderDeviceAddress)) result.address = device.getBufferAddress(vk::BufferDeviceAddressInfo{*result.buffer});
        if (mapped) {
            void* const block_mapping = this->allocation.blocks[result.allocation.block_index]->mapped;
            result.mapped             = block_mapping ? static_cast<std::byte*>(block_mapping) + result.allocation.offset : nullptr;
            if (!result.mapped) throw std::runtime_error("Spectra cannot map a GPU buffer allocated from non-host-visible memory");
        }
        return result;
    }

    GpuImage GpuResources::create_image_2d(const vk::Extent2D extent, const vk::Format format, const vk::ImageUsageFlags usage, const vk::ImageAspectFlags aspect, const std::uint32_t mip_levels) {
        const vk::raii::Device& device = this->context.graphics.device;
        GpuImage result{};
        result.extent     = extent;
        result.format     = format;
        result.aspect     = aspect;
        result.mip_levels = mip_levels;
        const vk::ImageCreateInfo image_create_info{
            {},
            vk::ImageType::e2D,
            format,
            vk::Extent3D{extent.width, extent.height, 1},
            mip_levels,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            usage,
            vk::SharingMode::eExclusive,
            0,
            nullptr,
            vk::ImageLayout::eUndefined,
        };
        result.image            = vk::raii::Image{device, image_create_info};
        const auto requirements = device.getImageMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(vk::ImageMemoryRequirementsInfo2{*result.image});
        result.allocation       = this->allocate({
                  .requirements       = requirements.get<vk::MemoryRequirements2>().memoryRequirements,
                  .properties         = vk::MemoryPropertyFlagBits::eDeviceLocal,
                  .resource_class     = ResourceClass::OptimalImage,
                  .requires_dedicated = static_cast<bool>(requirements.get<vk::MemoryDedicatedRequirements>().requiresDedicatedAllocation),
                  .prefers_dedicated  = static_cast<bool>(requirements.get<vk::MemoryDedicatedRequirements>().prefersDedicatedAllocation),
                  .dedicated_image    = *result.image,
        });
        result.image.bindMemory(*this->allocation.blocks[result.allocation.block_index]->memory, result.allocation.offset);
        result.view = vk::raii::ImageView{
            device,
            vk::ImageViewCreateInfo{
                {},
                *result.image,
                vk::ImageViewType::e2D,
                format,
                {},
                {aspect, 0, mip_levels, 0, 1},
            },
        };
        return result;
    }

    DescriptorHandle GpuResources::acquire_resource_descriptor() {
        if (!this->descriptors.free_resource_indices.empty()) {
            const DescriptorHandle handle{
                this->descriptors.free_resource_indices.back(),
            };
            this->descriptors.free_resource_indices.pop_back();
            return handle;
        }
        const DescriptorHandle handle{this->descriptors.next_resource_index};
        if (static_cast<vk::DeviceSize>(handle.slot_index + 1u) * this->descriptors.resource_stride > this->descriptors.resource_heap.size - this->descriptors.properties.minResourceHeapReservedRange) throw std::runtime_error("Spectra resource descriptor heap is exhausted");
        ++this->descriptors.next_resource_index;
        return handle;
    }

    DescriptorHandle GpuResources::acquire_sampler_descriptor() {
        if (!this->descriptors.free_sampler_indices.empty()) {
            const DescriptorHandle handle{
                this->descriptors.free_sampler_indices.back(),
            };
            this->descriptors.free_sampler_indices.pop_back();
            return handle;
        }
        const DescriptorHandle handle{this->descriptors.next_sampler_index};
        if (static_cast<vk::DeviceSize>(handle.slot_index + 1u) * this->descriptors.sampler_stride > this->descriptors.sampler_heap.size - this->descriptors.properties.minSamplerHeapReservedRange) throw std::runtime_error("Spectra sampler descriptor heap is exhausted");
        ++this->descriptors.next_sampler_index;
        return handle;
    }

    void GpuResources::reclaim_resource_descriptor(const std::uint32_t slot_index) noexcept {
        this->descriptors.free_resource_indices.push_back(slot_index);
    }

    void GpuResources::reclaim_sampler_descriptor(const std::uint32_t slot_index) noexcept {
        this->descriptors.free_sampler_indices.push_back(slot_index);
    }

    void GpuResources::write_storage_image_descriptor(const DescriptorHandle handle, const GpuImage& image, const vk::ImageLayout layout) {
        const vk::ImageViewCreateInfo view_create_info{
            {},
            *image.image,
            vk::ImageViewType::e2D,
            image.format,
            {},
            {image.aspect, 0, 1, 0, 1},
        };
        const vk::ImageDescriptorInfoEXT image_info{&view_create_info, layout};
        const vk::ResourceDescriptorInfoEXT descriptor{
            vk::DescriptorType::eStorageImage,
            vk::ResourceDescriptorDataEXT{&image_info},
        };
        const vk::HostAddressRangeEXT destination{
            static_cast<std::byte*>(this->descriptors.resource_heap.mapped) + static_cast<std::size_t>(handle.slot_index) * this->descriptors.resource_stride,
            static_cast<std::size_t>(align_device_size(this->descriptors.properties.imageDescriptorSize, this->descriptors.properties.imageDescriptorAlignment)),
        };
        this->context.graphics.device.writeResourceDescriptorsEXT(descriptor, destination);
    }

    void GpuResources::write_sampled_image_descriptor(const DescriptorHandle handle, const GpuImage& image, const vk::ImageLayout layout) {
        const vk::ImageViewCreateInfo view_create_info{
            {},
            *image.image,
            vk::ImageViewType::e2D,
            image.format,
            {},
            {image.aspect, 0, image.mip_levels, 0, 1},
        };
        const vk::ImageDescriptorInfoEXT image_info{&view_create_info, layout};
        const vk::ResourceDescriptorInfoEXT descriptor{
            vk::DescriptorType::eSampledImage,
            vk::ResourceDescriptorDataEXT{&image_info},
        };
        const vk::HostAddressRangeEXT destination{
            static_cast<std::byte*>(this->descriptors.resource_heap.mapped) + static_cast<std::size_t>(handle.slot_index) * this->descriptors.resource_stride,
            static_cast<std::size_t>(align_device_size(this->descriptors.properties.imageDescriptorSize, this->descriptors.properties.imageDescriptorAlignment)),
        };
        this->context.graphics.device.writeResourceDescriptorsEXT(descriptor, destination);
    }

    void GpuResources::write_buffer_descriptor(const DescriptorHandle handle, const vk::DescriptorType type, const GpuBuffer& buffer) {
        this->write_buffer_descriptor(handle, type, buffer.address, buffer.size);
    }

    void GpuResources::write_buffer_descriptor(const DescriptorHandle handle, const vk::DescriptorType type, const vk::DeviceAddress address, const vk::DeviceSize size) {
        const vk::DeviceAddressRangeEXT address_range{address, size};
        const vk::ResourceDescriptorInfoEXT descriptor{
            type,
            vk::ResourceDescriptorDataEXT{&address_range},
        };
        const vk::HostAddressRangeEXT destination{
            static_cast<std::byte*>(this->descriptors.resource_heap.mapped) + static_cast<std::size_t>(handle.slot_index) * this->descriptors.resource_stride,
            static_cast<std::size_t>(align_device_size(this->descriptors.properties.bufferDescriptorSize, this->descriptors.properties.bufferDescriptorAlignment)),
        };
        this->context.graphics.device.writeResourceDescriptorsEXT(descriptor, destination);
    }

    void GpuResources::write_sampler_descriptor(const DescriptorHandle handle, const vk::SamplerCreateInfo& sampler) {
        const vk::HostAddressRangeEXT destination{
            static_cast<std::byte*>(this->descriptors.sampler_heap.mapped) + static_cast<std::size_t>(handle.slot_index) * this->descriptors.sampler_stride,
            static_cast<std::size_t>(this->descriptors.sampler_stride),
        };
        this->context.graphics.device.writeSamplerDescriptorsEXT(sampler, destination);
    }

    GpuBuffer GpuResources::create_external_buffer(const vk::DeviceSize size, const vk::BufferUsageFlags usage) {
        const vk::raii::Device& device = this->context.graphics.device;
#if defined(_WIN32)
        const vk::ExternalMemoryBufferCreateInfo external_buffer{vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32};
#else
        const vk::ExternalMemoryBufferCreateInfo external_buffer{vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd};
#endif
        GpuBuffer result{};
        result.size   = size;
        result.buffer = vk::raii::Buffer{
            device,
            vk::BufferCreateInfo{
                {},
                size,
                usage,
                vk::SharingMode::eExclusive,
                0,
                nullptr,
                &external_buffer,
            },
        };
        const vk::MemoryRequirements requirements           = device.getBufferMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(vk::BufferMemoryRequirementsInfo2{*result.buffer}).get<vk::MemoryRequirements2>().memoryRequirements;
        const vk::PhysicalDeviceMemoryProperties properties = this->context.graphics.physical_device.getMemoryProperties();
        std::uint32_t memory_type                           = properties.memoryTypeCount;
        for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
            if ((requirements.memoryTypeBits & (1u << index)) != 0 && static_cast<bool>(properties.memoryTypes[index].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)) {
                memory_type = index;
                break;
            }
        if (memory_type == properties.memoryTypeCount) throw std::runtime_error("Spectra cannot allocate device-local exportable Vulkan memory");

        const vk::MemoryDedicatedAllocateInfo dedicated{{}, *result.buffer};
        const vk::MemoryAllocateFlagsInfo flags{vk::MemoryAllocateFlagBits::eDeviceAddress, 0, &dedicated};
#if defined(_WIN32)
        const vk::ExportMemoryAllocateInfo external_memory{vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32, &flags};
#else
        const vk::ExportMemoryAllocateInfo external_memory{vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd, &flags};
#endif
        result.external_memory = vk::raii::DeviceMemory{
            device,
            vk::MemoryAllocateInfo{
                requirements.size,
                memory_type,
                &external_memory,
            },
        };
        result.buffer.bindMemory(*result.external_memory, 0);
        if (static_cast<bool>(usage & vk::BufferUsageFlagBits::eShaderDeviceAddress)) result.address = device.getBufferAddress(vk::BufferDeviceAddressInfo{*result.buffer});
        return result;
    }

    ExternalHandle GpuResources::export_buffer_memory_handle(const GpuBuffer& buffer) const {
        if (!*buffer.external_memory) throw std::runtime_error("Spectra cannot export a non-external GPU buffer");
#if defined(_WIN32)
        const HANDLE handle = this->context.graphics.device.getMemoryWin32HandleKHR(vk::MemoryGetWin32HandleInfoKHR{*buffer.external_memory, vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32});
        return {ExternalHandleType::OpaqueWin32, reinterpret_cast<std::uintptr_t>(handle)};
#else
        const int handle = this->context.graphics.device.getMemoryFdKHR(vk::MemoryGetFdInfoKHR{*buffer.external_memory, vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd});
        return {ExternalHandleType::OpaqueFileDescriptor, static_cast<std::uint64_t>(handle)};
#endif
    }

    GpuExternalTimelineSemaphore GpuResources::create_external_simulation_timeline() {
        const vk::SemaphoreTypeCreateInfo timeline_type{vk::SemaphoreType::eTimeline, 0};
#if defined(_WIN32)
        const vk::ExportSemaphoreCreateInfo export_info{vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32, &timeline_type};
#else
        const vk::ExportSemaphoreCreateInfo export_info{vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd, &timeline_type};
#endif
        GpuExternalTimelineSemaphore result{};
        result.semaphore = vk::raii::Semaphore{this->context.graphics.device, vk::SemaphoreCreateInfo{{}, &export_info}};
        return result;
    }

    ExternalHandle GpuResources::export_timeline_semaphore_handle(const GpuExternalTimelineSemaphore& timeline) const {
#if defined(_WIN32)
        const HANDLE handle = this->context.graphics.device.getSemaphoreWin32HandleKHR(vk::SemaphoreGetWin32HandleInfoKHR{*timeline.semaphore, vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32});
        return {ExternalHandleType::OpaqueWin32, reinterpret_cast<std::uintptr_t>(handle)};
#else
        const int handle = this->context.graphics.device.getSemaphoreFdKHR(vk::SemaphoreGetFdInfoKHR{*timeline.semaphore, vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd});
        return {ExternalHandleType::OpaqueFileDescriptor, static_cast<std::uint64_t>(handle)};
#endif
    }

    void GpuResources::wait_external_timeline(const GpuExternalTimelineSemaphore& timeline, const std::uint64_t value) const {
        const vk::Semaphore semaphore = *timeline.semaphore;
        const vk::Result result       = this->context.graphics.device.waitSemaphores(vk::SemaphoreWaitInfo{{}, 1, &semaphore, &value}, std::numeric_limits<std::uint64_t>::max());
        if (result != vk::Result::eSuccess) throw std::runtime_error("Vulkan failed to wait for a consumed external output slot");
    }

    void GpuResources::signal_external_timeline(const GpuExternalTimelineSemaphore& timeline, const std::uint64_t value) const {
        this->context.graphics.device.signalSemaphore(vk::SemaphoreSignalInfo{*timeline.semaphore, value});
    }

    void GpuResources::submit_immediate(std::move_only_function<void(const vk::raii::CommandBuffer&)> record) {
        vk::raii::CommandBuffers command_buffers{
            this->context.graphics.device,
            vk::CommandBufferAllocateInfo{
                *this->immediate_command_pool,
                vk::CommandBufferLevel::ePrimary,
                1,
            },
        };
        const vk::raii::CommandBuffer& command_buffer = command_buffers.front();
        command_buffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        record(command_buffer);
        command_buffer.end();
        const vk::CommandBuffer raw_command_buffer = *command_buffer;
        const vk::raii::Fence fence{this->context.graphics.device, vk::FenceCreateInfo{}};
        this->context.graphics.queue.submit(
            vk::SubmitInfo{
                0,
                nullptr,
                nullptr,
                1,
                &raw_command_buffer,
            },
            *fence);
        if (this->context.graphics.device.waitForFences(*fence, vk::True, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Immediate Vulkan submission failed");
    }

    void GpuResources::bind_descriptor_heaps(const vk::raii::CommandBuffer& command_buffer) const noexcept {
        command_buffer.bindResourceHeapEXT(vk::BindHeapInfoEXT{
            {
                this->descriptors.resource_heap.address,
                this->descriptors.resource_heap.size,
            },
            this->descriptors.resource_heap.size - this->descriptors.properties.minResourceHeapReservedRange,
            this->descriptors.properties.minResourceHeapReservedRange,
        });
        command_buffer.bindSamplerHeapEXT(vk::BindHeapInfoEXT{
            {
                this->descriptors.sampler_heap.address,
                this->descriptors.sampler_heap.size,
            },
            this->descriptors.sampler_heap.size - this->descriptors.properties.minSamplerHeapReservedRange,
            this->descriptors.properties.minSamplerHeapReservedRange,
        });
    }

    void GpuResources::push_data(const vk::raii::CommandBuffer& command_buffer, const std::span<const std::byte> data, const std::uint32_t offset) const noexcept {
        command_buffer.pushDataEXT(vk::PushDataInfoEXT{
            offset,
            vk::HostAddressRangeConstEXT{data.data(), data.size_bytes()},
        });
    }


    GpuResources::AllocationBlock::~AllocationBlock() {
        if (this->mapped) this->memory.unmapMemory();
    }

    GpuAllocation GpuResources::allocate(const AllocationRequest& request) {
        const std::uint32_t memory_type         = this->find_memory_type(request.requirements.memoryTypeBits, request.properties);
        const vk::DeviceSize default_block_size = static_cast<bool>(request.properties & vk::MemoryPropertyFlagBits::eHostVisible) ? host_memory_block_size : device_memory_block_size;
        const bool dedicated                    = request.requires_dedicated || request.prefers_dedicated || request.requirements.size > default_block_size / 2u;

        if (!dedicated) {
            for (std::uint32_t block_index = 0; block_index < this->allocation.blocks.size(); ++block_index) {
                if (!this->allocation.blocks[block_index]) continue;
                AllocationBlock& block = *this->allocation.blocks[block_index];
                if (block.dedicated || block.memory_type != memory_type || block.device_address != request.device_address || block.resource_class != request.resource_class) continue;
                if (const std::optional<vk::DeviceSize> offset = this->allocate_range(block, request.requirements.size, request.requirements.alignment)) return this->make_allocation(block_index, *offset, request.requirements.size);
            }
        }

        const vk::DeviceSize allocation_size = dedicated ? request.requirements.size : std::max(default_block_size, align_device_size(request.requirements.size, 64u * 1024u));
        vk::MemoryDedicatedAllocateInfo dedicated_info{
            request.dedicated_image,
            request.dedicated_buffer,
        };
        vk::MemoryAllocateFlagsInfo allocate_flags{
            request.device_address ? vk::MemoryAllocateFlagBits::eDeviceAddress : vk::MemoryAllocateFlags{},
        };
        const void* next = nullptr;
        if (dedicated) next = &dedicated_info;
        if (request.device_address) {
            allocate_flags.pNext = next;
            next                 = &allocate_flags;
        }

        std::unique_ptr<AllocationBlock> block = std::make_unique<AllocationBlock>();
        block->memory                          = vk::raii::DeviceMemory{
            this->context.graphics.device,
            vk::MemoryAllocateInfo{
                allocation_size,
                memory_type,
                next,
            },
        };
        block->size           = allocation_size;
        block->memory_type    = memory_type;
        block->device_address = request.device_address;
        block->dedicated      = dedicated;
        block->resource_class = request.resource_class;
        if (static_cast<bool>(request.properties & vk::MemoryPropertyFlagBits::eHostVisible)) block->mapped = block->memory.mapMemory(0, allocation_size);
        if (!dedicated) block->free_ranges.emplace_back(0, allocation_size);

        const auto empty_slot           = std::ranges::find(this->allocation.blocks, nullptr);
        const std::uint32_t block_index = empty_slot == this->allocation.blocks.end() ? static_cast<std::uint32_t>(this->allocation.blocks.size()) : static_cast<std::uint32_t>(empty_slot - this->allocation.blocks.begin());
        if (empty_slot == this->allocation.blocks.end())
            this->allocation.blocks.emplace_back(std::move(block));
        else
            *empty_slot = std::move(block);
        if (dedicated) return this->make_allocation(block_index, 0, request.requirements.size);

        const std::optional<vk::DeviceSize> offset = this->allocate_range(*this->allocation.blocks[block_index], request.requirements.size, request.requirements.alignment);
        if (!offset) throw std::runtime_error("Spectra GPU allocator failed to suballocate a new memory block");
        return this->make_allocation(block_index, *offset, request.requirements.size);
    }

    void GpuResources::release_allocation(const std::uint32_t block_index, const vk::DeviceSize offset, const vk::DeviceSize size) noexcept {
        AllocationBlock& block = *this->allocation.blocks[block_index];
        if (block.dedicated) {
            this->allocation.blocks[block_index].reset();
            return;
        }

        block.free_ranges.emplace_back(offset, size);
        std::ranges::sort(block.free_ranges, {}, &AllocationRange::offset);
        std::vector<AllocationRange> merged{};
        merged.reserve(block.free_ranges.size());
        for (const AllocationRange range : block.free_ranges) {
            if (!merged.empty() && merged.back().offset + merged.back().size == range.offset)
                merged.back().size += range.size;
            else
                merged.emplace_back(range);
        }
        block.free_ranges = std::move(merged);
        if (block.free_ranges.size() == 1 && block.free_ranges.front().offset == 0 && block.free_ranges.front().size == block.size) this->allocation.blocks[block_index].reset();
    }

    std::uint32_t GpuResources::find_memory_type(const std::uint32_t type_bits, const vk::MemoryPropertyFlags properties) const {
        const vk::PhysicalDeviceMemoryProperties memory_properties = this->context.graphics.physical_device.getMemoryProperties();
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            if ((type_bits & (1u << index)) != 0 && (memory_properties.memoryTypes[index].propertyFlags & properties) == properties) return index;
        }
        throw std::runtime_error("Spectra cannot find a Vulkan memory type matching the requested GPU allocation");
    }

    std::optional<vk::DeviceSize> GpuResources::allocate_range(AllocationBlock& block, const vk::DeviceSize size, const vk::DeviceSize alignment) {
        for (std::size_t index = 0; index < block.free_ranges.size(); ++index) {
            const AllocationRange range = block.free_ranges[index];
            const vk::DeviceSize offset = align_device_size(range.offset, alignment);
            if (offset + size > range.offset + range.size) continue;
            block.free_ranges.erase(block.free_ranges.begin() + index);
            if (offset + size < range.offset + range.size)
                block.free_ranges.insert(block.free_ranges.begin() + index, AllocationRange{
                                                                                              offset + size,
                                                                                              range.offset + range.size - offset - size,
                                                                                          });
            if (range.offset < offset)
                block.free_ranges.insert(block.free_ranges.begin() + index, AllocationRange{
                                                                                              range.offset,
                                                                                              offset - range.offset,
                                                                                          });
            return offset;
        }
        return std::nullopt;
    }

    GpuAllocation GpuResources::make_allocation(const std::uint32_t block_index, const vk::DeviceSize offset, const vk::DeviceSize size) noexcept {
        GpuAllocation allocation{};
        allocation.resources   = this;
        allocation.block_index = block_index;
        allocation.offset      = offset;
        allocation.size        = size;
        return allocation;
    }

    void GpuResources::create_descriptor_heaps() {
        const vk::PhysicalDeviceDescriptorHeapPropertiesEXT& properties = this->context.graphics.descriptor_heap_properties;
        this->descriptors.resource_stride                               = align_device_size(std::max(properties.imageDescriptorSize, properties.bufferDescriptorSize), std::max(properties.imageDescriptorAlignment, properties.bufferDescriptorAlignment));
        this->descriptors.sampler_stride                                = align_device_size(properties.samplerDescriptorSize, properties.samplerDescriptorAlignment);

        const vk::DeviceSize resource_size       = align_down(std::min(align_device_size(resource_heap_size + properties.minResourceHeapReservedRange, properties.resourceHeapAlignment), properties.maxResourceHeapSize), properties.resourceHeapAlignment);
        const vk::DeviceSize sampler_size        = align_down(std::min(align_device_size(sampler_heap_size + properties.minSamplerHeapReservedRange, properties.samplerHeapAlignment), properties.maxSamplerHeapSize), properties.samplerHeapAlignment);
        constexpr vk::BufferUsageFlags usage     = vk::BufferUsageFlagBits::eDescriptorHeapEXT | vk::BufferUsageFlagBits::eShaderDeviceAddress;
        constexpr vk::MemoryPropertyFlags memory = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        this->descriptors.resource_heap          = this->create_buffer(resource_size, usage, memory, true);
        this->descriptors.sampler_heap           = this->create_buffer(sampler_size, usage, memory, true);
    }

} // namespace spectra
