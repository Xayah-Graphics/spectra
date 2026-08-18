module spectra.editor.viewport.picker;

import spectra.editor.shader_abi;
import std;
import vulkan;

namespace spectra::editor {
    namespace {
        struct alignas(16) ViewportPickPrimitive {
            runtime::DescriptorHandle positions{};
            runtime::DescriptorHandle radii{};
            std::array<std::uint32_t, 4> metadata{};
            std::array<float, 4> parameters{};
        };
        static_assert(sizeof(ViewportPickPrimitive) == 48);

        [[nodiscard]] runtime::GpuBuffer upload_pick_primitives(runtime::VulkanRuntime& runtime, const std::span<const ViewportPickPrimitive> primitives, const vk::raii::CommandBuffer* command_buffer) {
            const vk::DeviceSize size = std::max<vk::DeviceSize>(sizeof(ViewportPickPrimitive), primitives.size_bytes());
            if (!command_buffer) {
                runtime::GpuBuffer buffer = runtime.resources.create_buffer(size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
                std::memcpy(buffer.mapped, primitives.data(), primitives.size_bytes());
                return buffer;
            }
            runtime::GpuBuffer buffer            = runtime.resources.create_buffer(size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            const runtime::GpuUploadSlice upload = runtime.frames.stage_upload(std::as_bytes(primitives));
            command_buffer->copyBuffer(upload.buffer, *buffer.buffer, vk::BufferCopy{upload.offset, 0, upload.size});
            const vk::BufferMemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *buffer.buffer, 0, buffer.size};
            command_buffer->pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, 1, &dependency});
            return buffer;
        }
    } // namespace

    Picker::Picker(runtime::VulkanRuntime& runtime, render::GpuScene& gpu_scene, std::filesystem::path shader_directory) noexcept : context{runtime, gpu_scene, std::move(shader_directory)} {}

    Picker::~Picker() {
        this->destroy_scene();
    }

    void Picker::initialize(const scene::ResolvedSceneView scene_view) {
        this->scene.initialized = true;
        if (this->context.runtime.device.ray_tracing_supported) {
            this->scene.primitives_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->upload(scene_view);
            const std::vector<std::uint32_t> code = runtime::load_spirv(this->context.shader_directory / "picker.spv");
            vk::DescriptorMappingSourceDataEXT acceleration_structure_source{};
            acceleration_structure_source.pushAddressOffset = 0;
            const vk::DescriptorSetAndBindingMappingEXT acceleration_structure_mapping{0, 0, 1, vk::SpirvResourceTypeFlagBitsEXT::eAccelerationStructure, vk::DescriptorMappingSourceEXT::ePushAddress, acceleration_structure_source};
            const vk::ShaderDescriptorSetAndBindingMappingInfoEXT mapping{acceleration_structure_mapping};
            vk::ShaderCreateInfoEXT create_info{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, code.size() * sizeof(std::uint32_t), code.data(), "pick"};
            create_info.pNext    = &mapping;
            this->picking.shader = vk::raii::ShaderEXT{this->context.runtime.device.logical, create_info};
        }
        this->picking.frame_slots.reserve(runtime::VulkanFrames::frames_in_flight);
        for (std::uint32_t index = 0; index != runtime::VulkanFrames::frames_in_flight; ++index) {
            PickFrameSlot slot{};
            slot.result_buffer     = this->context.runtime.resources.create_buffer(32, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            slot.result_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(slot.result_descriptor, vk::DescriptorType::eStorageBuffer, slot.result_buffer);
            this->picking.frame_slots.emplace_back(std::move(slot));
        }
    }

    void Picker::destroy_scene() noexcept {
        this->context.runtime.frames.defer_destruction([slots = std::move(this->picking.frame_slots), shader = std::move(this->picking.shader), primitives = std::move(this->scene.primitives)]() mutable {});
        this->picking.pending_request.reset();
        this->scene.primitives_descriptor = {};
        if (!this->scene.initialized) return;
        this->scene.initialized = false;
    }

    void Picker::synchronize(const scene::ResolvedSceneView scene_view, const render::GpuSceneUpdate gpu_update, const vk::raii::CommandBuffer& command_buffer) {
        if (this->context.runtime.device.ray_tracing_supported && ((scene_view.revision.changes & (scene::SceneChange::Geometry | scene::SceneChange::Volume)) != scene::SceneChange::None || (gpu_update.gpu_changes & render::GpuSceneChange::Structure) != render::GpuSceneChange::None)) this->upload(scene_view, &command_buffer);
    }

    void Picker::submit_pick(const float normalized_x, const float normalized_y, const bool select, const bool additive) noexcept {
        const PickRequest request{normalized_x, normalized_y, select, additive};
        if (!this->picking.pending_request || request.select || !this->picking.pending_request->select) this->picking.pending_request = request;
    }

    void Picker::cancel_selection_requests() noexcept {
        if (this->picking.pending_request && this->picking.pending_request->select) this->picking.pending_request.reset();
        for (PickFrameSlot& slot : this->picking.frame_slots)
            if (slot.submitted_request && slot.submitted_request->select) slot.submitted_request.reset();
    }

    Picker::PickResult Picker::take_pick_result(const std::uint32_t frame_slot_index) noexcept {
        PickFrameSlot& slot = this->picking.frame_slots[frame_slot_index];
        if (!slot.submitted_request) return {};
        const std::uint32_t* values                     = static_cast<const std::uint32_t*>(slot.result_buffer.mapped);
        const PickRequest request                       = *std::exchange(slot.submitted_request, std::nullopt);
        const std::uint32_t acceleration_instance_index = values[0];
        return {
            true,
            acceleration_instance_index == std::numeric_limits<std::uint32_t>::max() ? std::nullopt : std::optional{slot.acceleration_entities[acceleration_instance_index]},
            values[5] == 0 ? std::nullopt : slot.neural_field,
            values[4] == 0 ? std::nullopt : std::optional{values[4]},
            request.select,
            request.additive,
        };
    }

    void Picker::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index, const scene::ResolvedSceneView scene_view, const scene::Camera& camera, const render::DepthBufferView depth, const runtime::GpuImage* diagnostic_pick_image) {
        if (!this->picking.pending_request) return;
        PickFrameSlot& slot   = this->picking.frame_slots[frame_slot_index];
        std::uint32_t* result = static_cast<std::uint32_t*>(slot.result_buffer.mapped);
        std::ranges::fill(std::span{result, 8}, std::numeric_limits<std::uint32_t>::max());
        result[4]              = 0;
        result[5]              = 0;
        slot.submitted_request = std::exchange(this->picking.pending_request, std::nullopt);
        slot.acceleration_entities.assign(this->context.gpu_scene.view().acceleration_entities.begin(), this->context.gpu_scene.view().acceleration_entities.end());
        const std::vector<scene::NeuralField>::const_iterator neural_field = std::ranges::find_if(scene_view.resources.neural_fields, [](const scene::NeuralField& field) { return field.visible; });
        slot.neural_field                                                  = neural_field == scene_view.resources.neural_fields.end() ? std::nullopt : std::optional{neural_field->id};

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
        camera_metadata[1]                         = slot.neural_field.has_value() ? 1u : 0u;
        const std::array<float, 16>& transform     = camera.transform.matrix;
        const math::Transform neural_field_inverse = slot.neural_field ? neural_field->transform.inverse() : math::Transform{};
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
            {neural_field_inverse.matrix[0], neural_field_inverse.matrix[1], neural_field_inverse.matrix[2], neural_field_inverse.matrix[3]},
            {neural_field_inverse.matrix[4], neural_field_inverse.matrix[5], neural_field_inverse.matrix[6], neural_field_inverse.matrix[7]},
            {neural_field_inverse.matrix[8], neural_field_inverse.matrix[9], neural_field_inverse.matrix[10], neural_field_inverse.matrix[11]},
        };
        if (this->context.runtime.device.ray_tracing_supported) {
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
    void Picker::upload(const scene::ResolvedSceneView scene_view, const vk::raii::CommandBuffer* command_buffer) {
        std::vector<ViewportPickPrimitive> primitives{};
        primitives.reserve(this->context.gpu_scene.view().acceleration_entities.size());
        for (const render::GpuAccelerationEntity entity : this->context.gpu_scene.view().acceleration_entities) {
            if (entity.kind == render::GpuAccelerationEntityKind::Volume) {
                ViewportPickPrimitive pick{};
                pick.metadata[1] = 1u;
                primitives.push_back(pick);
                continue;
            }
            const std::uint32_t scene_primitive_index      = entity.resource_index;
            const render::GpuScenePrimitive& gpu_primitive = this->context.gpu_scene.view().primitives[scene_primitive_index];
            const scene::Instance& instance                = scene_view.resources.instances[gpu_primitive.scene_instance_index];
            const scene::Prototype& prototype              = *std::ranges::find(scene_view.resources.prototypes, instance.prototype, &scene::Prototype::id);
            const scene::Primitive& primitive              = prototype.primitives[gpu_primitive.prototype_primitive_index];
            ViewportPickPrimitive pick{};
            const std::vector<scene::Material>::const_iterator material = std::ranges::find(scene_view.resources.materials, primitive.material, &scene::Material::id);
            pick.metadata[1]                                            = (primitive.media.inside.value != 0u || primitive.media.outside.value != 0u) && material != scene_view.resources.materials.end() && std::holds_alternative<scene::InterfaceMaterialData>(material->data) ? 1u : 0u;
            if (gpu_primitive.kind == render::GpuScenePrimitiveKind::SphereSet) {
                const render::GpuSphereSet& spheres = this->context.gpu_scene.view().sphere_sets[gpu_primitive.resource_index];
                pick.positions                      = spheres.positions_descriptor;
                pick.radii                          = spheres.radii_descriptor;
                pick.metadata[0]                    = 4;
                primitives.push_back(pick);
                continue;
            }
            const scene::Geometry& geometry = *std::ranges::find(scene_view.resources.geometries, primitive.geometry, &scene::Geometry::id);
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
        runtime::GpuBuffer new_primitives = upload_pick_primitives(this->context.runtime, primitives, command_buffer);
        if (command_buffer) {
            runtime::DescriptorLease descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(descriptor, vk::DescriptorType::eStorageBuffer, new_primitives);
            this->context.runtime.frames.defer_destruction([primitives = std::move(this->scene.primitives)]() mutable {});
            this->scene.primitives_descriptor = std::move(descriptor);
        } else {
            this->context.runtime.resources.write_buffer_descriptor(this->scene.primitives_descriptor, vk::DescriptorType::eStorageBuffer, new_primitives);
        }
        this->scene.primitives = std::move(new_primitives);
    }

} // namespace spectra::editor
