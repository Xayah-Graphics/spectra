module spectra.render.sampling;

import std;
import vulkan;

namespace spectra {
    SamplingResources::SamplingResources(VulkanRuntime& runtime, const std::filesystem::path& resource_directory) : runtime(runtime) {
        constexpr std::uint64_t table_size = 6'005'504;
        const std::filesystem::path path   = resource_directory / "sampling.tables";
        std::ifstream stream{path, std::ios::binary};
        std::error_code error{};
        if (!stream || std::filesystem::file_size(path, error) != table_size || error) throw std::runtime_error(std::format("Invalid sampling table: {}", path.string()));
        this->table_data.resize(table_size / sizeof(std::uint32_t));
        stream.read(reinterpret_cast<char*>(this->table_data.data()), table_size);
        if (!stream) throw std::runtime_error(std::format("Cannot read sampling table: {}", path.string()));
        GpuBuffer staging = runtime.resources.create_buffer(table_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        std::memcpy(staging.mapped, this->table_data.data(), table_size);
        this->tables = runtime.resources.create_buffer(table_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
        runtime.resources.submit_immediate([&](const vk::raii::CommandBuffer& command_buffer) {
            command_buffer.copyBuffer(*staging.buffer, *this->tables.buffer, vk::BufferCopy{0, 0, table_size});
            const vk::BufferMemoryBarrier2 barrier{vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eMeshShaderEXT | vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR, vk::AccessFlagBits2::eShaderStorageRead, {}, {}, *this->tables.buffer, 0, table_size};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, barrier, {}});
        });
        this->tables_descriptor = runtime.resources.allocate_resource_descriptor();
        runtime.resources.write_buffer_descriptor(this->tables_descriptor, vk::DescriptorType::eStorageBuffer, this->tables);
    }

    SamplingResources::~SamplingResources() {
        this->runtime.frames.defer_destruction([tables = std::move(this->tables), descriptor = std::move(this->tables_descriptor)]() mutable {});
    }
} // namespace spectra
