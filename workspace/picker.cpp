module spectra.workspace.picker;

import std;

namespace spectra::workspace {
    Picker::Picker(
        GpuDevice& gpu,
        const rasterizer::RasterScene& scene,
        const std::filesystem::path& shader_directory,
        const std::uint32_t frames_in_flight)
        : gpu(&gpu), scene(&scene) {
        const std::vector<std::uint32_t> code = load_spirv(shader_directory / "picker.spv");
        vk::DescriptorMappingSourceDataEXT acceleration_structure_source{};
        acceleration_structure_source.pushAddressOffset = 0;
        const vk::DescriptorSetAndBindingMappingEXT acceleration_structure_mapping{
            0,
            0,
            1,
            vk::SpirvResourceTypeFlagBitsEXT::eAccelerationStructure,
            vk::DescriptorMappingSourceEXT::ePushAddress,
            acceleration_structure_source,
        };
        const vk::ShaderDescriptorSetAndBindingMappingInfoEXT mapping{acceleration_structure_mapping};
        vk::ShaderCreateInfoEXT create_info{
            vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
            vk::ShaderStageFlagBits::eCompute,
            {},
            vk::ShaderCodeTypeEXT::eSpirv,
            code.size() * sizeof(std::uint32_t),
            code.data(),
            "pick",
        };
        create_info.pNext = &mapping;
        this->shader = vk::raii::ShaderEXT{gpu.device, create_info};
        this->slots.reserve(frames_in_flight);
        for (std::uint32_t index = 0; index < frames_in_flight; ++index) {
            Slot slot{};
            slot.result = gpu.create_buffer(
                sizeof(std::uint32_t),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                true);
            slot.descriptor = gpu.allocate_resource_descriptor();
            gpu.write_buffer(slot.descriptor, vk::DescriptorType::eStorageBuffer, slot.result);
            this->slots.emplace_back(std::move(slot));
        }
    }

    Picker::~Picker() {
        for (const Slot& slot : this->slots) this->gpu->release_resource_descriptor(slot.descriptor);
    }

    void Picker::request(const PickRequest request) noexcept {
        if (!this->pending || request.select || !this->pending->select) this->pending = request;
    }

    PickResult Picker::consume(const std::uint32_t frame_index) noexcept {
        Slot& slot = this->slots[frame_index];
        if (!slot.submitted) return {};
        const std::uint32_t* values =
            static_cast<
                const std::uint32_t*>(
                slot.result.mapped);
        const PickRequest request = *std::exchange(slot.submitted, std::nullopt);
        return {
            true,
            values[0] ==
                    std::numeric_limits<
                        std::uint32_t>::max()
                ? std::nullopt
                : std::optional{values[0]},
            request.select,
            request.additive,
        };
    }

    void Picker::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_index) {
        if (!this->pending) return;
        Slot& slot = this->slots[frame_index];
        std::uint32_t* result =
            static_cast<std::uint32_t*>(
                slot.result.mapped);
        std::ranges::fill(
            std::span{
                result,
                1},
            std::numeric_limits<
                std::uint32_t>::max());
        slot.submitted = std::exchange(this->pending, std::nullopt);

        struct alignas(16) PickPushData {
            vk::DeviceAddress acceleration_structure_address;
            DescriptorHandle result;
            DescriptorHandle primitives;
            std::array<std::uint32_t, 2> camera_metadata;
            std::array<float, 4> camera_transform_row_0;
            std::array<float, 4> camera_transform_row_1;
            std::array<float, 4> camera_transform_row_2;
            std::array<float, 2> screen;
            float near_plane;
            float far_plane;
        };
        static_assert(sizeof(PickPushData) == 96);
        const scene::CameraResource& camera = this->scene->camera;
        std::array<std::uint32_t, 2> camera_metadata{};
        std::array<float, 2> screen{};
        float near_plane{};
        float far_plane{};
        std::visit(
            [&](const auto& data) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>) {
                    const float tangent = std::tan(data.vertical_fov * std::numbers::pi_v<float> / 360.0f);
                    screen = {
                        std::lerp(data.screen.minimum.x, data.screen.maximum.x, slot.submitted->x) * tangent,
                        std::lerp(data.screen.maximum.y, data.screen.minimum.y, slot.submitted->y) * tangent,
                    };
                } else {
                    camera_metadata[0] = 1;
                    screen = {
                        std::lerp(data.screen.minimum.x, data.screen.maximum.x, slot.submitted->x),
                        std::lerp(data.screen.maximum.y, data.screen.minimum.y, slot.submitted->y),
                    };
                }
                near_plane = data.near_plane;
                far_plane  = data.far_plane;
            },
            camera.data);
        const std::array<float, 16>& transform = camera.transform.matrix;
        const PickPushData push_data{
            this->scene->assets.top_level_acceleration_structure.address,
            slot.descriptor,
            this->scene
                ->pick_primitives_descriptor,
            camera_metadata,
            {transform[0], transform[1], transform[2], transform[3]},
            {transform[4], transform[5], transform[6], transform[7]},
            {transform[8], transform[9], transform[10], transform[11]},
            screen,
            near_plane,
            far_plane,
        };
        const std::array begin_barriers{
            vk::MemoryBarrier2{
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eAccelerationStructureReadKHR,
            },
            vk::MemoryBarrier2{
                vk::PipelineStageFlagBits2::eHost,
                vk::AccessFlagBits2::eHostWrite,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, static_cast<std::uint32_t>(begin_barriers.size()), begin_barriers.data()});
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->shader);
        this->gpu->bind_descriptor_heaps(command_buffer);
        this->gpu->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.dispatch(1, 1, 1);
        const vk::MemoryBarrier2 completion{
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eHost,
            vk::AccessFlagBits2::eHostRead,
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &completion});
    }
}
