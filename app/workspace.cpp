module spectra.workspace;

import std;

namespace spectra::workspace {
    Picker::Picker(Spectra& runtime, const rasterizer::RasterScene& scene, const std::filesystem::path& shader_directory, const std::uint32_t frames_in_flight) : runtime(&runtime), scene(&scene) {
        const std::vector<std::uint32_t> code = render::load_spirv(shader_directory / "picker.spv");
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
        this->shader      = vk::raii::ShaderEXT{runtime.device, create_info};
        this->slots.reserve(frames_in_flight);
        for (std::uint32_t index = 0; index < frames_in_flight; ++index) {
            Slot slot{};
            slot.result     = runtime.create_buffer(sizeof(std::uint32_t), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            slot.descriptor = runtime.allocate_resource_descriptor();
            runtime.write_buffer_descriptor(slot.descriptor, vk::DescriptorType::eStorageBuffer, slot.result);
            this->slots.emplace_back(std::move(slot));
        }
    }

    Picker::~Picker() {
        for (const Slot& slot : this->slots) this->runtime->release_resource_descriptor(slot.descriptor);
    }

    void Picker::request(const PickRequest request) noexcept {
        if (!this->pending || request.select || !this->pending->select) this->pending = request;
    }

    PickResult Picker::consume(const std::uint32_t frame_index) noexcept {
        Slot& slot = this->slots[frame_index];
        if (!slot.submitted) return {};
        const std::uint32_t* values = static_cast<const std::uint32_t*>(slot.result.mapped);
        const PickRequest request   = *std::exchange(slot.submitted, std::nullopt);
        return {
            true,
            values[0] == std::numeric_limits<std::uint32_t>::max() ? std::nullopt : std::optional{values[0]},
            request.debug_object,
            request.debug_xray,
            request.select,
            request.additive,
        };
    }

    void Picker::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_index) {
        if (!this->pending) return;
        Slot& slot            = this->slots[frame_index];
        std::uint32_t* result = static_cast<std::uint32_t*>(slot.result.mapped);
        std::ranges::fill(std::span{result, 1}, std::numeric_limits<std::uint32_t>::max());
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
                    screen              = {
                        std::lerp(data.screen.minimum.x, data.screen.maximum.x, slot.submitted->x) * tangent,
                        std::lerp(data.screen.maximum.y, data.screen.minimum.y, slot.submitted->y) * tangent,
                    };
                } else {
                    camera_metadata[0] = 1;
                    screen             = {
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
            this->scene->gpu_scene.top_level_acceleration_structure.address,
            slot.descriptor,
            this->scene->pick_primitives_descriptor,
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
        this->runtime->bind_descriptor_heaps(command_buffer);
        this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.dispatch(1, 1, 1);
        const vk::MemoryBarrier2 completion{
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eHost,
            vk::AccessFlagBits2::eHostRead,
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &completion});
    }
    namespace {
        struct alignas(16) MaskPushData {
            DescriptorHandle positions;
            DescriptorHandle indices;
            DescriptorHandle radii;
            DescriptorHandle primitives;
            DescriptorHandle transforms;
            std::uint32_t instance_index;
            std::uint32_t element_count;
            std::uint32_t draw_kind;
            std::uint32_t reserved_0;
            std::array<std::uint32_t, 2> reserved_1;
            std::array<float, 4> color;
            std::array<float, 4> view_projection_row_0;
            std::array<float, 4> view_projection_row_1;
            std::array<float, 4> view_projection_row_2;
            std::array<float, 4> view_projection_row_3;
        };
        static_assert(sizeof(MaskPushData) == 144);

        struct alignas(16) AxesPushData {
            std::array<float, 4> view_projection_row_0;
            std::array<float, 4> view_projection_row_1;
            std::array<float, 4> view_projection_row_2;
            std::array<float, 4> view_projection_row_3;
            std::array<float, 4> inverse_view_projection_row_0;
            std::array<float, 4> inverse_view_projection_row_1;
            std::array<float, 4> inverse_view_projection_row_2;
            std::array<float, 4> inverse_view_projection_row_3;
            std::array<std::uint32_t, 4> metadata;
        };
        static_assert(sizeof(AxesPushData) == 144);

        struct alignas(16) DebugVertex {
            std::array<float, 4> position{};
            std::array<float, 4> color{};
        };
        static_assert(sizeof(DebugVertex) == 32);

        struct alignas(16) DebugPushData {
            DescriptorHandle vertices{};
            std::array<std::uint32_t, 2> reserved{};
            std::array<float, 4> view_projection_row_0{};
            std::array<float, 4> view_projection_row_1{};
            std::array<float, 4> view_projection_row_2{};
            std::array<float, 4> view_projection_row_3{};
        };
        static_assert(sizeof(DebugPushData) == 80);

        struct alignas(16) VolumeVectorPushData {
            DescriptorHandle velocity{};
            std::array<std::uint32_t, 2> reserved{};
            std::array<std::uint32_t, 4> resolution{};
            std::array<float, 4> bounds_minimum{};
            std::array<float, 4> bounds_maximum{};
            std::array<float, 4> transform_row_0{};
            std::array<float, 4> transform_row_1{};
            std::array<float, 4> transform_row_2{};
            std::array<float, 4> view_projection_row_0{};
            std::array<float, 4> view_projection_row_1{};
            std::array<float, 4> view_projection_row_2{};
            std::array<float, 4> view_projection_row_3{};
            std::array<float, 4> parameters{};
        };
        static_assert(sizeof(VolumeVectorPushData) == 192);

        void configure_mask_render_state(const vk::raii::CommandBuffer& command_buffer, const vk::Rect2D render_region, const vk::CompareOp depth_compare, const bool depth_write) {
            command_buffer.setViewportWithCount(vk::Viewport{
                static_cast<float>(render_region.offset.x),
                static_cast<float>(render_region.offset.y) + static_cast<float>(render_region.extent.height),
                static_cast<float>(render_region.extent.width),
                -static_cast<float>(render_region.extent.height),
                0.0f,
                1.0f,
            });
            command_buffer.setScissorWithCount(render_region);
            command_buffer.setCullMode(vk::CullModeFlagBits::eNone);
            command_buffer.setFrontFace(vk::FrontFace::eCounterClockwise);
            command_buffer.setDepthTestEnable(vk::True);
            command_buffer.setDepthWriteEnable(depth_write);
            command_buffer.setDepthCompareOp(depth_compare);
            command_buffer.setRasterizerDiscardEnable(vk::False);
            command_buffer.setPolygonModeEXT(vk::PolygonMode::eFill);
            command_buffer.setRasterizationSamplesEXT(vk::SampleCountFlagBits::e1);
            command_buffer.setAlphaToCoverageEnableEXT(vk::False);
            command_buffer.setDepthBiasEnable(vk::False);
            command_buffer.setStencilTestEnable(vk::False);
            constexpr vk::SampleMask sample_mask = 1;
            command_buffer.setSampleMaskEXT(vk::SampleCountFlagBits::e1, sample_mask);
            constexpr vk::Bool32 blend_enable = vk::False;
            command_buffer.setColorBlendEnableEXT(0, blend_enable);
            constexpr vk::ColorComponentFlags color_components = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            command_buffer.setColorWriteMaskEXT(0, color_components);
        }
    } // namespace

    OverlayRenderer::OverlayRenderer(Spectra& runtime, const rasterizer::RasterScene& scene, const std::filesystem::path& shader_directory) : runtime(&runtime), scene(&scene), mask_descriptor(runtime.allocate_resource_descriptor()), sampler_descriptor(runtime.allocate_sampler_descriptor()), debug_descriptor(runtime.allocate_resource_descriptor()) {
        const std::vector<std::uint32_t> mask_mesh_code     = render::load_spirv(shader_directory / "overlay_mesh.spv");
        const std::vector<std::uint32_t> mask_fragment_code = render::load_spirv(shader_directory / "overlay_mask.spv");
        const std::array mask_create_infos{
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap | vk::ShaderCreateFlagBitsEXT::eNoTaskShader,
                vk::ShaderStageFlagBits::eMeshEXT,
                vk::ShaderStageFlagBits::eFragment,
                vk::ShaderCodeTypeEXT::eSpirv,
                mask_mesh_code.size() * sizeof(std::uint32_t),
                mask_mesh_code.data(),
                "overlay_mesh",
            },
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eFragment,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                mask_fragment_code.size() * sizeof(std::uint32_t),
                mask_fragment_code.data(),
                "overlay_mask",
            },
        };
        this->mask_shaders = vk::raii::ShaderEXTs{runtime.device, mask_create_infos};

        const std::vector<std::uint32_t> axes_vertex_code   = render::load_spirv(shader_directory / "overlay_axes_vertex.spv");
        const std::vector<std::uint32_t> axes_fragment_code = render::load_spirv(shader_directory / "overlay_axes_fragment.spv");
        const std::array axes_create_infos{
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eVertex,
                vk::ShaderStageFlagBits::eFragment,
                vk::ShaderCodeTypeEXT::eSpirv,
                axes_vertex_code.size() * sizeof(std::uint32_t),
                axes_vertex_code.data(),
                "overlay_axes_vertex",
            },
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eFragment,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                axes_fragment_code.size() * sizeof(std::uint32_t),
                axes_fragment_code.data(),
                "overlay_axes_fragment",
            },
        };
        this->axes_shaders = vk::raii::ShaderEXTs{runtime.device, axes_create_infos};

        const std::vector<std::uint32_t> outline_vertex_code   = render::load_spirv(shader_directory / "overlay_vertex.spv");
        const std::vector<std::uint32_t> outline_fragment_code = render::load_spirv(shader_directory / "overlay_outline.spv");
        const std::array outline_create_infos{
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eVertex,
                vk::ShaderStageFlagBits::eFragment,
                vk::ShaderCodeTypeEXT::eSpirv,
                outline_vertex_code.size() * sizeof(std::uint32_t),
                outline_vertex_code.data(),
                "overlay_vertex",
            },
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eFragment,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                outline_fragment_code.size() * sizeof(std::uint32_t),
                outline_fragment_code.data(),
                "overlay_outline",
            },
        };
        this->outline_shaders                                = vk::raii::ShaderEXTs{runtime.device, outline_create_infos};
        const std::vector<std::uint32_t> debug_vertex_code   = render::load_spirv(shader_directory / "overlay_debug_vertex.spv");
        const std::vector<std::uint32_t> debug_fragment_code = render::load_spirv(shader_directory / "overlay_debug_fragment.spv");
        const std::array debug_create_infos{vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment, vk::ShaderCodeTypeEXT::eSpirv, debug_vertex_code.size() * sizeof(std::uint32_t), debug_vertex_code.data(), "overlay_debug_vertex"}, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eFragment, {}, vk::ShaderCodeTypeEXT::eSpirv, debug_fragment_code.size() * sizeof(std::uint32_t), debug_fragment_code.data(), "overlay_debug_fragment"}};
        this->debug_shaders                                        = vk::raii::ShaderEXTs{runtime.device, debug_create_infos};
        const std::vector<std::uint32_t> volume_vector_vertex_code = render::load_spirv(shader_directory / "overlay_volume_vector_vertex.spv");
        const std::array volume_vector_create_infos{vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment, vk::ShaderCodeTypeEXT::eSpirv, volume_vector_vertex_code.size() * sizeof(std::uint32_t), volume_vector_vertex_code.data(), "overlay_volume_vector_vertex"}, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eFragment, {}, vk::ShaderCodeTypeEXT::eSpirv, debug_fragment_code.size() * sizeof(std::uint32_t), debug_fragment_code.data(), "overlay_debug_fragment"}};
        this->volume_vector_shaders = vk::raii::ShaderEXTs{runtime.device, volume_vector_create_infos};
        runtime.write_sampler_descriptor(this->sampler_descriptor, vk::SamplerCreateInfo{
                                                                       {},
                                                                       vk::Filter::eNearest,
                                                                       vk::Filter::eNearest,
                                                                       vk::SamplerMipmapMode::eNearest,
                                                                       vk::SamplerAddressMode::eClampToEdge,
                                                                       vk::SamplerAddressMode::eClampToEdge,
                                                                       vk::SamplerAddressMode::eClampToEdge,
                                                                   });
    }

    OverlayRenderer::~OverlayRenderer() {
        this->runtime->release_resource_descriptor(this->mask_descriptor);
        this->runtime->release_sampler_descriptor(this->sampler_descriptor);
        this->runtime->release_resource_descriptor(this->debug_descriptor);
    }

    void OverlayRenderer::create_images(const vk::Extent2D extent) {
        this->mask  = this->runtime->create_image_2d(extent, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled);
        this->depth = this->runtime->create_image_2d(extent, vk::Format::eD32Sfloat, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::ImageAspectFlagBits::eDepth);
        this->runtime->write_sampled_image_descriptor(this->mask_descriptor, this->mask, vk::ImageLayout::eShaderReadOnlyOptimal);
        this->mask_layout  = vk::ImageLayout::eUndefined;
        this->depth_layout = vk::ImageLayout::eUndefined;
    }

    void OverlayRenderer::record(const vk::raii::CommandBuffer& command_buffer, const vk::Image target_image, const vk::ImageView target_view, const vk::Extent2D extent, const vk::Rect2D render_region, const std::span<const std::uint32_t> selected_instances, const std::span<const std::uint32_t> active_instances, const std::span<const std::uint32_t> hovered_instances, const std::uint32_t axes_plane, const bool axes_visible, const bool outline_visible, const bool raster_visualizations, const std::span<const scene::dynamics::DebugPrimitive> debug_primitives, const std::span<const render::VolumeVectorField> volume_vector_fields) {
        std::vector<DebugVertex> tested_vertices{};
        std::vector<DebugVertex> xray_vertices{};
        const auto add_line = [](std::vector<DebugVertex>& vertices, const scene::Float3 first, const scene::Float3 second, const scene::Float3 color) {
            vertices.push_back({{first.x, first.y, first.z, 1.0f}, {color.x, color.y, color.z, 1.0f}});
            vertices.push_back({{second.x, second.y, second.z, 1.0f}, {color.x, color.y, color.z, 1.0f}});
        };
        for (const scene::dynamics::DebugPrimitive& primitive : debug_primitives) {
            std::vector<DebugVertex>& vertices = primitive.depth_mode == scene::dynamics::DebugDepthMode::Tested ? tested_vertices : xray_vertices;
            if (primitive.kind == scene::dynamics::DebugPrimitiveKind::Line || primitive.kind == scene::dynamics::DebugPrimitiveKind::Constraint) {
                add_line(vertices, primitive.first, primitive.second, primitive.color);
                continue;
            }
            if (primitive.kind == scene::dynamics::DebugPrimitiveKind::Point || primitive.kind == scene::dynamics::DebugPrimitiveKind::Contact) {
                const float radius = std::max(primitive.radius, 1.0e-4f);
                add_line(vertices, primitive.first - scene::Float3{radius, 0.0f, 0.0f}, primitive.first + scene::Float3{radius, 0.0f, 0.0f}, primitive.color);
                add_line(vertices, primitive.first - scene::Float3{0.0f, radius, 0.0f}, primitive.first + scene::Float3{0.0f, radius, 0.0f}, primitive.color);
                add_line(vertices, primitive.first - scene::Float3{0.0f, 0.0f, radius}, primitive.first + scene::Float3{0.0f, 0.0f, radius}, primitive.color);
                if (primitive.kind == scene::dynamics::DebugPrimitiveKind::Point) continue;
            }
            if (primitive.kind == scene::dynamics::DebugPrimitiveKind::AxisAlignedBox) {
                const scene::Float3 minimum = primitive.first;
                const scene::Float3 maximum = primitive.second;
                const std::array points{scene::Float3{minimum.x, minimum.y, minimum.z}, scene::Float3{maximum.x, minimum.y, minimum.z}, scene::Float3{minimum.x, maximum.y, minimum.z}, scene::Float3{maximum.x, maximum.y, minimum.z}, scene::Float3{minimum.x, minimum.y, maximum.z}, scene::Float3{maximum.x, minimum.y, maximum.z}, scene::Float3{minimum.x, maximum.y, maximum.z}, scene::Float3{maximum.x, maximum.y, maximum.z}};
                constexpr std::array<std::array<std::uint32_t, 2>, 12> edges{{{0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3}, {2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7}}};
                for (const auto edge : edges) add_line(vertices, points[edge[0]], points[edge[1]], primitive.color);
                continue;
            }
            const scene::Float3 direction = (primitive.second - primitive.first).normalized();
            const float length            = (primitive.second - primitive.first).length();
            const scene::Float3 side      = (std::abs(direction.y) < 0.9f ? direction.cross({0.0f, 1.0f, 0.0f}) : direction.cross({1.0f, 0.0f, 0.0f})).normalized() * length * 0.16f;
            const scene::Float3 back      = primitive.second - direction * length * 0.24f;
            add_line(vertices, primitive.first, primitive.second, primitive.color);
            add_line(vertices, primitive.second, back + side, primitive.color);
            add_line(vertices, primitive.second, back - side, primitive.color);
        }
        const std::uint32_t tested_vertex_count = static_cast<std::uint32_t>(tested_vertices.size());
        tested_vertices.insert(tested_vertices.end(), xray_vertices.begin(), xray_vertices.end());
        if (!tested_vertices.empty()) {
            const std::uint64_t required_capacity = std::bit_ceil(static_cast<std::uint64_t>(tested_vertices.size()));
            if (required_capacity > this->debug_capacity) {
                GpuBuffer buffer = this->runtime->create_buffer(required_capacity * sizeof(DebugVertex), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                this->runtime->write_buffer_descriptor(this->debug_descriptor, vk::DescriptorType::eStorageBuffer, buffer);
                if (*this->debug_buffer.buffer) this->runtime->defer_destruction([old = std::move(this->debug_buffer)]() mutable {});
                this->debug_buffer   = std::move(buffer);
                this->debug_capacity = required_capacity;
            }
            const GpuUploadSlice upload = this->runtime->stage_upload(std::as_bytes(std::span{tested_vertices}));
            command_buffer.copyBuffer(upload.buffer, *this->debug_buffer.buffer, vk::BufferCopy{upload.offset, 0, upload.size});
            const vk::BufferMemoryBarrier2 debug_upload_dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eVertexShader, vk::AccessFlagBits2::eShaderStorageRead, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->debug_buffer.buffer, 0, upload.size};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 1, &debug_upload_dependency});
        }
        const bool outline_required = outline_visible && (!selected_instances.empty() || !active_instances.empty() || !hovered_instances.empty());
        if (!axes_visible && !outline_required && tested_vertices.empty() && volume_vector_fields.empty()) {
            const vk::ImageMemoryBarrier2 to_sample{
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderSampledRead,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                target_image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_sample});
            return;
        }
        if (!*this->mask.image || this->mask.extent != extent) this->create_images(extent);

        const std::array mask_barriers{
            vk::ImageMemoryBarrier2{
                this->mask_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eFragmentShader,
                this->mask_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eShaderSampledRead,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                this->mask_layout,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->mask.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            vk::ImageMemoryBarrier2{
                this->depth_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlags2{} : vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                this->depth_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                this->depth_layout,
                vk::ImageLayout::eDepthAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->depth.image,
                {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1},
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(mask_barriers.size()), mask_barriers.data()});
        const vk::RenderingAttachmentInfo color_attachment{
            *this->mask.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::ClearValue{vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 0.0f}}},
        };
        const vk::RenderingAttachmentInfo depth_attachment{
            *this->depth.view,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0}},
        };
        command_buffer.beginRendering(vk::RenderingInfo{
            {},
            vk::Rect2D{{0, 0}, extent},
            1,
            0,
            1,
            &color_attachment,
            &depth_attachment,
        });
        const std::array mask_stages{vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eFragment};
        const std::array mask_handles{*this->mask_shaders[0], *this->mask_shaders[1]};
        command_buffer.bindShadersEXT(mask_stages, mask_handles);
        this->runtime->bind_descriptor_heaps(command_buffer);
        const scene::CameraMatrices matrices = this->scene->camera.matrices();
        const auto draw_instance             = [&](const std::uint32_t instance_index, const std::array<float, 4> color) {
            const render::GpuDraw& draw = this->scene->gpu_scene.draws[instance_index];
            if (!raster_visualizations && draw.kind == render::GpuDrawKind::ParticleSet) return;
            const render::GpuGeometry* mesh         = draw.kind == render::GpuDrawKind::Geometry ? &this->scene->gpu_scene.geometries[draw.resource_index] : nullptr;
            const render::GpuParticleSet* particles = draw.kind == render::GpuDrawKind::ParticleSet ? &this->scene->gpu_scene.particle_sets[draw.resource_index] : nullptr;
            const MaskPushData push_data{
                mesh ? mesh->positions_descriptor : particles->positions_descriptor,
                mesh ? mesh->indices_descriptor : particles->positions_descriptor,
                particles ? particles->radii_descriptor : mesh->positions_descriptor,
                this->scene->primitives_descriptor,
                this->scene->transforms_descriptor,
                instance_index,
                mesh ? mesh->index_count / 3u : particles->particle_count,
                static_cast<std::uint32_t>(draw.kind),
                0,
                {},
                color,
                {matrices.view_projection[0], matrices.view_projection[1], matrices.view_projection[2], matrices.view_projection[3]},
                {matrices.view_projection[4], matrices.view_projection[5], matrices.view_projection[6], matrices.view_projection[7]},
                {matrices.view_projection[8], matrices.view_projection[9], matrices.view_projection[10], matrices.view_projection[11]},
                {matrices.view_projection[12], matrices.view_projection[13], matrices.view_projection[14], matrices.view_projection[15]},
            };
            this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.drawMeshTasksEXT((push_data.element_count + 31u) / 32u, 1, 1);
        };
        configure_mask_render_state(command_buffer, render_region, vk::CompareOp::eLess, true);
        for (std::uint32_t index = 0; index < this->scene->gpu_scene.draws.size(); ++index) draw_instance(index, {});
        if (outline_required) {
            configure_mask_render_state(command_buffer, render_region, vk::CompareOp::eEqual, false);
            for (const std::uint32_t index : selected_instances) draw_instance(index, {0.10f, 0.58f, 1.0f, 1.0f});
            for (const std::uint32_t index : active_instances) draw_instance(index, {1.0f, 0.55f, 0.08f, 1.0f});
            for (const std::uint32_t index : hovered_instances) draw_instance(index, {0.45f, 1.0f, 0.28f, 1.0f});
        }
        command_buffer.endRendering();
        this->mask_layout  = vk::ImageLayout::eColorAttachmentOptimal;
        this->depth_layout = vk::ImageLayout::eDepthAttachmentOptimal;

        const std::array outline_barriers{
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderSampledRead,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->mask.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(outline_barriers.size()), outline_barriers.data()});
        this->mask_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        const vk::RenderingAttachmentInfo target_attachment{
            target_view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eLoad,
            vk::AttachmentStoreOp::eStore,
        };
        const vk::RenderingAttachmentInfo overlay_depth_attachment{
            *this->depth.view,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eLoad,
            vk::AttachmentStoreOp::eDontCare,
        };
        command_buffer.beginRendering(vk::RenderingInfo{
            {},
            vk::Rect2D{{0, 0}, extent},
            1,
            0,
            1,
            &target_attachment,
            &overlay_depth_attachment,
        });
        command_buffer.setScissorWithCount(render_region);
        command_buffer.setCullMode(vk::CullModeFlagBits::eNone);
        command_buffer.setRasterizerDiscardEnable(vk::False);
        command_buffer.setPolygonModeEXT(vk::PolygonMode::eFill);
        command_buffer.setRasterizationSamplesEXT(vk::SampleCountFlagBits::e1);
        command_buffer.setAlphaToCoverageEnableEXT(vk::False);
        command_buffer.setDepthBiasEnable(vk::False);
        command_buffer.setStencilTestEnable(vk::False);
        command_buffer.setVertexInputEXT({}, {});
        command_buffer.setPrimitiveTopology(vk::PrimitiveTopology::eTriangleList);
        command_buffer.setPrimitiveRestartEnable(vk::False);
        constexpr vk::SampleMask sample_mask = 1;
        command_buffer.setSampleMaskEXT(vk::SampleCountFlagBits::e1, sample_mask);
        constexpr vk::Bool32 blend_enable = vk::True;
        command_buffer.setColorBlendEnableEXT(0, blend_enable);
        command_buffer.setColorBlendEquationEXT(0, vk::ColorBlendEquationEXT{
                                                       vk::BlendFactor::eSrcAlpha,
                                                       vk::BlendFactor::eOneMinusSrcAlpha,
                                                       vk::BlendOp::eAdd,
                                                       vk::BlendFactor::eOne,
                                                       vk::BlendFactor::eOneMinusSrcAlpha,
                                                       vk::BlendOp::eAdd,
                                                   });
        constexpr vk::ColorComponentFlags color_components = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        command_buffer.setColorWriteMaskEXT(0, color_components);
        if (!tested_vertices.empty()) {
            command_buffer.setViewportWithCount(vk::Viewport{static_cast<float>(render_region.offset.x), static_cast<float>(render_region.offset.y) + static_cast<float>(render_region.extent.height), static_cast<float>(render_region.extent.width), -static_cast<float>(render_region.extent.height), 0.0f, 1.0f});
            command_buffer.setPrimitiveTopology(vk::PrimitiveTopology::eLineList);
            command_buffer.setDepthWriteEnable(vk::False);
            command_buffer.setDepthCompareOp(vk::CompareOp::eLessOrEqual);
            const std::array debug_stages{vk::ShaderStageFlagBits::eTaskEXT, vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment};
            const std::array debug_handles{vk::ShaderEXT{}, vk::ShaderEXT{}, *this->debug_shaders[0], *this->debug_shaders[1]};
            command_buffer.bindShadersEXT(debug_stages, debug_handles);
            this->runtime->bind_descriptor_heaps(command_buffer);
            const DebugPushData debug_push{this->debug_descriptor, {}, {matrices.view_projection[0], matrices.view_projection[1], matrices.view_projection[2], matrices.view_projection[3]}, {matrices.view_projection[4], matrices.view_projection[5], matrices.view_projection[6], matrices.view_projection[7]}, {matrices.view_projection[8], matrices.view_projection[9], matrices.view_projection[10], matrices.view_projection[11]}, {matrices.view_projection[12], matrices.view_projection[13], matrices.view_projection[14], matrices.view_projection[15]}};
            this->runtime->push_data(command_buffer, std::as_bytes(std::span{&debug_push, 1}));
            command_buffer.setDepthTestEnable(vk::True);
            if (tested_vertex_count != 0) command_buffer.draw(tested_vertex_count, 1, 0, 0);
            const std::uint32_t xray_vertex_count = static_cast<std::uint32_t>(tested_vertices.size()) - tested_vertex_count;
            if (xray_vertex_count != 0) {
                command_buffer.setDepthTestEnable(vk::False);
                command_buffer.draw(xray_vertex_count, 1, tested_vertex_count, 0);
            }
        }
        if (!volume_vector_fields.empty()) {
            command_buffer.setViewportWithCount(vk::Viewport{static_cast<float>(render_region.offset.x), static_cast<float>(render_region.offset.y) + static_cast<float>(render_region.extent.height), static_cast<float>(render_region.extent.width), -static_cast<float>(render_region.extent.height), 0.0f, 1.0f});
            command_buffer.setPrimitiveTopology(vk::PrimitiveTopology::eLineList);
            command_buffer.setDepthTestEnable(vk::True);
            command_buffer.setDepthWriteEnable(vk::False);
            command_buffer.setDepthCompareOp(vk::CompareOp::eLessOrEqual);
            const std::array stages{vk::ShaderStageFlagBits::eTaskEXT, vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment};
            const std::array handles{vk::ShaderEXT{}, vk::ShaderEXT{}, *this->volume_vector_shaders[0], *this->volume_vector_shaders[1]};
            command_buffer.bindShadersEXT(stages, handles);
            this->runtime->bind_descriptor_heaps(command_buffer);
            for (const render::VolumeVectorField& field : volume_vector_fields) {
                const scene::Bounds3& bounds           = field.bounds;
                const std::array<float, 16>& transform = field.transform.matrix;
                const float scale                      = bounds.radius() * 0.08f;
                const VolumeVectorPushData push_data{field.velocity, {}, {field.resolution.x, field.resolution.y, field.resolution.z, 0}, {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, 0.0f}, {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z, 0.0f}, {transform[0], transform[1], transform[2], transform[3]}, {transform[4], transform[5], transform[6], transform[7]}, {transform[8], transform[9], transform[10], transform[11]}, {matrices.view_projection[0], matrices.view_projection[1], matrices.view_projection[2], matrices.view_projection[3]}, {matrices.view_projection[4], matrices.view_projection[5], matrices.view_projection[6], matrices.view_projection[7]}, {matrices.view_projection[8], matrices.view_projection[9], matrices.view_projection[10], matrices.view_projection[11]}, {matrices.view_projection[12], matrices.view_projection[13], matrices.view_projection[14], matrices.view_projection[15]}, {scale, 8.0f, 0.0f, 0.0f}};
                this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
                command_buffer.draw(6u * 8u * 8u * 8u, 1, 0, 0);
            }
        }
        if (axes_visible) {
            command_buffer.setViewportWithCount(vk::Viewport{
                static_cast<float>(render_region.offset.x),
                static_cast<float>(render_region.offset.y) + static_cast<float>(render_region.extent.height),
                static_cast<float>(render_region.extent.width),
                -static_cast<float>(render_region.extent.height),
                0.0f,
                1.0f,
            });
            command_buffer.setDepthTestEnable(vk::True);
            command_buffer.setDepthWriteEnable(vk::False);
            command_buffer.setDepthCompareOp(vk::CompareOp::eLess);
            const std::array axes_stages{
                vk::ShaderStageFlagBits::eTaskEXT,
                vk::ShaderStageFlagBits::eMeshEXT,
                vk::ShaderStageFlagBits::eVertex,
                vk::ShaderStageFlagBits::eFragment,
            };
            const std::array axes_handles{
                vk::ShaderEXT{},
                vk::ShaderEXT{},
                *this->axes_shaders[0],
                *this->axes_shaders[1],
            };
            command_buffer.bindShadersEXT(axes_stages, axes_handles);
            const AxesPushData axes_push{
                {matrices.view_projection[0], matrices.view_projection[1], matrices.view_projection[2], matrices.view_projection[3]},
                {matrices.view_projection[4], matrices.view_projection[5], matrices.view_projection[6], matrices.view_projection[7]},
                {matrices.view_projection[8], matrices.view_projection[9], matrices.view_projection[10], matrices.view_projection[11]},
                {matrices.view_projection[12], matrices.view_projection[13], matrices.view_projection[14], matrices.view_projection[15]},
                {matrices.inverse_view_projection[0], matrices.inverse_view_projection[1], matrices.inverse_view_projection[2], matrices.inverse_view_projection[3]},
                {matrices.inverse_view_projection[4], matrices.inverse_view_projection[5], matrices.inverse_view_projection[6], matrices.inverse_view_projection[7]},
                {matrices.inverse_view_projection[8], matrices.inverse_view_projection[9], matrices.inverse_view_projection[10], matrices.inverse_view_projection[11]},
                {matrices.inverse_view_projection[12], matrices.inverse_view_projection[13], matrices.inverse_view_projection[14], matrices.inverse_view_projection[15]},
                {axes_plane, render_region.extent.width, render_region.extent.height, 0},
            };
            this->runtime->push_data(command_buffer, std::as_bytes(std::span{&axes_push, 1}));
            command_buffer.draw(3, 1, 0, 0);
        }
        if (outline_required) {
            command_buffer.setViewportWithCount(vk::Viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f});
            command_buffer.setDepthTestEnable(vk::False);
            command_buffer.setPrimitiveTopology(vk::PrimitiveTopology::eTriangleList);
            command_buffer.setPrimitiveRestartEnable(vk::False);
            const std::array outline_stages{
                vk::ShaderStageFlagBits::eTaskEXT,
                vk::ShaderStageFlagBits::eMeshEXT,
                vk::ShaderStageFlagBits::eVertex,
                vk::ShaderStageFlagBits::eFragment,
            };
            const std::array outline_handles{
                vk::ShaderEXT{},
                vk::ShaderEXT{},
                *this->outline_shaders[0],
                *this->outline_shaders[1],
            };
            command_buffer.bindShadersEXT(outline_stages, outline_handles);
            this->runtime->bind_descriptor_heaps(command_buffer);
            struct alignas(16) OutlinePushData {
                DescriptorHandle mask;
                DescriptorHandle sampler;
                std::array<float, 2> inverse_extent;
                std::array<std::uint32_t, 2> reserved;
            };
            const OutlinePushData outline_push{
                this->mask_descriptor,
                this->sampler_descriptor,
                {1.0f / static_cast<float>(extent.width), 1.0f / static_cast<float>(extent.height)},
                {},
            };
            this->runtime->push_data(command_buffer, std::as_bytes(std::span{&outline_push, 1}));
            command_buffer.draw(3, 1, 0, 0);
        }
        command_buffer.endRendering();
        const vk::ImageMemoryBarrier2 target_to_sample{
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::AccessFlagBits2::eShaderSampledRead,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            target_image,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &target_to_sample});
    }
    namespace {
        [[nodiscard]] FrozenExportResult write_frozen_scene(scene::Scene scene, const std::filesystem::path& requested, const std::filesystem::path& source_path) {
            FrozenExportResult result{};
            const std::filesystem::path parent      = std::filesystem::absolute(requested.parent_path());
            const std::string name                  = requested.stem().string();
            const std::filesystem::path destination = parent / name;
            const std::filesystem::path temporary   = parent / std::format(".{}.spectra-export-{}", name, std::chrono::steady_clock::now().time_since_epoch().count());
            try {
                if (std::filesystem::exists(destination)) throw std::runtime_error(std::format("Frozen Scene destination '{}' already exists", destination.string()));
                if (temporary.parent_path() != parent) throw std::runtime_error("Frozen Scene temporary directory escaped its destination");
                std::filesystem::create_directory(temporary);
                const std::filesystem::path scene_path = temporary / std::format("{}.spectra", name);
                scene::save_scene(std::move(scene), scene_path, source_path);
                std::filesystem::rename(temporary, destination);
                result.path = destination / scene_path.filename();
            } catch (const std::exception& error) {
                result.error = error.what();
                std::error_code cleanup_error{};
                if (temporary.parent_path() == parent) std::filesystem::remove_all(temporary, cleanup_error);
            }
            return result;
        }
    } // namespace

    ViewportCamera::ViewportCamera(const scene::CameraResource& camera, const scene::Bounds3 bounds) noexcept : camera(camera), focus(bounds.center()) {}

    void ViewportCamera::orbit(const float x_pixels, const float y_pixels) noexcept {
        constexpr float radians_per_pixel = 0.006f;
        scene::Float3 offset              = this->camera.frame().position - this->focus;
        const float distance              = offset.length();
        const scene::Float3 up            = this->navigation_up.normalized();
        scene::Float3 horizontal          = (offset - up * offset.dot(up)).normalized();
        const float yaw                   = -x_pixels * radians_per_pixel;
        horizontal                        = horizontal * std::cos(yaw) + up.cross(horizontal) * std::sin(yaw);
        const float pitch                 = std::clamp(std::asin(std::clamp(offset.normalized().dot(up), -1.0f, 1.0f)) + y_pixels * radians_per_pixel, -std::numbers::pi_v<float> * 0.49f, std::numbers::pi_v<float> * 0.49f);
        const scene::Float3 direction     = horizontal.normalized() * std::cos(pitch) + up * std::sin(pitch);
        this->camera.transform            = scene::Transform::look_at(this->focus + direction * distance, this->focus, this->navigation_up);
        ++this->revision;
    }

    void ViewportCamera::pan(const float x_pixels, const float y_pixels, const float viewport_height) noexcept {
        const scene::CameraFrame frame = this->camera.frame();
        const float distance           = (frame.position - this->focus).length();
        const float world_per_pixel    = std::visit(
            [distance, viewport_height](const auto& data) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>)
                    return 2.0f * distance * std::tan(data.vertical_fov * std::numbers::pi_v<float> / 360.0f) / viewport_height;
                else
                    return (data.screen.maximum.y - data.screen.minimum.y) / viewport_height;
            },
            this->camera.data);
        const scene::Float3 movement = frame.right * (-x_pixels * world_per_pixel) + frame.up * (y_pixels * world_per_pixel);
        this->focus                  = this->focus + movement;
        this->camera.transform       = scene::Transform::look_at(frame.position + movement, this->focus, this->navigation_up);
        ++this->revision;
    }

    void ViewportCamera::zoom(const float steps) noexcept {
        if (scene::OrthographicCameraData* orthographic = std::get_if<scene::OrthographicCameraData>(&this->camera.data)) {
            const float scale = std::pow(0.88f, steps);
            const scene::Float2 center{
                (orthographic->screen.minimum.x + orthographic->screen.maximum.x) * 0.5f,
                (orthographic->screen.minimum.y + orthographic->screen.maximum.y) * 0.5f,
            };
            orthographic->screen.minimum = {
                center.x + (orthographic->screen.minimum.x - center.x) * scale,
                center.y + (orthographic->screen.minimum.y - center.y) * scale,
            };
            orthographic->screen.maximum = {
                center.x + (orthographic->screen.maximum.x - center.x) * scale,
                center.y + (orthographic->screen.maximum.y - center.y) * scale,
            };
            ++this->revision;
            return;
        }
        const scene::CameraFrame frame = this->camera.frame();
        const scene::Float3 offset     = frame.position - this->focus;
        const float distance           = std::clamp(offset.length() * std::pow(0.88f, steps), 0.01f, 1000000.0f);
        this->camera.transform         = scene::Transform::look_at(this->focus + offset.normalized() * distance, this->focus, this->navigation_up);
        ++this->revision;
    }

    void ViewportCamera::frame(const scene::Bounds3 bounds, const float aspect) noexcept {
        const scene::CameraFrame camera_frame = this->camera.frame();
        const scene::Float3 target            = bounds.center();
        const scene::Float3 direction         = (camera_frame.position - this->focus).normalized();
        if (scene::OrthographicCameraData* orthographic = std::get_if<scene::OrthographicCameraData>(&this->camera.data)) {
            const float half_height = bounds.radius() * 1.1f * std::max(1.0f, 1.0f / aspect);
            const float half_width  = half_height * aspect;
            const float distance    = std::max((camera_frame.position - this->focus).length(), bounds.radius() * 3.0f);
            orthographic->screen    = {{-half_width, -half_height}, {half_width, half_height}};
            orthographic->far_plane = std::max(orthographic->far_plane, distance + bounds.radius() * 4.0f);
            this->focus             = target;
            this->camera.transform  = scene::Transform::look_at(target + direction * distance, target, this->navigation_up);
            ++this->revision;
            return;
        }
        scene::PerspectiveCameraData& perspective = std::get<scene::PerspectiveCameraData>(this->camera.data);
        const float horizontal_fov                = 2.0f * std::atan(std::tan(perspective.vertical_fov * std::numbers::pi_v<float> / 360.0f) * aspect);
        const float limiting_fov                  = std::min(perspective.vertical_fov * std::numbers::pi_v<float> / 180.0f, horizontal_fov);
        const float distance                      = bounds.radius() / std::sin(limiting_fov * 0.5f) * 1.1f;
        this->focus                               = target;
        this->camera.transform                    = scene::Transform::look_at(target + direction * distance, target, this->navigation_up);
        perspective.far_plane                     = std::max(perspective.far_plane, distance + bounds.radius() * 4.0f);
        ++this->revision;
    }

    void ViewportCamera::align(const scene::Float3 direction, const scene::Bounds3 bounds, const float aspect) noexcept {
        this->focus            = bounds.center();
        this->navigation_up    = std::abs(direction.y) > 0.9f ? scene::Float3{0.0f, 0.0f, -1.0f} : scene::Float3{0.0f, 1.0f, 0.0f};
        this->camera.transform = scene::Transform::look_at(this->focus + direction.normalized() * bounds.radius() * 3.0f, this->focus, this->navigation_up);
        this->frame(bounds, aspect);
    }

    Workspace::Workspace(Spectra& runtime, const std::filesystem::path& shader_directory, const std::filesystem::path& scene_path, const std::uint32_t frames_in_flight) : runtime(&runtime), shader_directory(shader_directory), frame_count(frames_in_flight), frozen_export_slots(frames_in_flight) {
        this->open_scene(scene_path);
    }

    Workspace::~Workspace() {
        this->wait_for_export();
    }

    void Workspace::destroy_renderers() noexcept {
        this->picker.reset();
        this->overlay_renderer.reset();
        this->pathtracer_renderer.reset();
        this->rasterizer_renderer.reset();
        this->raster_scene.reset();
        this->gpu_scene.reset();
    }

    void Workspace::rebuild_renderers(scene::Scene& source, scene::dynamics::Runtime* dynamics) {
        std::vector<render::GpuGeometryBinding> geometry_bindings{};
        if (dynamics) {
            geometry_bindings.reserve(dynamics->mesh_bindings.size());
            for (const scene::dynamics::MeshOutputBinding& binding : dynamics->mesh_bindings) geometry_bindings.emplace_back(binding.geometry, binding.mode == scene::dynamics::MeshUpdateMode::Deformable ? render::GpuMeshUpdateMode::Deformable : render::GpuMeshUpdateMode::TopologyChanging, binding.vertex_capacity, binding.index_capacity);
        }
        std::unique_ptr<render::GpuScene> gpu_scene                 = std::make_unique<render::GpuScene>(*this->runtime, source, this->shader_directory, geometry_bindings, dynamics ? std::span{dynamics->particle_capacities} : std::span<const std::pair<scene::ParticleSetId, std::uint32_t>>{}, dynamics ? std::span{dynamics->hidden_instances} : std::span<const scene::InstanceId>{});
                        std::unique_ptr<rasterizer::RasterScene> raster_scene = std::make_unique<rasterizer::RasterScene>(*this->runtime, *gpu_scene, gpu_scene->state.view(), this->shader_directory);
        std::unique_ptr<rasterizer::Rasterizer> rasterizer_renderer = std::make_unique<rasterizer::Rasterizer>(*this->runtime, *raster_scene, this->shader_directory);
        std::unique_ptr<pathtracer::PathTracer> pathtracer_renderer = std::make_unique<pathtracer::PathTracer>(*this->runtime, *gpu_scene, gpu_scene->state.view(), this->shader_directory, this->frame_count);
        std::unique_ptr<Picker> picker                              = std::make_unique<Picker>(*this->runtime, *raster_scene, this->shader_directory, this->frame_count);
        std::unique_ptr<OverlayRenderer> overlay_renderer           = std::make_unique<OverlayRenderer>(*this->runtime, *raster_scene, this->shader_directory);
        this->runtime->wait_idle();
        this->destroy_renderers();
        this->gpu_scene                  = std::move(gpu_scene);
        this->raster_scene               = std::move(raster_scene);
        this->rasterizer_renderer        = std::move(rasterizer_renderer);
        this->pathtracer_renderer        = std::move(pathtracer_renderer);
        this->picker                     = std::move(picker);
        this->overlay_renderer           = std::move(overlay_renderer);
        this->synchronized_revision      = source.revision().value;
        this->pending_pathtracer_changes = scene::SceneChange::None;
        this->selection.debug_object.reset();
        this->selection.hovered_debug_object.reset();
        source.acknowledge_changes();
    }

    void Workspace::open_scene(const std::filesystem::path& path) {
        scene::Scene next_scene = scene::load_scene(path);
        std::unique_ptr<scene::dynamics::Runtime> next_dynamics{};
        if (next_scene.dynamic_setup) next_dynamics = std::make_unique<scene::dynamics::Runtime>(*this->runtime, path, next_scene);
        this->rebuild_renderers(next_scene, next_dynamics.get());
        this->scene    = std::move(next_scene);
        this->dynamics = std::move(next_dynamics);
        if (this->dynamics) this->dynamics->bind_scene(this->scene);
        this->source_path       = path;
        this->viewport_camera   = ViewportCamera{this->scene.camera(), this->scene.view().bounds()};
        const scene::Film& film = this->scene.film();
        this->viewport_aspect   = static_cast<float>(film.resolution[0]) / static_cast<float>(film.resolution[1]);
        this->axes_plane        = AxesPlane::Xz;
        this->camera_source     = CameraSource::Viewport;
        this->camera_changed();
        this->selection = {};
        this->undo_history.clear();
        this->redo_history.clear();
        this->transform_edit.reset();
        this->current_edit_serial = 0;
        this->saved_edit_serial   = 0;
        this->next_edit_serial    = 1;
        this->pathtracer_paused   = false;
    }

    void Workspace::save() {
        scene::save_scene(this->scene, this->source_path, this->source_path);
        this->saved_edit_serial = this->current_edit_serial;
    }

    void Workspace::save_as(const std::filesystem::path& path) {
        scene::save_scene(this->scene, path, this->source_path);
        if (this->scene.dynamic_setup && std::filesystem::absolute(path.parent_path()).lexically_normal() != std::filesystem::absolute(this->source_path.parent_path()).lexically_normal()) {
            std::vector<std::string> providers{};
            for (const scene::DynamicSystem& system : this->scene.dynamic_setup->systems)
                if (!std::ranges::contains(providers, system.provider)) providers.emplace_back(system.provider);
            for (const std::string& provider : providers) {
                const std::filesystem::path filename = provider + ".spectra-plugin.dll";
                std::filesystem::copy_file(this->source_path.parent_path() / filename, path.parent_path() / filename);
            }
        }
        this->source_path       = path;
        this->saved_edit_serial = this->current_edit_serial;
    }

    void Workspace::export_frozen(const std::filesystem::path& path) {
        if (this->export_in_progress()) throw std::runtime_error("A Frozen Scene export is already in progress");
        this->frozen_export_result.reset();
        this->frozen_export_request = path;
    }

    bool Workspace::export_in_progress() const noexcept {
        if (this->frozen_export_request) return true;
        if (std::ranges::any_of(this->frozen_export_slots, [](const FrozenExportSlot& slot) { return slot.snapshot.has_value(); })) return true;
        return this->frozen_export_writer.valid();
    }

    std::optional<FrozenExportResult> Workspace::take_export_result() {
        if (this->frozen_export_writer.valid() && this->frozen_export_writer.wait_for(std::chrono::seconds{0}) == std::future_status::ready) this->frozen_export_result = this->frozen_export_writer.get();
        return std::exchange(this->frozen_export_result, std::nullopt);
    }

    void Workspace::wait_for_export() {
        for (FrozenExportSlot& slot : this->frozen_export_slots)
            if (slot.snapshot) {
                slot.snapshot->materialize();
                if (this->frozen_export_writer.valid()) this->frozen_export_writer.get();
                this->frozen_export_writer = std::async(std::launch::async, write_frozen_scene, std::move(slot.snapshot->scene), slot.path, slot.source_path);
                slot.snapshot.reset();
            }
        if (this->frozen_export_writer.valid()) this->frozen_export_result = this->frozen_export_writer.get();
    }

    void Workspace::begin_frame(const std::uint32_t frame_index) {
        FrozenExportSlot& export_slot = this->frozen_export_slots[frame_index];
        if (export_slot.snapshot) {
            export_slot.snapshot->materialize();
            this->frozen_export_writer = std::async(std::launch::async, write_frozen_scene, std::move(export_slot.snapshot->scene), export_slot.path, export_slot.source_path);
            export_slot.snapshot.reset();
            export_slot.path.clear();
            export_slot.source_path.clear();
        }
        if (this->frozen_export_writer.valid() && this->frozen_export_writer.wait_for(std::chrono::seconds{0}) == std::future_status::ready) this->frozen_export_result = this->frozen_export_writer.get();
        const PickResult result = this->picker->consume(frame_index);
        if (!result.available) return;
        std::optional<scene::InstanceId> instance{};
        if (result.instance_index) instance = this->gpu_scene->acceleration_source_instances[*result.instance_index];
        const bool debug_hit = result.debug_object && (result.debug_xray || !instance);
        if (!result.select) {
            this->selection.hovered              = debug_hit ? std::nullopt : instance;
            this->selection.hovered_debug_object = debug_hit ? result.debug_object : std::nullopt;
            return;
        }
        if (debug_hit) {
            if (!result.additive) this->clear_selection();
            this->selection.debug_object = result.debug_object;
            return;
        }
        if (!instance) {
            if (!result.additive) this->clear_selection();
            return;
        }
        this->selection.debug_object.reset();
        this->select_instance(*instance, result.additive);
    }

    void Workspace::update(const std::chrono::duration<double> elapsed) {
        if (this->dynamics) this->dynamics->update(elapsed);
    }

    void Workspace::prepare(const vk::raii::CommandBuffer& command_buffer, const vk::Extent2D extent) {
        scene::SceneChange binding_changes{scene::SceneChange::None};
        bool gpu_scene_synchronized{};
        if (this->dynamics) {
            if (const scene::dynamics::PublishedFrame* frame = this->dynamics->prepare_frame()) {
                binding_changes        = this->gpu_scene->apply(*frame, command_buffer);
                gpu_scene_synchronized = true;
            }
        }
        const scene::SceneRevision revision = this->gpu_scene->state.revision();
        const bool scene_changed            = revision.value != this->synchronized_revision;
        if (scene_changed) {
            if (!gpu_scene_synchronized) binding_changes = this->gpu_scene->synchronize(command_buffer);
            scene::SceneView synchronized_scene             = this->gpu_scene->state.view();
            synchronized_scene.revision.changes             = synchronized_scene.revision.changes | binding_changes;
            constexpr scene::SceneChange pathtracer_changes = scene::SceneChange::Geometry | scene::SceneChange::Transform | scene::SceneChange::Texture | scene::SceneChange::Material | scene::SceneChange::Light | scene::SceneChange::Medium | scene::SceneChange::Volume | scene::SceneChange::Camera | scene::SceneChange::Film | scene::SceneChange::Sampler | scene::SceneChange::Transport;
            this->pending_pathtracer_changes                = this->pending_pathtracer_changes | (synchronized_scene.revision.changes & pathtracer_changes);
            this->raster_scene->synchronize(synchronized_scene, command_buffer);
            this->synchronized_revision = revision.value;
            this->prune_selection();
        }
        if (this->mode == RenderMode::PathTracer && this->pending_pathtracer_changes != scene::SceneChange::None) {
            scene::SceneView path_scene = this->gpu_scene->state.view();
            path_scene.revision.changes = this->pending_pathtracer_changes;
            this->pathtracer_renderer->synchronize(path_scene, command_buffer);
            this->pathtracer_renderer->reset_accumulation();
            this->pending_pathtracer_changes = scene::SceneChange::None;
        }
        const float aspect              = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const bool aspect_changed       = aspect != this->viewport_aspect;
        const bool scene_camera_changed = scene_changed && (revision.changes & scene::SceneChange::Camera) != scene::SceneChange::None;
        this->viewport_aspect           = aspect;
        const bool camera_changed       = aspect_changed || this->camera_source != this->synchronized_camera_source || (this->camera_source == CameraSource::Viewport && (scene_camera_changed || this->viewport_camera.revision != this->synchronized_viewport_camera_revision)) || (this->camera_source == CameraSource::Scene && this->scene.camera().revision != this->synchronized_scene_camera_revision);
        if (camera_changed) this->camera_changed();
        if (this->frozen_export_request) {
            FrozenExportSlot& slot = this->frozen_export_slots[this->runtime->frame_index];
            slot.path              = *this->frozen_export_request;
            slot.source_path       = this->source_path;
            slot.snapshot          = this->gpu_scene->record_frozen_scene(command_buffer, this->display_camera, extent, this->exposure);
            this->frozen_export_request.reset();
        }
        if (scene_changed) this->gpu_scene->state.acknowledge_changes();
        if (this->scene.revision().changes != scene::SceneChange::None) this->scene.acknowledge_changes();
        if (this->mode == RenderMode::Rasterizer)
            this->rasterizer_renderer->prepare(extent);
        else
            this->pathtracer_renderer->prepare(extent);
        if (this->dynamics) this->dynamics->consume_frame();
    }

    void Workspace::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_index) {
        if (this->mode == RenderMode::Rasterizer)
            this->rasterizer_renderer->record(command_buffer);
        else if (!this->pathtracer_paused && this->pathtracer_renderer->accumulated_samples() < this->scene.sampler().samples_per_pixel)
            this->pathtracer_renderer->record(command_buffer, frame_index);
        this->picker->record(command_buffer, frame_index);
    }

    void Workspace::record_overlays(const vk::raii::CommandBuffer& command_buffer, const vk::Image target_image, const vk::ImageView target_view, const vk::Extent2D extent, const bool show_axes) {
        std::vector<std::uint32_t> selected_indices{};
        std::vector<std::uint32_t> active_indices{};
        std::vector<std::uint32_t> hovered_indices{};
        const auto collect = [this](const scene::InstanceId instance, std::vector<std::uint32_t>& destination) {
            for (std::uint32_t gpu_instance = 0; gpu_instance < this->gpu_scene->source_instances.size(); ++gpu_instance) {
                if (this->mode == RenderMode::PathTracer && this->gpu_scene->draws[gpu_instance].kind == render::GpuDrawKind::ParticleSet) continue;
                if (this->gpu_scene->source_instances[gpu_instance] == instance) destination.push_back(gpu_instance);
            }
        };
        for (const scene::InstanceId instance : this->selection.selected_instances) collect(instance, selected_indices);
        if (this->selection.active) collect(*this->selection.active, active_indices);
        if (this->selection.hovered) collect(*this->selection.hovered, hovered_indices);
        const vk::Rect2D render_region{{0, 0}, extent};
        this->overlay_renderer->record(command_buffer, target_image, target_view, extent, render_region, selected_indices, active_indices, hovered_indices, std::to_underlying(this->camera_source == CameraSource::Scene ? AxesPlane::Xz : this->axes_plane), show_axes, this->overlays_visible, this->mode == RenderMode::Rasterizer, this->mode == RenderMode::Rasterizer && this->dynamics ? std::span<const scene::dynamics::DebugPrimitive>{this->dynamics->debug_primitives} : std::span<const scene::dynamics::DebugPrimitive>{}, this->mode == RenderMode::Rasterizer ? std::span<const render::VolumeVectorField>{this->gpu_scene->volume_vector_fields} : std::span<const render::VolumeVectorField>{});
    }

    render::RenderOutput Workspace::output() const noexcept {
        if (this->mode == RenderMode::Rasterizer) return this->rasterizer_renderer->output();
        return this->pathtracer_renderer->output();
    }

    bool Workspace::dirty() const noexcept {
        return this->current_edit_serial != this->saved_edit_serial || (this->transform_edit && this->transform_edit->before != this->transform_edit->after);
    }

    bool Workspace::has_dynamic_setup() const noexcept {
        return static_cast<bool>(this->dynamics);
    }

    void Workspace::reset_accumulation() noexcept {
        this->pathtracer_renderer->reset_accumulation();
    }

    std::uint32_t Workspace::accumulated_path_samples() const noexcept {
        return this->pathtracer_renderer->accumulated_samples();
    }

    render::RenderReadback Workspace::readback() {
        return this->pathtracer_renderer->readback();
    }

    bool Workspace::playback_running() const noexcept {
        return this->dynamics->running();
    }

    scene::dynamics::TimelineState Workspace::timeline() const noexcept {
        return this->dynamics->timeline();
    }

    void Workspace::start_playback() {
        this->dynamics->set_running(true);
    }

    void Workspace::stop_playback() {
        this->dynamics->set_running(false);
    }

    void Workspace::advance_playback() {
        this->dynamics->advance();
    }

    void Workspace::reset_playback() {
        this->dynamics->set_running(false);
        this->dynamics->reset();
    }

    void Workspace::set_simulation_step(const std::uint64_t step) {
        this->dynamics->set_running(false);
        this->dynamics->seek(step);
    }

    void Workspace::set_dynamic_parameters(const std::size_t system, std::vector<scene::DynamicParameterSetting> parameters, const bool reset) {
        scene::DynamicSetup setup                       = *this->scene.dynamic_setup;
        scene::DynamicSystem& destination_system        = setup.systems[system];
        const std::optional<scene::DynamicSetup> before = this->scene.dynamic_setup;
        destination_system.parameters                   = std::move(parameters);
        scene::SceneUpdate{this->scene}.update_dynamic_setup(setup);
        this->push_edit(DynamicSetupEdit{.before = before, .after = setup});
        if (!destination_system.enabled) return;
        scene::dynamics::SystemState& state = this->dynamics->systems[system];
        for (scene::dynamics::Parameter& parameter : state.parameters) {
            const auto setting = std::ranges::find(destination_system.parameters, parameter.id, &scene::DynamicParameterSetting::id);
            if (setting != destination_system.parameters.end()) parameter.value = setting->value;
        }
        this->dynamics->apply_parameters(system, reset);
    }

    std::vector<scene::dynamics::SystemState>& Workspace::dynamic_systems() noexcept {
        return this->dynamics->systems;
    }

    std::span<const scene::dynamics::ProviderDescriptor> Workspace::dynamic_providers() const noexcept {
        return this->dynamics ? std::span<const scene::dynamics::ProviderDescriptor>{this->dynamics->providers} : std::span<const scene::dynamics::ProviderDescriptor>{};
    }

    const std::optional<scene::DynamicSetup>& Workspace::dynamic_setup() const noexcept {
        return this->scene.dynamic_setup;
    }

    void Workspace::set_dynamic_clock(scene::DynamicClock clock) {
        scene::DynamicSetup setup = *this->scene.dynamic_setup;
        setup.clock               = std::move(clock);
        this->commit_dynamic_setup(std::move(setup));
    }

    void Workspace::set_dynamic_seed(const std::uint64_t seed) {
        scene::DynamicSetup setup = *this->scene.dynamic_setup;
        setup.seed                = seed;
        this->commit_dynamic_setup(std::move(setup));
    }

    void Workspace::commit_dynamic_setup(std::optional<scene::DynamicSetup> setup, const bool preserve_simulation) {
        if (setup == this->scene.dynamic_setup) return;
        const std::optional<scene::dynamics::TimelineState> timeline = preserve_simulation && this->dynamics ? std::optional{this->dynamics->timeline()} : std::nullopt;
        const bool running                                           = preserve_simulation && this->dynamics && this->dynamics->running();
        DynamicSetupEdit edit{.before = this->scene.dynamic_setup, .after = setup, .preserve_simulation = preserve_simulation};
        scene::Scene next_scene = this->scene;
        scene::SceneUpdate{next_scene}.update_dynamic_setup(std::move(setup));
        std::unique_ptr<scene::dynamics::Runtime> next_dynamics{};
        if (next_scene.dynamic_setup) next_dynamics = std::make_unique<scene::dynamics::Runtime>(*this->runtime, this->source_path, next_scene);
        if (next_dynamics && timeline) {
            next_dynamics->seek(timeline->step);
            next_dynamics->set_running(running);
        }
        this->rebuild_renderers(next_scene, next_dynamics.get());
        this->scene    = std::move(next_scene);
        this->dynamics = std::move(next_dynamics);
        if (this->dynamics) this->dynamics->bind_scene(this->scene);
        this->camera_changed();
        this->push_edit(std::move(edit));
    }

    void Workspace::set_dynamic_system_enabled(const std::size_t system, const bool enabled) {
        scene::DynamicSetup setup     = *this->scene.dynamic_setup;
        setup.systems[system].enabled = enabled;
        this->commit_dynamic_setup(std::move(setup));
    }

    void Workspace::set_dynamic_system_visible(const std::size_t system, const bool visible) {
        scene::DynamicSetup setup     = *this->scene.dynamic_setup;
        setup.systems[system].visible = visible;
        this->commit_dynamic_setup(std::move(setup), true);
    }

    void Workspace::add_dynamic_system(const scene::dynamics::ProviderDescriptor& provider, std::vector<scene::DynamicPortBinding> bindings) {
        scene::DynamicSetup setup = this->scene.dynamic_setup.value_or(scene::DynamicSetup{});
        std::string system_id     = provider.id;
        for (std::uint32_t suffix = 2; std::ranges::any_of(setup.systems, [&system_id](const scene::DynamicSystem& system) { return system.id.value == system_id; }); ++suffix) system_id = std::format("{}_{}", provider.id, suffix);
        scene::DynamicSystem system{
            .id       = {system_id},
            .name     = provider.name,
            .provider = provider.id,
            .enabled  = false,
            .visible  = true,
            .bindings = std::move(bindings),
        };
        for (const scene::dynamics::Parameter& parameter : provider.parameters) system.parameters.emplace_back(parameter.id, parameter.value);
        setup.systems.emplace_back(std::move(system));
        this->commit_dynamic_setup(std::move(setup));
    }

    void Workspace::set_dynamic_system_provider(const std::size_t system, const scene::dynamics::ProviderDescriptor& provider, std::vector<scene::DynamicPortBinding> bindings) {
        scene::DynamicSetup setup                          = *this->scene.dynamic_setup;
        scene::DynamicSystem& destination                  = setup.systems[system];
        const scene::dynamics::ProviderDescriptor& current = this->dynamics->provider(destination.provider);
        if (provider.interface_id != current.interface_id || provider.interface_version != current.interface_version) throw std::runtime_error("A comparison System can only replace its Provider with the same interface contract");
        destination.provider = provider.id;
        destination.name     = provider.name;
        destination.bindings = std::move(bindings);
        destination.parameters.clear();
        for (const scene::dynamics::Parameter& parameter : provider.parameters) destination.parameters.emplace_back(parameter.id, parameter.value);
        this->commit_dynamic_setup(std::move(setup));
    }

    void Workspace::remove_dynamic_system(const std::size_t system) {
        scene::DynamicSetup setup = *this->scene.dynamic_setup;
        setup.systems.erase(setup.systems.begin() + system);
        this->commit_dynamic_setup(setup.systems.empty() ? std::optional<scene::DynamicSetup>{} : std::optional{std::move(setup)});
    }

    void Workspace::set_dynamic_port_binding(const std::size_t system, std::string port, scene::DynamicPortBinding replacement) {
        scene::DynamicSetup setup                        = *this->scene.dynamic_setup;
        std::vector<scene::DynamicPortBinding>& bindings = setup.systems[system].bindings;
        std::erase_if(bindings, [&port](const scene::DynamicPortBinding& binding) { return binding.port == port; });
        replacement.port = std::move(port);
        bindings.emplace_back(std::move(replacement));
        this->commit_dynamic_setup(std::move(setup));
    }

    const scene::CameraResource& Workspace::active_camera() const noexcept {
        return this->display_camera;
    }

    void Workspace::camera_changed() noexcept {
        this->display_camera = this->camera_source == CameraSource::Scene ? this->scene.camera() : this->viewport_camera.camera;
        std::visit(
            [this](auto& data) {
                const float center_x    = (data.screen.minimum.x + data.screen.maximum.x) * 0.5f;
                const float half_height = (data.screen.maximum.y - data.screen.minimum.y) * 0.5f;
                const float half_width  = half_height * this->viewport_aspect;
                data.screen.minimum.x   = center_x - half_width;
                data.screen.maximum.x   = center_x + half_width;
            },
            this->display_camera.data);
        this->raster_scene->camera = this->display_camera;
        this->pathtracer_renderer->change_camera(this->display_camera);
        this->synchronized_camera_source            = this->camera_source;
        this->synchronized_viewport_camera_revision = this->viewport_camera.revision;
        this->synchronized_scene_camera_revision    = this->scene.camera().revision;
    }

    void Workspace::orbit_viewport_camera(const float x_pixels, const float y_pixels) noexcept {
        this->viewport_camera.orbit(x_pixels, y_pixels);
        this->axes_plane    = AxesPlane::Xz;
        this->camera_source = CameraSource::Viewport;
        this->camera_changed();
    }

    void Workspace::pan_viewport_camera(const float x_pixels, const float y_pixels, const float viewport_height) noexcept {
        this->viewport_camera.pan(x_pixels, y_pixels, viewport_height);
        this->camera_source = CameraSource::Viewport;
        this->camera_changed();
    }

    void Workspace::zoom_viewport_camera(const float steps) noexcept {
        this->viewport_camera.zoom(steps);
        this->camera_source = CameraSource::Viewport;
        this->camera_changed();
    }

    void Workspace::frame_scene(const float aspect) noexcept {
        this->viewport_camera.frame(this->gpu_scene->state.view().bounds(), aspect);
        this->camera_source = CameraSource::Viewport;
        this->camera_changed();
    }

    void Workspace::frame_selection(const float aspect) noexcept {
        if (const std::optional<scene::Bounds3> bounds = this->gpu_scene->state.view().bounds(this->selection.selected_instances))
            this->viewport_camera.frame(*bounds, aspect);
        else
            this->viewport_camera.frame(this->gpu_scene->state.view().bounds(), aspect);
        this->camera_source = CameraSource::Viewport;
        this->camera_changed();
    }

    void Workspace::view_axis(const scene::Float3 direction, const float aspect) noexcept {
        const std::optional<scene::Bounds3> selected = this->gpu_scene->state.view().bounds(this->selection.selected_instances);
        this->viewport_camera.align(direction, selected.value_or(this->gpu_scene->state.view().bounds()), aspect);
        if (std::abs(direction.x) > 0.5f)
            this->axes_plane = AxesPlane::Yz;
        else if (std::abs(direction.z) > 0.5f)
            this->axes_plane = AxesPlane::Xy;
        else
            this->axes_plane = AxesPlane::Xz;
        this->camera_source = CameraSource::Viewport;
        this->camera_changed();
    }

    void Workspace::select_instance(const scene::InstanceId instance, const bool additive) {
        const std::vector<scene::InstanceId>::iterator found = std::ranges::find(this->selection.selected_instances, instance);
        if (!additive) {
            this->selection.selected_instances.assign(1, instance);
            this->selection.active = instance;
            return;
        }
        if (found == this->selection.selected_instances.end()) {
            this->selection.selected_instances.push_back(instance);
            this->selection.active = instance;
            return;
        }
        this->selection.selected_instances.erase(found);
        if (this->selection.selected_instances.empty())
            this->selection.active.reset();
        else
            this->selection.active = this->selection.selected_instances.back();
    }

    void Workspace::clear_selection() noexcept {
        this->selection.selected_instances.clear();
        this->selection.active.reset();
        this->selection.hovered.reset();
        this->selection.debug_object.reset();
        this->selection.hovered_debug_object.reset();
    }

    void Workspace::clear_hover() noexcept {
        this->selection.hovered.reset();
        this->selection.hovered_debug_object.reset();
    }

    void Workspace::request_pick(const float x, const float y, const bool select, const bool additive) noexcept {
        const std::pair<std::optional<std::uint64_t>, bool> debug = this->mode == RenderMode::Rasterizer ? this->debug_object_at(x, y) : std::pair<std::optional<std::uint64_t>, bool>{};
        this->picker->request(PickRequest{x, y, select, additive, debug.first, debug.second});
    }

    std::pair<std::optional<std::uint64_t>, bool> Workspace::debug_object_at(const float x, const float y) const noexcept {
        if (!this->dynamics) return {};
        const std::array<float, 16>& matrix = this->display_camera.matrices().view_projection;
        const auto project                  = [&matrix](const scene::Float3 point) -> std::optional<scene::Float2> {
            const float clip_x = matrix[0] * point.x + matrix[1] * point.y + matrix[2] * point.z + matrix[3];
            const float clip_y = matrix[4] * point.x + matrix[5] * point.y + matrix[6] * point.z + matrix[7];
            const float clip_w = matrix[12] * point.x + matrix[13] * point.y + matrix[14] * point.z + matrix[15];
            if (clip_w <= 0.0f) return std::nullopt;
            return scene::Float2{
                clip_x / clip_w * 0.5f + 0.5f,
                0.5f - clip_y / clip_w * 0.5f,
            };
        };
        const scene::Float2 cursor{x * this->viewport_aspect, y};
        float closest = 0.000144f;
        std::optional<std::uint64_t> object{};
        bool xray{};
        const auto test_segment = [&](const scene::Float3 first, const scene::Float3 second, const scene::dynamics::DebugPrimitive& primitive) {
            const std::optional<scene::Float2> projected_first  = project(first);
            const std::optional<scene::Float2> projected_second = project(second);
            if (!projected_first || !projected_second) return;
            const scene::Float2 a{projected_first->x * this->viewport_aspect, projected_first->y};
            const scene::Float2 b{projected_second->x * this->viewport_aspect, projected_second->y};
            const scene::Float2 edge{b.x - a.x, b.y - a.y};
            const scene::Float2 offset{cursor.x - a.x, cursor.y - a.y};
            const float length_squared   = edge.x * edge.x + edge.y * edge.y;
            const float t                = length_squared > 0.0f ? std::clamp((offset.x * edge.x + offset.y * edge.y) / length_squared, 0.0f, 1.0f) : 0.0f;
            const float dx               = cursor.x - (a.x + edge.x * t);
            const float dy               = cursor.y - (a.y + edge.y * t);
            const float distance_squared = dx * dx + dy * dy;
            if (distance_squared >= closest || primitive.pick == 0) return;
            closest = distance_squared;
            object  = primitive.pick;
            xray    = primitive.depth_mode == scene::dynamics::DebugDepthMode::XRay;
        };
        for (const scene::dynamics::DebugPrimitive& primitive : this->dynamics->debug_primitives) {
            if (primitive.kind == scene::dynamics::DebugPrimitiveKind::Point) {
                test_segment(primitive.first, primitive.first, primitive);
                continue;
            }
            if (primitive.kind == scene::dynamics::DebugPrimitiveKind::AxisAlignedBox) {
                const scene::Float3 minimum = primitive.first;
                const scene::Float3 maximum = primitive.second;
                const std::array corners{
                    scene::Float3{minimum.x, minimum.y, minimum.z},
                    scene::Float3{maximum.x, minimum.y, minimum.z},
                    scene::Float3{minimum.x, maximum.y, minimum.z},
                    scene::Float3{maximum.x, maximum.y, minimum.z},
                    scene::Float3{minimum.x, minimum.y, maximum.z},
                    scene::Float3{maximum.x, minimum.y, maximum.z},
                    scene::Float3{minimum.x, maximum.y, maximum.z},
                    scene::Float3{maximum.x, maximum.y, maximum.z},
                };
                constexpr std::array<std::array<std::uint32_t, 2>, 12> edges{{
                    {0, 1},
                    {2, 3},
                    {4, 5},
                    {6, 7},
                    {0, 2},
                    {1, 3},
                    {4, 6},
                    {5, 7},
                    {0, 4},
                    {1, 5},
                    {2, 6},
                    {3, 7},
                }};
                for (const std::array<std::uint32_t, 2> edge : edges) test_segment(corners[edge[0]], corners[edge[1]], primitive);
                continue;
            }
            test_segment(primitive.first, primitive.second, primitive);
        }
        return {object, xray};
    }

    void Workspace::begin_transform_edit(const scene::InstanceId instance) {
        if (this->transform_edit || (this->dynamics && this->dynamics->controls(instance))) return;
        const std::vector<scene::Instance>::const_iterator found = std::ranges::find(this->scene.resources.instances, instance, &scene::Instance::id);
        this->transform_edit                                     = TransformEdit{
                                                .instance = instance,
                                                .before   = found->transform,
                                                .after    = found->transform,
        };
    }

    void Workspace::update_transform_edit(scene::Transform transform) {
        if (!this->transform_edit) return;
        this->transform_edit->after = transform;
        scene::SceneUpdate{this->scene}.update_transform(this->transform_edit->instance, transform);
        scene::SceneUpdate{this->gpu_scene->state}.update_transform(this->transform_edit->instance, std::move(transform));
    }

    void Workspace::finish_transform_edit() {
        if (!this->transform_edit) return;
        if (this->transform_edit->before != this->transform_edit->after) this->push_edit(*this->transform_edit);
        this->transform_edit.reset();
    }

    bool Workspace::can_undo() const noexcept {
        return !this->undo_history.empty();
    }

    bool Workspace::can_redo() const noexcept {
        return !this->redo_history.empty();
    }

    void Workspace::push_edit(std::variant<TransformEdit, DynamicSetupEdit> edit) {
        std::visit(
            [this](auto& value) {
                value.before_serial       = this->current_edit_serial;
                value.after_serial        = this->next_edit_serial++;
                this->current_edit_serial = value.after_serial;
            },
            edit);
        this->undo_history.push_back(std::move(edit));
        this->redo_history.clear();
    }

    void Workspace::apply_edit(const std::variant<TransformEdit, DynamicSetupEdit>& edit, const bool before) {
        std::visit(
            [this, before](const auto& value) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, TransformEdit>) {
                    const scene::Transform transform = before ? value.before : value.after;
                    scene::SceneUpdate{this->scene}.update_transform(value.instance, transform);
                    scene::SceneUpdate{this->gpu_scene->state}.update_transform(value.instance, transform);
                } else {
                    const std::optional<scene::dynamics::TimelineState> timeline = value.preserve_simulation && this->dynamics ? std::optional{this->dynamics->timeline()} : std::nullopt;
                    const bool running                                           = value.preserve_simulation && this->dynamics && this->dynamics->running();
                    scene::Scene next_scene                                      = this->scene;
                    scene::SceneUpdate{next_scene}.update_dynamic_setup(before ? value.before : value.after);
                    std::unique_ptr<scene::dynamics::Runtime> next_dynamics{};
                    if (next_scene.dynamic_setup) next_dynamics = std::make_unique<scene::dynamics::Runtime>(*this->runtime, this->source_path, next_scene);
                    if (next_dynamics && timeline) {
                        next_dynamics->seek(timeline->step);
                        next_dynamics->set_running(running);
                    }
                    this->rebuild_renderers(next_scene, next_dynamics.get());
                    this->scene    = std::move(next_scene);
                    this->dynamics = std::move(next_dynamics);
                    if (this->dynamics) this->dynamics->bind_scene(this->scene);
                    this->camera_changed();
                }
            },
            edit);
    }

    void Workspace::undo() {
        if (this->undo_history.empty()) return;
        std::variant<TransformEdit, DynamicSetupEdit> edit = std::move(this->undo_history.back());
        this->undo_history.pop_back();
        this->apply_edit(edit, true);
        this->current_edit_serial = std::visit([](const auto& value) { return value.before_serial; }, edit);
        this->redo_history.push_back(std::move(edit));
    }

    void Workspace::redo() {
        if (this->redo_history.empty()) return;
        std::variant<TransformEdit, DynamicSetupEdit> edit = std::move(this->redo_history.back());
        this->redo_history.pop_back();
        this->apply_edit(edit, false);
        this->current_edit_serial = std::visit([](const auto& value) { return value.after_serial; }, edit);
        this->undo_history.push_back(std::move(edit));
    }

    std::optional<std::uint32_t> Workspace::instance_index(const scene::InstanceId id) const noexcept {
        const std::vector<scene::Instance>::const_iterator found = std::ranges::find(this->scene.resources.instances, id, &scene::Instance::id);
        if (found == this->scene.resources.instances.end()) return std::nullopt;
        return static_cast<std::uint32_t>(found - this->scene.resources.instances.begin());
    }

    void Workspace::prune_selection() noexcept {
        std::erase_if(this->selection.selected_instances, [this](const scene::InstanceId id) { return !this->instance_index(id); });
        if (this->selection.hovered && !this->instance_index(*this->selection.hovered)) this->selection.hovered.reset();
        if (this->selection.active && !this->instance_index(*this->selection.active)) this->selection.active.reset();
    }
} // namespace spectra::workspace
