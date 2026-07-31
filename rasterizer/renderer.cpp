module spectra.rasterizer;

import std;

namespace spectra::rasterizer {
    Rasterizer::Rasterizer(GpuDevice& gpu, const RasterScene& scene, const std::filesystem::path& shader_directory)
        : gpu(&gpu), scene(&scene), sampled_output_descriptor(gpu.allocate_resource_descriptor()) {
        this->create_shaders(shader_directory);
    }

    Rasterizer::~Rasterizer() {
        this->gpu->release_resource_descriptor(this->sampled_output_descriptor);
    }

    void Rasterizer::create_shaders(const std::filesystem::path& shader_directory) {
        const std::vector<std::uint32_t> mesh_code = load_spirv(shader_directory / "raster_mesh.spv");
        const std::vector<std::uint32_t> particle_code =
            load_spirv(
                shader_directory /
                "raster_particles.spv");
        const std::vector<std::uint32_t> fragment_code = load_spirv(shader_directory / "raster_fragment.spv");
        const std::array create_infos{
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eDescriptorHeap | vk::ShaderCreateFlagBitsEXT::eNoTaskShader,
                vk::ShaderStageFlagBits::eMeshEXT,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                mesh_code.size() * sizeof(std::uint32_t),
                mesh_code.data(),
                "raster_mesh",
            },
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eFragment,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                fragment_code.size() * sizeof(std::uint32_t),
                fragment_code.data(),
                "raster_fragment",
            },
        };
        this->shaders =
            vk::raii::ShaderEXTs{
                this->gpu->device,
                create_infos};
        this->particle_shader =
            vk::raii::ShaderEXT{
                this->gpu->device,
                vk::ShaderCreateInfoEXT{
                    vk::ShaderCreateFlagBitsEXT::
                            eDescriptorHeap |
                        vk::ShaderCreateFlagBitsEXT::
                            eNoTaskShader,
                    vk::ShaderStageFlagBits::
                        eMeshEXT,
                    {},
                    vk::ShaderCodeTypeEXT::eSpirv,
                    particle_code.size() *
                        sizeof(std::uint32_t),
                    particle_code.data(),
                    "raster_particles"}};
    }

    void Rasterizer::create_output(const vk::Extent2D extent) {
        this->output_image = this->gpu->create_image_2d(
            extent,
            vk::Format::eR32G32B32A32Sfloat,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled);
        this->depth_image = this->gpu->create_image_2d(
            extent,
            vk::Format::eD32Sfloat,
            vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::ImageAspectFlagBits::eDepth);
        this->gpu->write_sampled_image(this->sampled_output_descriptor, this->output_image, vk::ImageLayout::eShaderReadOnlyOptimal);
        this->output_layout = vk::ImageLayout::eUndefined;
        this->depth_layout = vk::ImageLayout::eUndefined;
    }

    void Rasterizer::prepare(const vk::Extent2D extent) {
        if (!*this->output_image.image || this->output_image.extent != extent) this->create_output(extent);
    }

    std::array<float, 16> Rasterizer::view_projection() const noexcept {
        return this->scene->camera.matrices().view_projection;
    }

    void Rasterizer::record(const vk::raii::CommandBuffer& command_buffer) {
        const std::array barriers{
            vk::ImageMemoryBarrier2{
                this->output_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                this->output_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                this->output_layout,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->output_image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            vk::ImageMemoryBarrier2{
                this->depth_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eLateFragmentTests,
                this->depth_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                this->depth_layout,
                vk::ImageLayout::eDepthAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->depth_image.image,
                {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1},
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 0, nullptr, static_cast<std::uint32_t>(barriers.size()), barriers.data()});

        const vk::RenderingAttachmentInfo color_attachment{
            *this->output_image.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::ClearValue{vk::ClearColorValue{std::array{0.01f, 0.015f, 0.025f, 1.0f}}},
        };
        const vk::RenderingAttachmentInfo depth_attachment{
            *this->depth_image.view,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eDontCare,
            vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0}},
        };
        command_buffer.beginRendering(vk::RenderingInfo{
            {},
            vk::Rect2D{{0, 0}, this->output_image.extent},
            1,
            0,
            1,
            &color_attachment,
            &depth_attachment,
        });

        const vk::Viewport viewport{
            0.0f,
            static_cast<float>(this->output_image.extent.height),
            static_cast<float>(this->output_image.extent.width),
            -static_cast<float>(this->output_image.extent.height),
            0.0f,
            1.0f,
        };
        const vk::Rect2D scissor{{0, 0}, this->output_image.extent};
        command_buffer.setViewportWithCount(viewport);
        command_buffer.setScissorWithCount(scissor);
        command_buffer.setCullMode(vk::CullModeFlagBits::eNone);
        command_buffer.setFrontFace(vk::FrontFace::eCounterClockwise);
        command_buffer.setDepthTestEnable(vk::True);
        command_buffer.setDepthWriteEnable(vk::True);
        command_buffer.setDepthCompareOp(vk::CompareOp::eLess);
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
        constexpr vk::ColorComponentFlags color_components =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        command_buffer.setColorWriteMaskEXT(0, color_components);

        const std::array stages{vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eFragment};
        const std::array shader_handles{*this->shaders[0], *this->shaders[1]};
        command_buffer.bindShadersEXT(stages, shader_handles);
        this->gpu->bind_descriptor_heaps(command_buffer);

        struct alignas(16) RasterPushData {
            DescriptorHandle positions;
            DescriptorHandle normals;
            DescriptorHandle tangents;
            DescriptorHandle texture_coordinates;
            DescriptorHandle indices;
            DescriptorHandle bindings;
            std::uint32_t instance_index;
            std::uint32_t element_count;
            std::array<std::uint32_t, 2> attributes;
            std::array<float, 4> view_projection_row_0;
            std::array<float, 4> view_projection_row_1;
            std::array<float, 4> view_projection_row_2;
            std::array<float, 4> view_projection_row_3;
            std::array<float, 4> camera_position;
        };
        static_assert(sizeof(RasterPushData) == 144);
        const std::array<float, 16> view_projection = this->view_projection();
        const scene::CameraFrame camera = this->scene->camera.frame();
        for (const render::GpuDraw& draw : this->scene->assets.draws) {
            if (
                draw.kind !=
                render::GpuDrawKind::Geometry)
                continue;
            const render::GpuGeometry& mesh =
                this->scene->assets.geometries[
                    draw.resource_index];
            const RasterPushData push_data{
                mesh.positions_descriptor,
                mesh.normals_descriptor,
                mesh.tangents_descriptor,
                mesh.texture_coordinates_descriptor,
                mesh.indices_descriptor,
                this->scene->bindings_descriptor,
                draw.instance_index,
                mesh.index_count / 3u,
                {mesh.attribute_flags, 0},
                {view_projection[0], view_projection[1], view_projection[2], view_projection[3]},
                {view_projection[4], view_projection[5], view_projection[6], view_projection[7]},
                {view_projection[8], view_projection[9], view_projection[10], view_projection[11]},
                {view_projection[12], view_projection[13], view_projection[14], view_projection[15]},
                {
                    camera.position.x,
                    camera.position.y,
                    camera.position.z,
                    1.0f},
            };
            this->gpu->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.drawMeshTasksEXT((push_data.element_count + 31u) / 32u, 1, 1);
        }
        const std::array particle_shader_handles{
            *this->particle_shader,
            *this->shaders[1]};
        command_buffer.bindShadersEXT(
            stages,
            particle_shader_handles);
        for (
            const render::GpuDraw& draw :
            this->scene->assets.draws) {
            if (
                draw.kind !=
                render::GpuDrawKind::ParticleSet)
                continue;
            const render::GpuParticleSet&
                particles =
                    this->scene->assets.particle_sets[
                        draw.resource_index];
            const RasterPushData push_data{
                particles.positions_descriptor,
                particles.radii_descriptor,
                particles.colors_descriptor,
                particles.positions_descriptor,
                particles.positions_descriptor,
                this->scene->bindings_descriptor,
                draw.instance_index,
                particles.particle_count,
                {particles.attribute_flags, 0},
                {
                    view_projection[0],
                    view_projection[1],
                    view_projection[2],
                    view_projection[3]},
                {
                    view_projection[4],
                    view_projection[5],
                    view_projection[6],
                    view_projection[7]},
                {
                    view_projection[8],
                    view_projection[9],
                    view_projection[10],
                    view_projection[11]},
                {
                    view_projection[12],
                    view_projection[13],
                    view_projection[14],
                    view_projection[15]},
                {
                    camera.position.x,
                    camera.position.y,
                    camera.position.z,
                    1.0f},
            };
            this->gpu->push_data(
                command_buffer,
                std::as_bytes(
                    std::span{
                        &push_data,
                        1}));
            command_buffer.drawMeshTasksEXT(
                (
                    push_data.element_count +
                    31u
                ) /
                    32u,
                1,
                1);
        }
        command_buffer.endRendering();
        this->output_layout = vk::ImageLayout::eColorAttachmentOptimal;
        this->depth_layout = vk::ImageLayout::eDepthAttachmentOptimal;
    }

    render::RenderOutput Rasterizer::output() const noexcept {
        return {
            this->output_image,
            this->sampled_output_descriptor,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite,
        };
    }
} // namespace spectra::rasterizer
