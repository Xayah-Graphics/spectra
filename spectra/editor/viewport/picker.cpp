module spectra.editor.viewport.picker;

import std;
import vulkan;

namespace spectra {
    namespace {
        struct alignas(16) ViewportPickPrimitive {
            DescriptorHandle positions{};
            DescriptorHandle radii{};
            std::array<std::uint32_t, 4> metadata{};
            std::array<float, 4> parameters{};
        };
        static_assert(sizeof(ViewportPickPrimitive) == 48);

        [[nodiscard]] GpuBuffer upload_pick_primitives(VulkanRuntime& runtime, const std::span<const ViewportPickPrimitive> primitives, const vk::raii::CommandBuffer* command_buffer) {
            const vk::DeviceSize size = std::max<vk::DeviceSize>(sizeof(ViewportPickPrimitive), primitives.size_bytes());
            if (!command_buffer) {
                GpuBuffer buffer = runtime.resources.create_buffer(size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
                std::memcpy(buffer.mapped, primitives.data(), primitives.size_bytes());
                return buffer;
            }
            GpuBuffer buffer            = runtime.resources.create_buffer(size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            const GpuUploadSlice upload = runtime.frames.stage_upload(std::as_bytes(primitives));
            command_buffer->copyBuffer(upload.buffer, *buffer.buffer, vk::BufferCopy{upload.offset, 0, upload.size});
            const vk::BufferMemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *buffer.buffer, 0, buffer.size};
            command_buffer->pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, 1, &dependency});
            return buffer;
        }
    } // namespace

    ViewportPicker::ViewportPicker(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory) noexcept : context{runtime, gpu_scene, std::move(shader_directory)} {}

    ViewportPicker::~ViewportPicker() {
        this->destroy_scene();
    }

    void ViewportPicker::initialize(const scene::SceneView source_scene) {
        this->scene.initialized = true;
        if (this->context.runtime.graphics.ray_tracing_supported) {
            this->scene.primitives_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->upload(source_scene);
            const std::vector<std::uint32_t> code = load_spirv(this->context.shader_directory / "picker.spv");
            vk::DescriptorMappingSourceDataEXT acceleration_structure_source{};
            acceleration_structure_source.pushAddressOffset = 0;
            const vk::DescriptorSetAndBindingMappingEXT acceleration_structure_mapping{0, 0, 1, vk::SpirvResourceTypeFlagBitsEXT::eAccelerationStructure, vk::DescriptorMappingSourceEXT::ePushAddress, acceleration_structure_source};
            const vk::ShaderDescriptorSetAndBindingMappingInfoEXT mapping{acceleration_structure_mapping};
            vk::ShaderCreateInfoEXT create_info{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, code.size() * sizeof(std::uint32_t), code.data(), "pick"};
            create_info.pNext    = &mapping;
            this->picking.shader = vk::raii::ShaderEXT{this->context.runtime.graphics.device, create_info};
        }
        this->picking.frame_slots.reserve(VulkanFrames::frames_in_flight);
        for (std::uint32_t index = 0; index != VulkanFrames::frames_in_flight; ++index) {
            PickFrameSlot slot{};
            slot.result_buffer     = this->context.runtime.resources.create_buffer(32, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            slot.result_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(slot.result_descriptor, vk::DescriptorType::eStorageBuffer, slot.result_buffer);
            this->picking.frame_slots.emplace_back(std::move(slot));
        }
    }

    void ViewportPicker::destroy_scene() noexcept {
        this->context.runtime.frames.defer_destruction([slots = std::move(this->picking.frame_slots), shader = std::move(this->picking.shader), primitives = std::move(this->scene.primitives)]() mutable {});
        this->picking.pending_request.reset();
        this->scene.primitives_descriptor = {};
        if (!this->scene.initialized) return;
        this->scene.initialized = false;
    }

    void ViewportPicker::synchronize(const scene::SceneView source_scene, const GpuSceneUpdate gpu_update, const vk::raii::CommandBuffer& command_buffer) {
        if (this->context.runtime.graphics.ray_tracing_supported && ((source_scene.revision.changes & scene::SceneChange::Geometry) != scene::SceneChange::None || (gpu_update.gpu_changes & GpuSceneChange::Structure) != GpuSceneChange::None)) this->upload(source_scene, &command_buffer);
    }

    void ViewportPicker::submit_pick(const float normalized_x, const float normalized_y, const bool select, const bool additive) noexcept {
        const PickRequest request{normalized_x, normalized_y, select, additive};
        if (!this->picking.pending_request || request.select || !this->picking.pending_request->select) this->picking.pending_request = request;
    }

    void ViewportPicker::cancel_selection_requests() noexcept {
        if (this->picking.pending_request && this->picking.pending_request->select) this->picking.pending_request.reset();
        for (PickFrameSlot& slot : this->picking.frame_slots)
            if (slot.submitted_request && slot.submitted_request->select) slot.submitted_request.reset();
    }

    ViewportPicker::PickResult ViewportPicker::take_pick_result(const std::uint32_t frame_slot_index) noexcept {
        PickFrameSlot& slot = this->picking.frame_slots[frame_slot_index];
        if (!slot.submitted_request) return {};
        const std::uint32_t* values = static_cast<const std::uint32_t*>(slot.result_buffer.mapped);
        const PickRequest request   = *std::exchange(slot.submitted_request, std::nullopt);
        const std::uint32_t acceleration_instance_index = values[0];
        return {
            true,
            acceleration_instance_index == std::numeric_limits<std::uint32_t>::max() ? std::nullopt : std::optional{slot.acceleration_instance_ids[acceleration_instance_index]},
            values[4] == 0 ? std::nullopt : std::optional{values[4]},
            request.select,
            request.additive,
        };
    }

    void ViewportPicker::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index, const scene::Camera& camera, const DepthBufferView depth, const GpuImage* diagnostic_pick_image) {
        if (!this->picking.pending_request) return;
        PickFrameSlot& slot   = this->picking.frame_slots[frame_slot_index];
        std::uint32_t* result = static_cast<std::uint32_t*>(slot.result_buffer.mapped);
        std::ranges::fill(std::span{result, 8}, std::numeric_limits<std::uint32_t>::max());
        result[4]              = 0;
        slot.submitted_request = std::exchange(this->picking.pending_request, std::nullopt);
        slot.acceleration_instance_ids.assign(this->context.gpu_scene.view().acceleration_instance_ids.begin(), this->context.gpu_scene.view().acceleration_instance_ids.end());

        struct alignas(16) PickPushData {
            vk::DeviceAddress acceleration_structure_address;
            DescriptorHandle result_descriptor;
            DescriptorHandle primitive_descriptor;
            DescriptorHandle depth_descriptor;
            std::array<std::uint32_t, 2> camera_metadata;
            std::array<std::uint32_t, 2> pixel;
            std::array<float, 4> camera_transform_row_0;
            std::array<float, 4> camera_transform_row_1;
            std::array<float, 4> camera_transform_row_2;
            std::array<float, 2> screen;
            float near_plane;
            float far_plane;
        };
        static_assert(sizeof(PickPushData) == 112);
        std::array<std::uint32_t, 2> camera_metadata{};
        std::array<float, 2> screen{};
        float near_plane{};
        float far_plane{};
        std::visit(
            [&](const auto& data) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>) {
                    const float tangent = std::tan(data.vertical_fov * std::numbers::pi_v<float> / 360.0f);
                    screen              = {
                        std::lerp(data.screen_window.minimum.x, data.screen_window.maximum.x, slot.submitted_request->normalized_x) * tangent,
                        std::lerp(data.screen_window.maximum.y, data.screen_window.minimum.y, slot.submitted_request->normalized_y) * tangent,
                    };
                } else {
                    camera_metadata[0] = 1;
                    screen             = {
                        std::lerp(data.screen_window.minimum.x, data.screen_window.maximum.x, slot.submitted_request->normalized_x),
                        std::lerp(data.screen_window.maximum.y, data.screen_window.minimum.y, slot.submitted_request->normalized_y),
                    };
                }
                near_plane = data.near_plane;
                far_plane  = data.far_plane;
            },
            camera.data);
        const std::array<float, 16>& transform = camera.transform.matrix;
        const PickPushData push_data{
            this->context.gpu_scene.view().acceleration_structure,
            slot.result_descriptor,
            this->scene.primitives_descriptor,
            depth.descriptor,
            camera_metadata,
            {
                std::min(static_cast<std::uint32_t>(slot.submitted_request->normalized_x * static_cast<float>(depth.image.extent.width)), depth.image.extent.width - 1u),
                std::min(static_cast<std::uint32_t>(slot.submitted_request->normalized_y * static_cast<float>(depth.image.extent.height)), depth.image.extent.height - 1u),
            },
            {transform[0], transform[1], transform[2], transform[3]},
            {transform[4], transform[5], transform[6], transform[7]},
            {transform[8], transform[9], transform[10], transform[11]},
            screen,
            near_plane,
            far_plane,
        };
        if (this->context.runtime.graphics.ray_tracing_supported) {
            const std::array begin_barriers{
                vk::MemoryBarrier2{vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureWriteKHR, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eAccelerationStructureReadKHR},
                vk::MemoryBarrier2{vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite},
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, static_cast<std::uint32_t>(begin_barriers.size()), begin_barriers.data()});
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->picking.shader);
            this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.dispatch(1, 1, 1);
        }
        if (diagnostic_pick_image) {
            const std::uint32_t pixel_x = std::min(static_cast<std::uint32_t>(slot.submitted_request->normalized_x * static_cast<float>(diagnostic_pick_image->extent.width)), diagnostic_pick_image->extent.width - 1u);
            const std::uint32_t pixel_y = std::min(static_cast<std::uint32_t>(slot.submitted_request->normalized_y * static_cast<float>(diagnostic_pick_image->extent.height)), diagnostic_pick_image->extent.height - 1u);
            const vk::BufferImageCopy pick_copy{16, 0, 0, {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {static_cast<std::int32_t>(pixel_x), static_cast<std::int32_t>(pixel_y), 0}, {1, 1, 1}};
            command_buffer.copyImageToBuffer(*diagnostic_pick_image->image, vk::ImageLayout::eTransferSrcOptimal, *slot.result_buffer.buffer, pick_copy);
        }
        const vk::MemoryBarrier2 completion{vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &completion});
    }
    void ViewportPicker::upload(const scene::SceneView source_scene, const vk::raii::CommandBuffer* command_buffer) {
        std::vector<ViewportPickPrimitive> primitives{};
        primitives.reserve(this->context.gpu_scene.view().acceleration_primitive_indices.size());
        const auto volume_medium = [&source_scene](const scene::MediumId medium_id) {
            if (medium_id.value == 0) return false;
            const scene::Medium& medium = *std::ranges::find(source_scene.resources.media, medium_id, &scene::Medium::id);
            return std::holds_alternative<scene::VolumeMedium>(medium.data);
        };
        for (const std::uint32_t scene_primitive_index : this->context.gpu_scene.view().acceleration_primitive_indices) {
            const GpuScenePrimitive& gpu_primitive = this->context.gpu_scene.view().primitives[scene_primitive_index];
            const scene::Instance& instance        = source_scene.resources.instances[gpu_primitive.scene_instance_index];
            const scene::Prototype& prototype      = *std::ranges::find(source_scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
            const scene::Primitive& primitive      = prototype.primitives[gpu_primitive.prototype_primitive_index];
            ViewportPickPrimitive pick{};
            pick.metadata[1] = volume_medium(primitive.media.inside) || volume_medium(primitive.media.outside) ? 1u : 0u;
            if (gpu_primitive.kind == GpuScenePrimitiveKind::SphereSet) {
                const GpuSphereSet& spheres = this->context.gpu_scene.view().sphere_sets[gpu_primitive.resource_index];
                pick.positions              = spheres.positions_descriptor;
                pick.radii                  = spheres.radii_descriptor;
                pick.metadata[0]            = 4;
                primitives.push_back(pick);
                continue;
            }
            const scene::Geometry& geometry   = *std::ranges::find(source_scene.resources.geometries, primitive.geometry, &scene::Geometry::id);
            std::visit(
                [&pick](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::SphereGeometry>) {
                        pick.metadata[0] = 1;
                        pick.parameters  = {data.radius, data.z_min, data.z_max, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiskGeometry>) {
                        pick.metadata[0] = 2;
                        pick.parameters  = {data.height, data.radius, data.inner_radius, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CylinderGeometry>) {
                        pick.metadata[0] = 3;
                        pick.parameters  = {data.radius, data.z_min, data.z_max, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                    }
                },
                geometry.data);
            primitives.push_back(pick);
        }
        if (primitives.empty()) primitives.emplace_back();
        GpuBuffer new_primitives = upload_pick_primitives(this->context.runtime, primitives, command_buffer);
        if (command_buffer) {
            DescriptorLease descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(descriptor, vk::DescriptorType::eStorageBuffer, new_primitives);
            this->context.runtime.frames.defer_destruction([primitives = std::move(this->scene.primitives)]() mutable {});
            this->scene.primitives_descriptor = std::move(descriptor);
        } else {
            this->context.runtime.resources.write_buffer_descriptor(this->scene.primitives_descriptor, vk::DescriptorType::eStorageBuffer, new_primitives);
        }
        this->scene.primitives = std::move(new_primitives);
    }

} // namespace spectra
