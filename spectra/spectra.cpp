module;

#include <vulkan/vulkan.h>
#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

module spectra;

import std;
import vulkan;

namespace spectra {
    namespace {
        constexpr vk::DeviceSize resource_heap_size = 16u * 1024u * 1024u;
        constexpr vk::DeviceSize sampler_heap_size = 1u * 1024u * 1024u;
        constexpr vk::DeviceSize device_memory_block_size = 256u * 1024u * 1024u;
        constexpr vk::DeviceSize host_memory_block_size = 64u * 1024u * 1024u;
        constexpr vk::DeviceSize upload_frame_size = 64u * 1024u * 1024u;

        [[nodiscard]] constexpr vk::DeviceSize align_up(const vk::DeviceSize value, const vk::DeviceSize alignment) noexcept {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }

        [[nodiscard]] constexpr vk::DeviceSize align_down(const vk::DeviceSize value, const vk::DeviceSize alignment) noexcept {
            return value & ~(alignment - 1u);
        }

        [[nodiscard]] constexpr std::array<const char*, 12> required_device_extensions() noexcept {
            return {
                vk::EXTDescriptorHeapExtensionName,
                vk::KHRShaderUntypedPointersExtensionName,
                vk::KHRAccelerationStructureExtensionName,
                vk::KHRDeferredHostOperationsExtensionName,
                vk::KHRRayTracingPipelineExtensionName,
                vk::KHRRayTracingMaintenance1ExtensionName,
                vk::KHRRayTracingPositionFetchExtensionName,
                vk::KHRRayQueryExtensionName,
                vk::KHRPipelineLibraryExtensionName,
                vk::EXTShaderObjectExtensionName,
                vk::EXTMeshShaderExtensionName,
                vk::KHRSwapchainExtensionName,
            };
        }
    } // namespace

    struct GpuAllocator {
        enum class ResourceClass : std::uint8_t {
            Buffer,
            OptimalImage,
        };

        GpuAllocator(
            const vk::raii::PhysicalDevice& physical_device,
            const vk::raii::Device& device)
            : physical_device(&physical_device), device(&device) {}

        [[nodiscard]] GpuAllocation allocate(
            const vk::MemoryRequirements requirements,
            const vk::MemoryPropertyFlags properties,
            const bool device_address,
            const ResourceClass resource_class,
            const bool requires_dedicated,
            const bool prefers_dedicated,
            const vk::Buffer dedicated_buffer,
            const vk::Image dedicated_image) {
            const std::uint32_t memory_type = this->find_memory_type(
                requirements.memoryTypeBits,
                properties);
            const vk::DeviceSize default_block_size =
                static_cast<bool>(properties & vk::MemoryPropertyFlagBits::eHostVisible)
                    ? host_memory_block_size
                    : device_memory_block_size;
            const bool dedicated =
                requires_dedicated ||
                prefers_dedicated ||
                requirements.size > default_block_size / 2u;

            if (!dedicated) {
                for (std::uint32_t block_index = 0; block_index < this->blocks.size(); ++block_index) {
                    Block& block = *this->blocks[block_index];
                    if (block.dedicated ||
                        block.memory_type != memory_type ||
                        block.device_address != device_address ||
                        block.resource_class != resource_class)
                        continue;
                    if (const std::optional<vk::DeviceSize> offset = this->allocate_range(
                            block,
                            requirements.size,
                            requirements.alignment))
                        return this->make_allocation(
                            block_index,
                            *offset,
                            requirements.size);
                }
            }

            const vk::DeviceSize allocation_size = dedicated
                ? requirements.size
                : std::max(
                    default_block_size,
                    align_up(requirements.size, 64u * 1024u));
            vk::MemoryDedicatedAllocateInfo dedicated_info{
                dedicated_image,
                dedicated_buffer,
            };
            vk::MemoryAllocateFlagsInfo allocate_flags{
                device_address
                    ? vk::MemoryAllocateFlagBits::eDeviceAddress
                    : vk::MemoryAllocateFlags{},
            };
            const void* next = nullptr;
            if (dedicated) next = &dedicated_info;
            if (device_address) {
                allocate_flags.pNext = next;
                next = &allocate_flags;
            }

            std::unique_ptr<Block> block = std::make_unique<Block>();
            block->memory = vk::raii::DeviceMemory{
                *this->device,
                vk::MemoryAllocateInfo{
                    allocation_size,
                    memory_type,
                    next,
                },
            };
            block->size = allocation_size;
            block->memory_type = memory_type;
            block->device_address = device_address;
            block->dedicated = dedicated;
            block->resource_class = resource_class;
            if (static_cast<bool>(properties & vk::MemoryPropertyFlagBits::eHostVisible))
                block->mapped = block->memory.mapMemory(0, allocation_size);
            if (!dedicated)
                block->free_ranges.emplace_back(
                    0,
                    allocation_size);

            const std::uint32_t block_index =
                static_cast<std::uint32_t>(this->blocks.size());
            this->blocks.emplace_back(std::move(block));
            if (dedicated)
                return this->make_allocation(
                    block_index,
                    0,
                    requirements.size);

            const std::optional<vk::DeviceSize> offset = this->allocate_range(
                *this->blocks.back(),
                requirements.size,
                requirements.alignment);
            if (!offset) throw std::runtime_error("Spectra GPU allocator failed to suballocate a new memory block");
            return this->make_allocation(
                block_index,
                *offset,
                requirements.size);
        }

        [[nodiscard]] vk::DeviceMemory memory(const GpuAllocation& allocation) const noexcept {
            return *this->blocks[allocation.block]->memory;
        }

        [[nodiscard]] vk::DeviceSize offset(const GpuAllocation& allocation) const noexcept {
            return allocation.offset;
        }

        [[nodiscard]] void* mapped(const GpuAllocation& allocation) const noexcept {
            if (!this->blocks[allocation.block]->mapped) return nullptr;
            return static_cast<std::byte*>(this->blocks[allocation.block]->mapped) +
                allocation.offset;
        }

        void release(
            const std::uint32_t block_index,
            const vk::DeviceSize offset,
            const vk::DeviceSize size) noexcept {
            Block& block = *this->blocks[block_index];
            if (block.dedicated) {
                if (block.mapped) {
                    block.memory.unmapMemory();
                    block.mapped = nullptr;
                }
                block.memory = nullptr;
                return;
            }

            block.free_ranges.emplace_back(offset, size);
            std::ranges::sort(block.free_ranges, {}, &Range::offset);
            std::vector<Range> merged{};
            merged.reserve(block.free_ranges.size());
            for (const Range range : block.free_ranges) {
                if (!merged.empty() &&
                    merged.back().offset + merged.back().size == range.offset)
                    merged.back().size += range.size;
                else
                    merged.emplace_back(range);
            }
            block.free_ranges = std::move(merged);
        }

    private:
        struct Range {
            vk::DeviceSize offset{};
            vk::DeviceSize size{};
        };

        struct Block {
            vk::raii::DeviceMemory memory{nullptr};
            vk::DeviceSize size{};
            std::uint32_t memory_type{};
            bool device_address{};
            bool dedicated{};
            ResourceClass resource_class{ResourceClass::Buffer};
            void* mapped{};
            std::vector<Range> free_ranges{};

            ~Block() {
                if (this->mapped) this->memory.unmapMemory();
            }
        };

        [[nodiscard]] std::uint32_t find_memory_type(
            const std::uint32_t type_bits,
            const vk::MemoryPropertyFlags properties) const {
            const vk::PhysicalDeviceMemoryProperties memory_properties =
                this->physical_device->getMemoryProperties();
            for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
                if ((type_bits & (1u << index)) != 0 &&
                    (memory_properties.memoryTypes[index].propertyFlags & properties) == properties)
                    return index;
            }
            throw std::runtime_error("Spectra cannot find a Vulkan memory type matching the requested GPU allocation");
        }

        [[nodiscard]] std::optional<vk::DeviceSize> allocate_range(
            Block& block,
            const vk::DeviceSize size,
            const vk::DeviceSize alignment) {
            for (std::size_t index = 0; index < block.free_ranges.size(); ++index) {
                const Range range = block.free_ranges[index];
                const vk::DeviceSize offset = align_up(range.offset, alignment);
                if (offset + size > range.offset + range.size) continue;
                block.free_ranges.erase(block.free_ranges.begin() + index);
                if (offset + size < range.offset + range.size)
                    block.free_ranges.insert(
                        block.free_ranges.begin() + index,
                        Range{
                            offset + size,
                            range.offset + range.size - offset - size,
                        });
                if (range.offset < offset)
                    block.free_ranges.insert(
                        block.free_ranges.begin() + index,
                        Range{
                            range.offset,
                            offset - range.offset,
                        });
                return offset;
            }
            return std::nullopt;
        }

        [[nodiscard]] GpuAllocation make_allocation(
            const std::uint32_t block,
            const vk::DeviceSize offset,
            const vk::DeviceSize size) noexcept {
            GpuAllocation allocation{};
            allocation.allocator = this;
            allocation.block = block;
            allocation.offset = offset;
            allocation.size = size;
            return allocation;
        }

        const vk::raii::PhysicalDevice* physical_device{};
        const vk::raii::Device* device{};
        std::vector<std::unique_ptr<Block>> blocks{};
    };

    GpuAllocation::~GpuAllocation() {
        if (this->allocator)
            this->allocator->release(
                this->block,
                this->offset,
                this->size);
    }

    GpuAllocation::GpuAllocation(GpuAllocation&& other) noexcept
        : allocator(std::exchange(other.allocator, nullptr)),
          block(std::exchange(other.block, 0)),
          offset(std::exchange(other.offset, 0)),
          size(std::exchange(other.size, 0)) {}

    GpuAllocation& GpuAllocation::operator=(GpuAllocation&& other) noexcept {
        if (this == &other) return *this;
        if (this->allocator)
            this->allocator->release(
                this->block,
                this->offset,
                this->size);
        this->allocator = std::exchange(other.allocator, nullptr);
        this->block = std::exchange(other.block, 0);
        this->offset = std::exchange(other.offset, 0);
        this->size = std::exchange(other.size, 0);
        return *this;
    }

    GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
        : allocation(std::move(other.allocation)),
          buffer(std::move(other.buffer)),
          address(std::exchange(other.address, 0)),
          size(std::exchange(other.size, 0)),
          mapped(std::exchange(other.mapped, nullptr)) {}

    GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
        if (this == &other) return *this;
        this->buffer = nullptr;
        this->allocation = std::move(other.allocation);
        this->buffer = std::move(other.buffer);
        this->address = std::exchange(other.address, 0);
        this->size = std::exchange(other.size, 0);
        this->mapped = std::exchange(other.mapped, nullptr);
        return *this;
    }

    GpuImage::GpuImage(GpuImage&& other) noexcept
        : allocation(std::move(other.allocation)),
          image(std::move(other.image)),
          view(std::move(other.view)),
          extent(std::exchange(other.extent, {})),
          format(std::exchange(other.format, {})),
          mip_levels(std::exchange(other.mip_levels, 1)) {}

    GpuImage& GpuImage::operator=(GpuImage&& other) noexcept {
        if (this == &other) return *this;
        this->view = nullptr;
        this->image = nullptr;
        this->allocation = std::move(other.allocation);
        this->image = std::move(other.image);
        this->view = std::move(other.view);
        this->extent = std::exchange(other.extent, {});
        this->format = std::exchange(other.format, {});
        this->mip_levels =
            std::exchange(other.mip_levels, 1);
        return *this;
    }

    std::vector<std::uint32_t> load_spirv(const std::filesystem::path& path) {
        std::ifstream input{path, std::ios::binary | std::ios::ate};
        if (!input) throw std::runtime_error(std::format("Cannot open Spectra shader: {}", path.string()));
        const std::streamsize byte_count = input.tellg();
        if (byte_count <= 0 || byte_count % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0) throw std::runtime_error(std::format("Spectra shader has an invalid SPIR-V size: {}", path.string()));
        std::vector<std::uint32_t> words(static_cast<std::size_t>(byte_count) / sizeof(std::uint32_t));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(words.data()), byte_count);
        if (!input) throw std::runtime_error(std::format("Cannot read Spectra shader: {}", path.string()));
        return words;
    }

    Spectra::GlfwLifetime::GlfwLifetime() {
        if (glfwInit() != GLFW_TRUE) throw std::runtime_error("GLFW initialization failed");
    }

    Spectra::GlfwLifetime::~GlfwLifetime() {
        glfwTerminate();
    }

    Spectra::Spectra(
        const std::string_view application_name,
        const vk::Extent2D initial_extent)
        : gpu(*this) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
        this->platform.window.reset(glfwCreateWindow(
            static_cast<int>(initial_extent.width),
            static_cast<int>(initial_extent.height),
            std::string{application_name}.c_str(),
            nullptr,
            nullptr));
        if (!this->platform.window) throw std::runtime_error("Spectra window creation failed");
        this->window = this->platform.window.get();
        glfwSetWindowSizeLimits(this->platform.window.get(), 960, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
        glfwSetWindowUserPointer(this->platform.window.get(), this);
        glfwSetDropCallback(this->platform.window.get(), [](GLFWwindow* window, const int count, const char** paths) {
            Spectra& runtime = *static_cast<Spectra*>(glfwGetWindowUserPointer(window));
            runtime.platform.dropped_paths.clear();
            for (int index = 0; index < count; ++index) runtime.platform.dropped_paths.emplace_back(paths[index]);
        });
        glfwSetWindowCloseCallback(this->platform.window.get(), [](GLFWwindow* window) {
            Spectra& runtime = *static_cast<Spectra*>(glfwGetWindowUserPointer(window));
            runtime.platform.close_requested = true;
            glfwSetWindowShouldClose(window, GLFW_FALSE);
        });

        const std::string application_name_string{application_name};
        const vk::ApplicationInfo application_info{
            application_name_string.c_str(),
            vk::makeApiVersion(0u, 2u, 0u, 0u),
            "Spectra",
            vk::makeApiVersion(0u, 2u, 0u, 0u),
            vk::ApiVersion14,
        };
        constexpr std::array instance_extensions{
            vk::KHRSurfaceExtensionName,
            vk::KHRWin32SurfaceExtensionName,
        };
        const vk::InstanceCreateInfo instance_create_info{
            {},
            &application_info,
            0,
            nullptr,
            static_cast<std::uint32_t>(instance_extensions.size()),
            instance_extensions.data(),
        };
        this->context.instance = vk::raii::Instance{this->context.loader, instance_create_info};

        VkSurfaceKHR surface{};
        const VkResult surface_result = glfwCreateWindowSurface(
            static_cast<VkInstance>(*this->context.instance),
            this->platform.window.get(),
            nullptr,
            &surface);
        if (surface_result != VK_SUCCESS) throw std::runtime_error("GLFW failed to create the Spectra Vulkan surface");
        this->platform.surface = vk::raii::SurfaceKHR{this->context.instance, vk::SurfaceKHR{surface}};

        constexpr std::array required_extensions = required_device_extensions();
        for (const vk::raii::PhysicalDevice& candidate : this->context.instance.enumeratePhysicalDevices()) {
            const vk::PhysicalDeviceProperties candidate_properties = candidate.getProperties();
            if (candidate_properties.apiVersion < vk::ApiVersion14 || candidate_properties.deviceType != vk::PhysicalDeviceType::eDiscreteGpu) continue;

            const std::vector<vk::ExtensionProperties> available_extensions = candidate.enumerateDeviceExtensionProperties();
            const bool extensions_available = std::ranges::all_of(required_extensions, [&available_extensions](const char* required) {
                return std::ranges::contains(available_extensions, std::string_view{required}, [](const vk::ExtensionProperties& extension) {
                    return std::string_view{extension.extensionName.data()};
                });
            });
            if (!extensions_available) continue;

            const auto features = candidate.getFeatures2<
                vk::PhysicalDeviceFeatures2,
                vk::PhysicalDeviceVulkan11Features,
                vk::PhysicalDeviceVulkan12Features,
                vk::PhysicalDeviceVulkan13Features,
                vk::PhysicalDeviceDescriptorHeapFeaturesEXT,
                vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR,
                vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
                vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
                vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR,
                vk::PhysicalDeviceRayTracingPositionFetchFeaturesKHR,
                vk::PhysicalDeviceRayQueryFeaturesKHR,
                vk::PhysicalDeviceShaderObjectFeaturesEXT,
                vk::PhysicalDeviceMeshShaderFeaturesEXT
            >();
            if (!features.get<vk::PhysicalDeviceFeatures2>().features.shaderInt64) continue;
            if (!features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters) continue;
            if (!features.get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress || !features.get<vk::PhysicalDeviceVulkan12Features>().scalarBlockLayout) continue;
            if (!features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 || !features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering) continue;
            if (!features.get<vk::PhysicalDeviceDescriptorHeapFeaturesEXT>().descriptorHeap) continue;
            if (!features.get<vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR>().shaderUntypedPointers) continue;
            if (!features.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure) continue;
            if (!features.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>().rayTracingPipeline) continue;
            if (!features.get<vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR>().rayTracingMaintenance1 ||
                !features.get<vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR>().rayTracingPipelineTraceRaysIndirect2)
                continue;
            if (!features.get<vk::PhysicalDeviceRayTracingPositionFetchFeaturesKHR>().rayTracingPositionFetch) continue;
            if (!features.get<vk::PhysicalDeviceRayQueryFeaturesKHR>().rayQuery) continue;
            if (!features.get<vk::PhysicalDeviceShaderObjectFeaturesEXT>().shaderObject) continue;
            if (!features.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>().meshShader) continue;

            const std::vector<vk::QueueFamilyProperties> queue_families = candidate.getQueueFamilyProperties();
            std::uint32_t graphics_family_index = static_cast<std::uint32_t>(queue_families.size());
            for (std::uint32_t index = 0; index < queue_families.size(); ++index) {
                if (!static_cast<bool>(queue_families[index].queueFlags & vk::QueueFlagBits::eGraphics)) continue;
                if (!candidate.getSurfaceSupportKHR(index, *this->platform.surface)) continue;
                graphics_family_index = index;
                break;
            }
            if (graphics_family_index == queue_families.size()) continue;

            this->device.physical_device = candidate;
            this->device.queue_family_index = graphics_family_index;
            break;
        }
        if (!*this->device.physical_device) throw std::runtime_error("Spectra requires a discrete Vulkan 1.4 GPU with Descriptor Heap, Shader Untyped Pointers, Shader Object, Mesh Shader, and the complete KHR ray tracing pipeline profile");

        vk::StructureChain<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan12Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceDescriptorHeapFeaturesEXT,
            vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR,
            vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
            vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
            vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR,
            vk::PhysicalDeviceRayTracingPositionFetchFeaturesKHR,
            vk::PhysicalDeviceRayQueryFeaturesKHR,
            vk::PhysicalDeviceShaderObjectFeaturesEXT,
            vk::PhysicalDeviceMeshShaderFeaturesEXT
        > enabled_features{};
        enabled_features.get<vk::PhysicalDeviceFeatures2>().features.shaderInt64 = vk::True;
        enabled_features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters = vk::True;
        enabled_features.get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress = vk::True;
        enabled_features.get<vk::PhysicalDeviceVulkan12Features>().scalarBlockLayout = vk::True;
        enabled_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = vk::True;
        enabled_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = vk::True;
        enabled_features.get<vk::PhysicalDeviceDescriptorHeapFeaturesEXT>().descriptorHeap = vk::True;
        enabled_features.get<vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR>().shaderUntypedPointers = vk::True;
        enabled_features.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure = vk::True;
        enabled_features.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>().rayTracingPipeline = vk::True;
        enabled_features.get<vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR>().rayTracingMaintenance1 = vk::True;
        enabled_features.get<vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR>().rayTracingPipelineTraceRaysIndirect2 = vk::True;
        enabled_features.get<vk::PhysicalDeviceRayTracingPositionFetchFeaturesKHR>().rayTracingPositionFetch = vk::True;
        enabled_features.get<vk::PhysicalDeviceRayQueryFeaturesKHR>().rayQuery = vk::True;
        enabled_features.get<vk::PhysicalDeviceShaderObjectFeaturesEXT>().shaderObject = vk::True;
        enabled_features.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>().meshShader = vk::True;

        constexpr std::array queue_priorities{1.0f};
        const vk::DeviceQueueCreateInfo queue_create_info{{}, this->device.queue_family_index, 1, queue_priorities.data()};
        const vk::DeviceCreateInfo device_create_info{
            {},
            1,
            &queue_create_info,
            0,
            nullptr,
            static_cast<std::uint32_t>(required_extensions.size()),
            required_extensions.data(),
            nullptr,
            &enabled_features.get<vk::PhysicalDeviceFeatures2>(),
        };
        this->device.logical_device = vk::raii::Device{this->device.physical_device, device_create_info};
        this->device.queue = vk::raii::Queue{
            this->device.logical_device,
            this->device.queue_family_index,
            0,
        };
        this->memory.allocator = std::make_unique<GpuAllocator>(
            this->device.physical_device,
            this->device.logical_device);

        const auto properties = this->device.physical_device.getProperties2<
            vk::PhysicalDeviceProperties2,
            vk::PhysicalDeviceDescriptorHeapPropertiesEXT,
            vk::PhysicalDeviceAccelerationStructurePropertiesKHR,
            vk::PhysicalDeviceRayTracingPipelinePropertiesKHR
        >();
        this->descriptors.properties = properties.get<vk::PhysicalDeviceDescriptorHeapPropertiesEXT>();
        this->device.acceleration_structure_properties =
            properties.get<
                vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
        this->device.ray_tracing_properties = properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
        this->descriptors.resource_stride = std::max(
            align_up(this->descriptors.properties.imageDescriptorSize, this->descriptors.properties.imageDescriptorAlignment),
            align_up(this->descriptors.properties.bufferDescriptorSize, this->descriptors.properties.bufferDescriptorAlignment));
        this->descriptors.sampler_stride = align_up(
            this->descriptors.properties.samplerDescriptorSize,
            this->descriptors.properties.samplerDescriptorAlignment);
        this->descriptors.resource_heap = this->gpu.create_buffer(
            align_down(
                std::min(
                    align_up(resource_heap_size + this->descriptors.properties.minResourceHeapReservedRange, this->descriptors.properties.resourceHeapAlignment),
                    this->descriptors.properties.maxResourceHeapSize),
                this->descriptors.properties.resourceHeapAlignment),
            vk::BufferUsageFlagBits::eDescriptorHeapEXT | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            true);
        this->descriptors.sampler_heap = this->gpu.create_buffer(
            align_down(
                std::min(
                    align_up(sampler_heap_size + this->descriptors.properties.minSamplerHeapReservedRange, this->descriptors.properties.samplerHeapAlignment),
                    this->descriptors.properties.maxSamplerHeapSize),
                this->descriptors.properties.samplerHeapAlignment),
            vk::BufferUsageFlagBits::eDescriptorHeapEXT | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            true);

        this->uploads.buffer = this->gpu.create_buffer(
            upload_frame_size * frames_in_flight,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent,
            true);

        this->immediate.command_pool = vk::raii::CommandPool{
            this->device.logical_device,
            vk::CommandPoolCreateInfo{
                vk::CommandPoolCreateFlagBits::eTransient,
                this->device.queue_family_index,
            },
        };
        this->frames.command_pool = vk::raii::CommandPool{
            this->device.logical_device,
            vk::CommandPoolCreateInfo{
                vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                this->device.queue_family_index,
            },
        };
        this->frames.command_buffers = vk::raii::CommandBuffers{
            this->device.logical_device,
            vk::CommandBufferAllocateInfo{
                *this->frames.command_pool,
                vk::CommandBufferLevel::ePrimary,
                frames_in_flight,
            },
        };
        for (std::uint32_t index = 0; index < frames_in_flight; ++index) {
            this->frames.image_available.emplace_back(this->device.logical_device, vk::SemaphoreCreateInfo{});
            this->frames.fences.emplace_back(
                this->device.logical_device,
                vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled});
        }

        this->platform.native = glfwGetWin32Window(this->platform.window.get());
        SetPropW(this->platform.native, L"SpectraWindow", this);
        this->platform.original_window_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            this->platform.native,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&Spectra::window_proc)));
        constexpr LONG_PTR style =
            WS_POPUP |
            WS_VISIBLE |
            WS_THICKFRAME |
            WS_MINIMIZEBOX |
            WS_MAXIMIZEBOX |
            WS_SYSMENU;
        SetWindowLongPtrW(this->platform.native, GWL_STYLE, style);
        constexpr BOOL dark_mode = TRUE;
        DwmSetWindowAttribute(this->platform.native, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));
        constexpr DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
        DwmSetWindowAttribute(this->platform.native, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
        SetWindowPos(
            this->platform.native,
            nullptr,
            0,
            0,
            0,
            0,
            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

        this->create_swapchain();
    }

    Spectra::~Spectra() {
        static_cast<void>(
            vkDeviceWaitIdle(
                static_cast<VkDevice>(
                    *this->device.logical_device)));
        SetWindowLongPtrW(
            this->platform.native,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(this->platform.original_window_proc));
        RemovePropW(this->platform.native, L"SpectraWindow");
    }

    void Spectra::poll_events() noexcept {
        glfwPollEvents();
    }

    void Spectra::wait_events() noexcept {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(this->platform.window.get(), &width, &height);
        if (width == 0 || height == 0) glfwWaitEvents();
        else glfwPollEvents();
    }

    bool Spectra::take_close_request() noexcept {
        return std::exchange(this->platform.close_requested, false);
    }

    std::vector<std::filesystem::path> Spectra::take_dropped_paths() noexcept {
        return std::exchange(this->platform.dropped_paths, {});
    }

    void Spectra::request_close() noexcept {
        this->platform.close_requested = true;
    }

    std::optional<FrameContext> Spectra::begin_frame() {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(this->platform.window.get(), &width, &height);
        if (width == 0 || height == 0) return std::nullopt;
        const vk::Extent2D framebuffer_extent{
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
        };
        if (this->presentation.extent != framebuffer_extent) this->create_swapchain();

        const vk::raii::Fence& fence = this->frames.fences[this->frames.index];
        if (this->device.logical_device.waitForFences(
                *fence,
                vk::True,
                std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess)
            throw std::runtime_error("Spectra frame fence wait failed");
        this->uploads.offsets[this->frames.index] = 0;
        this->deferred.destructions[this->frames.index].clear();
        this->descriptors.free_resource_indices.append_range(
            this->deferred.resource_indices[this->frames.index]);
        this->deferred.resource_indices[this->frames.index].clear();
        this->descriptors.free_sampler_indices.append_range(
            this->deferred.sampler_indices[this->frames.index]);
        this->deferred.sampler_indices[this->frames.index].clear();

        vk::ResultValue<std::uint32_t> acquired{vk::Result::eSuccess, 0};
        try {
            acquired = this->presentation.swapchain.acquireNextImage(
                std::numeric_limits<std::uint64_t>::max(),
                *this->frames.image_available[this->frames.index]);
        } catch (const vk::OutOfDateKHRError&) {
            this->create_swapchain();
            return std::nullopt;
        }
        this->presentation.acquired_image = acquired.value;
        this->presentation.acquired_suboptimal = acquired.result == vk::Result::eSuboptimalKHR;

        this->device.logical_device.resetFences(*fence);
        const vk::raii::CommandBuffer& command_buffer = this->frames.command_buffers[this->frames.index];
        command_buffer.reset();
        command_buffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        return FrameContext{
            this->frames.index,
            command_buffer,
            {
                this->presentation.images[acquired.value],
                *this->presentation.views[acquired.value],
                this->presentation.extent,
                this->presentation.layouts[acquired.value],
            },
        };
    }

    bool Spectra::present_frame() {
        const vk::raii::CommandBuffer& command_buffer = this->frames.command_buffers[this->frames.index];
        command_buffer.end();
        this->presentation.layouts[this->presentation.acquired_image] = vk::ImageLayout::ePresentSrcKHR;

        const vk::SemaphoreSubmitInfo wait_info{
            *this->frames.image_available[this->frames.index],
            0,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            0,
        };
        const vk::CommandBufferSubmitInfo command_info{*command_buffer};
        const vk::SemaphoreSubmitInfo signal_info{
            *this->presentation.render_finished[this->presentation.acquired_image],
            0,
            vk::PipelineStageFlagBits2::eAllCommands,
            0,
        };
        this->device.queue.submit2(
            vk::SubmitInfo2{
                {},
                1,
                &wait_info,
                1,
                &command_info,
                1,
                &signal_info,
            },
            *this->frames.fences[this->frames.index]);

        const vk::Semaphore render_finished = *this->presentation.render_finished[this->presentation.acquired_image];
        const vk::SwapchainKHR swapchain = *this->presentation.swapchain;
        vk::Result present_result{};
        try {
            present_result = this->device.queue.presentKHR(vk::PresentInfoKHR{
                1,
                &render_finished,
                1,
                &swapchain,
                &this->presentation.acquired_image,
            });
        } catch (const vk::OutOfDateKHRError&) {
            this->create_swapchain();
            return false;
        }
        if (this->presentation.acquired_suboptimal || present_result == vk::Result::eSuboptimalKHR)
            this->create_swapchain();
        this->frames.index = (this->frames.index + 1u) % frames_in_flight;
        return true;
    }

    void Spectra::wait_idle() const {
        this->device.logical_device.waitIdle();
    }

    LRESULT CALLBACK Spectra::window_proc(
        HWND window,
        const UINT message,
        const WPARAM wparam,
        const LPARAM lparam) {
        Spectra* runtime = static_cast<Spectra*>(GetPropW(window, L"SpectraWindow"));
        if (runtime == nullptr) return DefWindowProcW(window, message, wparam, lparam);
        switch (message) {
        case WM_NCCALCSIZE:
            if (wparam != 0) return 0;
            break;
        case WM_NCHITTEST: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(window, &point);
            RECT client{};
            GetClientRect(window, &client);
            if (!IsZoomed(window)) {
                const UINT dpi = GetDpiForWindow(window);
                const int border =
                    GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
                    GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                const bool left = point.x < border;
                const bool right = point.x >= client.right - border;
                const bool top = point.y < border;
                const bool bottom = point.y >= client.bottom - border;
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
            }
            for (const std::array<float, 4>& region : runtime->drag_regions)
                if (static_cast<float>(point.x) >= region[0] &&
                    static_cast<float>(point.y) >= region[1] &&
                    static_cast<float>(point.x) < region[2] &&
                    static_cast<float>(point.y) < region[3])
                    return HTCAPTION;
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO: {
            MONITORINFO monitor_info{sizeof(MONITORINFO)};
            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor_info);
            MINMAXINFO& minmax = *reinterpret_cast<MINMAXINFO*>(lparam);
            minmax.ptMaxPosition = {
                monitor_info.rcWork.left - monitor_info.rcMonitor.left,
                monitor_info.rcWork.top - monitor_info.rcMonitor.top,
            };
            minmax.ptMaxSize = {
                monitor_info.rcWork.right - monitor_info.rcWork.left,
                monitor_info.rcWork.bottom - monitor_info.rcWork.top,
            };
            minmax.ptMinTrackSize = {960, 600};
            return 0;
        }
        case WM_DPICHANGED: {
            const RECT& suggested = *reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(
                window,
                nullptr,
                suggested.left,
                suggested.top,
                suggested.right - suggested.left,
                suggested.bottom - suggested.top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        }
        return CallWindowProcW(runtime->platform.original_window_proc, window, message, wparam, lparam);
    }

    void Spectra::create_swapchain() {
        this->wait_idle();
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(this->platform.window.get(), &width, &height);
        if (width == 0 || height == 0) {
            this->presentation.extent = vk::Extent2D{};
            return;
        }
        const vk::SurfaceCapabilitiesKHR capabilities =
            this->device.physical_device.getSurfaceCapabilitiesKHR(*this->platform.surface);
        const std::vector<vk::SurfaceFormatKHR> formats =
            this->device.physical_device.getSurfaceFormatsKHR(*this->platform.surface);
        const std::vector<vk::PresentModeKHR> present_modes =
            this->device.physical_device.getSurfacePresentModesKHR(*this->platform.surface);
        constexpr vk::SurfaceFormatKHR required_format{
            vk::Format::eB8G8R8A8Srgb,
            vk::ColorSpaceKHR::eSrgbNonlinear,
        };
        if (!std::ranges::contains(formats, required_format)) throw std::runtime_error("Spectra requires a B8G8R8A8 sRGB swapchain");
        if (!std::ranges::contains(present_modes, vk::PresentModeKHR::eMailbox)) throw std::runtime_error("Spectra requires mailbox presentation");
        if (!static_cast<bool>(capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)) throw std::runtime_error("Spectra requires identity surface transforms");
        if (!static_cast<bool>(capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eOpaque)) throw std::runtime_error("Spectra requires opaque swapchain composition");
        if (capabilities.minImageCount > 3 || (capabilities.maxImageCount != 0 && capabilities.maxImageCount < 3)) throw std::runtime_error("Spectra requires triple-buffered presentation");

        this->presentation.extent = vk::Extent2D{
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
        };
        const vk::SwapchainKHR old_swapchain = *this->presentation.swapchain;
        const vk::SwapchainCreateInfoKHR create_info{
            {},
            *this->platform.surface,
            3,
            required_format.format,
            required_format.colorSpace,
            this->presentation.extent,
            1,
            vk::ImageUsageFlagBits::eColorAttachment,
            vk::SharingMode::eExclusive,
            0,
            nullptr,
            vk::SurfaceTransformFlagBitsKHR::eIdentity,
            vk::CompositeAlphaFlagBitsKHR::eOpaque,
            vk::PresentModeKHR::eMailbox,
            vk::True,
            old_swapchain,
        };
        vk::raii::SwapchainKHR replacement{this->device.logical_device, create_info};
        this->presentation.views.clear();
        this->presentation.swapchain = std::move(replacement);
        this->presentation.images = this->presentation.swapchain.getImages();
        this->presentation.layouts.assign(
            this->presentation.images.size(),
            vk::ImageLayout::eUndefined);
        this->presentation.render_finished.clear();
        this->presentation.render_finished.reserve(this->presentation.images.size());
        for (const vk::Image image : this->presentation.images) {
            this->presentation.views.emplace_back(
                this->device.logical_device,
                vk::ImageViewCreateInfo{
                    {},
                    image,
                    vk::ImageViewType::e2D,
                    required_format.format,
                    {},
                    {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
                });
            this->presentation.render_finished.emplace_back(
                this->device.logical_device,
                vk::SemaphoreCreateInfo{});
        }
    }

    GpuDevice::GpuDevice(Spectra& runtime) noexcept
        : device(runtime.device.logical_device),
          acceleration_structure_properties(runtime.device.acceleration_structure_properties),
          ray_tracing_properties(runtime.device.ray_tracing_properties),
          frame_index(runtime.frames.index),
          runtime(&runtime) {}

    void GpuDevice::wait_idle() const {
        this->runtime->wait_idle();
    }

    GpuBuffer GpuDevice::create_buffer(
        const vk::DeviceSize size,
        const vk::BufferUsageFlags usage,
        const vk::MemoryPropertyFlags memory_properties,
        const bool mapped) const {
        const vk::raii::Device& logical_device = this->runtime->device.logical_device;
        GpuBuffer result{};
        result.size = size;
        result.buffer = vk::raii::Buffer{
            logical_device,
            vk::BufferCreateInfo{{}, size, usage, vk::SharingMode::eExclusive},
        };
        const auto requirements = logical_device.getBufferMemoryRequirements2<
            vk::MemoryRequirements2,
            vk::MemoryDedicatedRequirements>(
            vk::BufferMemoryRequirementsInfo2{*result.buffer});
        result.allocation = this->runtime->memory.allocator->allocate(
            requirements.get<vk::MemoryRequirements2>().memoryRequirements,
            memory_properties,
            static_cast<bool>(usage & vk::BufferUsageFlagBits::eShaderDeviceAddress),
            GpuAllocator::ResourceClass::Buffer,
            requirements.get<vk::MemoryDedicatedRequirements>().requiresDedicatedAllocation,
            requirements.get<vk::MemoryDedicatedRequirements>().prefersDedicatedAllocation,
            *result.buffer,
            {});
        result.buffer.bindMemory(
            this->runtime->memory.allocator->memory(result.allocation),
            this->runtime->memory.allocator->offset(result.allocation));
        if (static_cast<bool>(usage & vk::BufferUsageFlagBits::eShaderDeviceAddress))
            result.address = logical_device.getBufferAddress(vk::BufferDeviceAddressInfo{*result.buffer});
        if (mapped) {
            result.mapped = this->runtime->memory.allocator->mapped(result.allocation);
            if (!result.mapped) throw std::runtime_error("Spectra cannot map a GPU buffer allocated from non-host-visible memory");
        }
        return result;
    }

    GpuImage GpuDevice::create_image_2d(
        const vk::Extent2D extent,
        const vk::Format format,
        const vk::ImageUsageFlags usage,
        const vk::ImageAspectFlags aspect,
        const std::uint32_t mip_levels) const {
        const vk::raii::Device& logical_device = this->runtime->device.logical_device;
        GpuImage result{};
        result.extent = extent;
        result.format = format;
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
        result.image = vk::raii::Image{logical_device, image_create_info};
        const auto requirements = logical_device.getImageMemoryRequirements2<
            vk::MemoryRequirements2,
            vk::MemoryDedicatedRequirements>(
            vk::ImageMemoryRequirementsInfo2{*result.image});
        result.allocation = this->runtime->memory.allocator->allocate(
            requirements.get<vk::MemoryRequirements2>().memoryRequirements,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            false,
            GpuAllocator::ResourceClass::OptimalImage,
            requirements.get<vk::MemoryDedicatedRequirements>().requiresDedicatedAllocation,
            requirements.get<vk::MemoryDedicatedRequirements>().prefersDedicatedAllocation,
            {},
            *result.image);
        result.image.bindMemory(
            this->runtime->memory.allocator->memory(result.allocation),
            this->runtime->memory.allocator->offset(result.allocation));
        result.view = vk::raii::ImageView{
            logical_device,
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

    GpuUploadSlice GpuDevice::stage_upload(
        const std::span<const std::byte> data,
        const vk::DeviceSize alignment) const {
        const std::uint32_t frame = this->runtime->frames.index;
        const vk::DeviceSize local_offset = align_up(
            this->runtime->uploads.offsets[frame],
            alignment);
        if (local_offset + data.size_bytes() > upload_frame_size)
            throw std::runtime_error(
                "Spectra per-frame upload ring is exhausted");
        const vk::DeviceSize offset =
            frame * upload_frame_size +
            local_offset;
        std::memcpy(
            static_cast<std::byte*>(
                this->runtime->uploads.buffer.mapped) +
                offset,
            data.data(),
            data.size_bytes());
        this->runtime->uploads.offsets[frame] =
            local_offset +
            data.size_bytes();
        return {
            *this->runtime->uploads.buffer.buffer,
            offset,
            data.size_bytes(),
        };
    }

    void GpuDevice::defer(
        std::move_only_function<void()> destruction) {
        this->runtime->deferred
            .destructions[this->runtime->frames.index]
            .push_back(std::move(destruction));
    }

    void GpuDevice::immediate(
        std::move_only_function<void(const vk::raii::CommandBuffer&)> record) const {
        vk::raii::CommandBuffers command_buffers{
            this->runtime->device.logical_device,
            vk::CommandBufferAllocateInfo{
                *this->runtime->immediate.command_pool,
                vk::CommandBufferLevel::ePrimary,
                1,
            },
        };
        const vk::raii::CommandBuffer& command_buffer = command_buffers.front();
        command_buffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        record(command_buffer);
        command_buffer.end();
        const vk::CommandBuffer raw_command_buffer = *command_buffer;
        this->runtime->device.queue.submit(vk::SubmitInfo{
            0,
            nullptr,
            nullptr,
            1,
            &raw_command_buffer,
        });
        this->runtime->device.queue.waitIdle();
    }

    DescriptorHandle GpuDevice::allocate_resource_descriptor() {
        if (!this->runtime->descriptors.free_resource_indices.empty()) {
            const DescriptorHandle handle{
                this->runtime->descriptors.free_resource_indices.back(),
            };
            this->runtime->descriptors.free_resource_indices.pop_back();
            return handle;
        }
        const DescriptorHandle handle{this->runtime->descriptors.next_resource_index++};
        if (static_cast<vk::DeviceSize>(handle.index + 1u) *
                this->runtime->descriptors.resource_stride >
            this->runtime->descriptors.resource_heap.size -
                this->runtime->descriptors.properties.minResourceHeapReservedRange)
            throw std::runtime_error("Spectra resource descriptor heap is exhausted");
        return handle;
    }

    DescriptorHandle GpuDevice::allocate_sampler_descriptor() {
        if (!this->runtime->descriptors.free_sampler_indices.empty()) {
            const DescriptorHandle handle{
                this->runtime->descriptors.free_sampler_indices.back(),
            };
            this->runtime->descriptors.free_sampler_indices.pop_back();
            return handle;
        }
        const DescriptorHandle handle{this->runtime->descriptors.next_sampler_index++};
        if (static_cast<vk::DeviceSize>(handle.index + 1u) *
                this->runtime->descriptors.sampler_stride >
            this->runtime->descriptors.sampler_heap.size -
                this->runtime->descriptors.properties.minSamplerHeapReservedRange)
            throw std::runtime_error("Spectra sampler descriptor heap is exhausted");
        return handle;
    }

    void GpuDevice::release_resource_descriptor(const DescriptorHandle handle) noexcept {
        this->runtime->deferred
            .resource_indices[this->runtime->frames.index]
            .push_back(handle.index);
    }

    void GpuDevice::release_sampler_descriptor(const DescriptorHandle handle) noexcept {
        this->runtime->deferred
            .sampler_indices[this->runtime->frames.index]
            .push_back(handle.index);
    }

    void GpuDevice::write_storage_image(
        const DescriptorHandle handle,
        const GpuImage& image,
        const vk::ImageLayout layout) {
        const vk::ImageViewCreateInfo view_create_info{
            {},
            *image.image,
            vk::ImageViewType::e2D,
            image.format,
            {},
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        const vk::ImageDescriptorInfoEXT image_info{&view_create_info, layout};
        const vk::ResourceDescriptorInfoEXT descriptor{
            vk::DescriptorType::eStorageImage,
            vk::ResourceDescriptorDataEXT{&image_info},
        };
        const vk::HostAddressRangeEXT destination{
            static_cast<std::byte*>(this->runtime->descriptors.resource_heap.mapped) +
                static_cast<std::size_t>(handle.index) *
                    this->runtime->descriptors.resource_stride,
            static_cast<std::size_t>(align_up(
                this->runtime->descriptors.properties.imageDescriptorSize,
                this->runtime->descriptors.properties.imageDescriptorAlignment)),
        };
        this->runtime->device.logical_device.writeResourceDescriptorsEXT(
            descriptor,
            destination);
    }

    void GpuDevice::write_sampled_image(
        const DescriptorHandle handle,
        const GpuImage& image,
        const vk::ImageLayout layout) {
        const vk::ImageViewCreateInfo view_create_info{
            {},
            *image.image,
            vk::ImageViewType::e2D,
            image.format,
            {},
            {
                vk::ImageAspectFlagBits::eColor,
                0,
                image.mip_levels,
                0,
                1},
        };
        const vk::ImageDescriptorInfoEXT image_info{&view_create_info, layout};
        const vk::ResourceDescriptorInfoEXT descriptor{
            vk::DescriptorType::eSampledImage,
            vk::ResourceDescriptorDataEXT{&image_info},
        };
        const vk::HostAddressRangeEXT destination{
            static_cast<std::byte*>(this->runtime->descriptors.resource_heap.mapped) +
                static_cast<std::size_t>(handle.index) *
                    this->runtime->descriptors.resource_stride,
            static_cast<std::size_t>(align_up(
                this->runtime->descriptors.properties.imageDescriptorSize,
                this->runtime->descriptors.properties.imageDescriptorAlignment)),
        };
        this->runtime->device.logical_device.writeResourceDescriptorsEXT(
            descriptor,
            destination);
    }

    void GpuDevice::write_buffer(
        const DescriptorHandle handle,
        const vk::DescriptorType type,
        const GpuBuffer& buffer) {
        const vk::DeviceAddressRangeEXT address_range{buffer.address, buffer.size};
        const vk::ResourceDescriptorInfoEXT descriptor{
            type,
            vk::ResourceDescriptorDataEXT{&address_range},
        };
        const vk::HostAddressRangeEXT destination{
            static_cast<std::byte*>(this->runtime->descriptors.resource_heap.mapped) +
                static_cast<std::size_t>(handle.index) *
                    this->runtime->descriptors.resource_stride,
            static_cast<std::size_t>(align_up(
                this->runtime->descriptors.properties.bufferDescriptorSize,
                this->runtime->descriptors.properties.bufferDescriptorAlignment)),
        };
        this->runtime->device.logical_device.writeResourceDescriptorsEXT(
            descriptor,
            destination);
    }

    void GpuDevice::write_sampler(
        const DescriptorHandle handle,
        const vk::SamplerCreateInfo& sampler) {
        const vk::HostAddressRangeEXT destination{
            static_cast<std::byte*>(this->runtime->descriptors.sampler_heap.mapped) +
                static_cast<std::size_t>(handle.index) *
                    this->runtime->descriptors.sampler_stride,
            static_cast<std::size_t>(this->runtime->descriptors.sampler_stride),
        };
        this->runtime->device.logical_device.writeSamplerDescriptorsEXT(
            sampler,
            destination);
    }

    void GpuDevice::bind_descriptor_heaps(
        const vk::raii::CommandBuffer& command_buffer) const noexcept {
        command_buffer.bindResourceHeapEXT(vk::BindHeapInfoEXT{
            {
                this->runtime->descriptors.resource_heap.address,
                this->runtime->descriptors.resource_heap.size,
            },
            this->runtime->descriptors.resource_heap.size -
                this->runtime->descriptors.properties.minResourceHeapReservedRange,
            this->runtime->descriptors.properties.minResourceHeapReservedRange,
        });
        command_buffer.bindSamplerHeapEXT(vk::BindHeapInfoEXT{
            {
                this->runtime->descriptors.sampler_heap.address,
                this->runtime->descriptors.sampler_heap.size,
            },
            this->runtime->descriptors.sampler_heap.size -
                this->runtime->descriptors.properties.minSamplerHeapReservedRange,
            this->runtime->descriptors.properties.minSamplerHeapReservedRange,
        });
    }

    void GpuDevice::push_data(
        const vk::raii::CommandBuffer& command_buffer,
        const std::span<const std::byte> data,
        const std::uint32_t offset) const noexcept {
        command_buffer.pushDataEXT(vk::PushDataInfoEXT{
            offset,
            vk::HostAddressRangeConstEXT{data.data(), data.size_bytes()},
        });
    }
} // namespace spectra
