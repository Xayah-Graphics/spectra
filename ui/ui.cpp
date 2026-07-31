module;

#include <imgui.h>

module spectra.ui;

import std;

namespace spectra::ui {
    namespace {
        struct ImGuiTexture {
            GpuImage image{};
            DescriptorHandle descriptor{};
        };

        struct alignas(16) ImGuiPushData {
            DescriptorHandle vertices;
            DescriptorHandle indices;
            DescriptorHandle texture;
            DescriptorHandle sampler;
            std::uint32_t index_offset;
            std::uint32_t vertex_offset;
            std::array<float, 2> scale;
            std::array<float, 2> translation;
            std::array<std::uint32_t, 2> reserved;
        };
        static_assert(sizeof(ImGuiPushData) == 64);

    } // namespace

    ImGuiRenderer::ImGuiRenderer(
        GpuDevice& gpu,
        const std::filesystem::path& shader_directory,
        const std::uint32_t frames_in_flight)
        : gpu(&gpu),
          sampler_descriptor(gpu.allocate_sampler_descriptor()) {
        this->frames.reserve(frames_in_flight);
        for (std::uint32_t index = 0; index < frames_in_flight; ++index)
            this->frames.emplace_back(
                GpuBuffer{},
                GpuBuffer{},
                gpu.allocate_resource_descriptor(),
                gpu.allocate_resource_descriptor());
        const std::vector<std::uint32_t> vertex_code = load_spirv(shader_directory / "imgui_vertex.spv");
        const std::vector<std::uint32_t> fragment_code = load_spirv(shader_directory / "imgui_fragment.spv");
        const std::array create_infos{
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eVertex,
                vk::ShaderStageFlagBits::eFragment,
                vk::ShaderCodeTypeEXT::eSpirv,
                vertex_code.size() * sizeof(std::uint32_t),
                vertex_code.data(),
                "imgui_vertex",
            },
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eFragment,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                fragment_code.size() * sizeof(std::uint32_t),
                fragment_code.data(),
                "imgui_fragment",
            },
        };
        this->shaders = vk::raii::ShaderEXTs{gpu.device, create_infos};
        gpu.write_sampler(this->sampler_descriptor, vk::SamplerCreateInfo{
            {},
            vk::Filter::eLinear,
            vk::Filter::eLinear,
            vk::SamplerMipmapMode::eLinear,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            0.0f,
            vk::False,
            1.0f,
            vk::False,
            vk::CompareOp::eNever,
            -1000.0f,
            1000.0f,
        });

        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = "spectra_shader_object";
        io.BackendRendererUserData = this;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    }

    ImGuiRenderer::~ImGuiRenderer() {
        for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
            if (texture->BackendUserData != nullptr) this->destroy_texture(*texture);
        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = nullptr;
        io.BackendRendererUserData = nullptr;
        io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
        for (const FrameResources& frame : this->frames) {
            this->gpu->release_resource_descriptor(frame.vertex_descriptor);
            this->gpu->release_resource_descriptor(frame.index_descriptor);
        }
        this->gpu->release_sampler_descriptor(this->sampler_descriptor);
    }

    void ImGuiRenderer::update_texture(ImTextureData& texture) {
        if (texture.Status == ImTextureStatus_WantDestroy) {
            if (texture.UnusedFrames >= static_cast<int>(this->frames.size())) this->destroy_texture(texture);
            return;
        }
        if (texture.Status != ImTextureStatus_WantCreate && texture.Status != ImTextureStatus_WantUpdates) return;

        if (texture.Status == ImTextureStatus_WantCreate) {
            ImGuiTexture* backend_texture = new ImGuiTexture{
                this->gpu->create_image_2d(
                    {static_cast<std::uint32_t>(texture.Width), static_cast<std::uint32_t>(texture.Height)},
                    vk::Format::eR8G8B8A8Unorm,
                    vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst),
                this->gpu->allocate_resource_descriptor(),
            };
            this->gpu->write_sampled_image(
                backend_texture->descriptor,
                backend_texture->image,
                vk::ImageLayout::eShaderReadOnlyOptimal);
            texture.BackendUserData = backend_texture;
            texture.SetTexID(static_cast<ImTextureID>(backend_texture->descriptor.index) + 1u);
        }

        ImGuiTexture& backend_texture = *static_cast<ImGuiTexture*>(texture.BackendUserData);
        const int upload_x = texture.Status == ImTextureStatus_WantCreate ? 0 : texture.UpdateRect.x;
        const int upload_y = texture.Status == ImTextureStatus_WantCreate ? 0 : texture.UpdateRect.y;
        const int upload_width = texture.Status == ImTextureStatus_WantCreate ? texture.Width : texture.UpdateRect.w;
        const int upload_height = texture.Status == ImTextureStatus_WantCreate ? texture.Height : texture.UpdateRect.h;
        const vk::DeviceSize upload_size =
            static_cast<vk::DeviceSize>(upload_width) *
            static_cast<vk::DeviceSize>(upload_height) *
            4u;
        GpuBuffer upload = this->gpu->create_buffer(
            upload_size,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            true);
        std::byte* destination = static_cast<std::byte*>(upload.mapped);
        for (int row = 0; row < upload_height; ++row) {
            const unsigned char* source = static_cast<const unsigned char*>(texture.GetPixelsAt(upload_x, upload_y + row));
            if (texture.Format == ImTextureFormat_RGBA32) {
                std::memcpy(destination + static_cast<std::size_t>(row * upload_width * 4), source, static_cast<std::size_t>(upload_width * 4));
                continue;
            }
            for (int column = 0; column < upload_width; ++column) {
                const std::size_t offset = static_cast<std::size_t>((row * upload_width + column) * 4);
                destination[offset] = std::byte{0xff};
                destination[offset + 1] = std::byte{0xff};
                destination[offset + 2] = std::byte{0xff};
                destination[offset + 3] = static_cast<std::byte>(source[column]);
            }
        }

        const bool creating = texture.Status == ImTextureStatus_WantCreate;
        this->gpu->immediate([&](const vk::raii::CommandBuffer& command_buffer) {
            const vk::ImageMemoryBarrier2 to_transfer{
                creating ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eFragmentShader,
                creating ? vk::AccessFlags2{} : vk::AccessFlagBits2::eShaderSampledRead,
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                creating ? vk::ImageLayout::eUndefined : vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eTransferDstOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *backend_texture.image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_transfer});
            command_buffer.copyBufferToImage(
                *upload.buffer,
                *backend_texture.image.image,
                vk::ImageLayout::eTransferDstOptimal,
                vk::BufferImageCopy{
                    0,
                    0,
                    0,
                    {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                    {upload_x, upload_y, 0},
                    {
                        static_cast<std::uint32_t>(upload_width),
                        static_cast<std::uint32_t>(upload_height),
                        1,
                    },
                });
            const vk::ImageMemoryBarrier2 to_shader{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderSampledRead,
                vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *backend_texture.image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_shader});
        });
        texture.SetStatus(ImTextureStatus_OK);
    }

    void ImGuiRenderer::destroy_texture(ImTextureData& texture) {
        ImGuiTexture* backend_texture = static_cast<ImGuiTexture*>(texture.BackendUserData);
        this->gpu->release_resource_descriptor(backend_texture->descriptor);
        delete backend_texture;
        texture.BackendUserData = nullptr;
        texture.SetTexID(ImTextureID_Invalid);
        texture.SetStatus(ImTextureStatus_Destroyed);
    }

    void ImGuiRenderer::setup_render_state(
        const vk::raii::CommandBuffer& command_buffer,
        const ImDrawData& draw_data,
        const FrameResources& frame,
        const vk::Extent2D extent) {
        command_buffer.setViewportWithCount(vk::Viewport{
            0.0f,
            0.0f,
            static_cast<float>(extent.width),
            static_cast<float>(extent.height),
            0.0f,
            1.0f,
        });
        command_buffer.setCullMode(vk::CullModeFlagBits::eNone);
        command_buffer.setFrontFace(vk::FrontFace::eCounterClockwise);
        command_buffer.setDepthTestEnable(vk::False);
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
        constexpr vk::ColorComponentFlags color_components =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        command_buffer.setColorWriteMaskEXT(0, color_components);

        const std::array stages{
            vk::ShaderStageFlagBits::eTaskEXT,
            vk::ShaderStageFlagBits::eMeshEXT,
            vk::ShaderStageFlagBits::eVertex,
            vk::ShaderStageFlagBits::eFragment,
        };
        const std::array shader_handles{
            vk::ShaderEXT{},
            vk::ShaderEXT{},
            *this->shaders[0],
            *this->shaders[1],
        };
        command_buffer.bindShadersEXT(stages, shader_handles);
        this->gpu->bind_descriptor_heaps(command_buffer);

        const ImGuiPushData push_data{
            frame.vertex_descriptor,
            frame.index_descriptor,
            {},
            this->sampler_descriptor,
            0,
            0,
            {
                2.0f / draw_data.DisplaySize.x,
                2.0f / draw_data.DisplaySize.y,
            },
            {
                -1.0f - draw_data.DisplayPos.x * 2.0f / draw_data.DisplaySize.x,
                -1.0f - draw_data.DisplayPos.y * 2.0f / draw_data.DisplaySize.y,
            },
            {},
        };
        this->gpu->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
    }

    void ImGuiRenderer::record(
        ImDrawData& draw_data,
        const vk::raii::CommandBuffer& command_buffer,
        const std::uint32_t frame_index,
        const vk::Image target_image,
        const vk::ImageView target_view,
        const vk::Extent2D extent,
        const vk::ImageLayout target_layout,
        const vk::ImageLayout final_layout) {
        FrameResources& frame = this->frames[frame_index];
        if (draw_data.Textures != nullptr)
            for (ImTextureData* texture : *draw_data.Textures) this->update_texture(*texture);

        if (draw_data.TotalVtxCount != 0) {
            static_assert(sizeof(ImDrawVert) == 20);
            static_assert(offsetof(ImDrawVert, pos) == 0);
            static_assert(offsetof(ImDrawVert, uv) == 8);
            static_assert(offsetof(ImDrawVert, col) == 16);
            const std::size_t required_vertex_bytes = static_cast<std::size_t>(draw_data.TotalVtxCount) * sizeof(ImDrawVert);
            const std::size_t required_index_bytes = static_cast<std::size_t>(draw_data.TotalIdxCount) * sizeof(std::uint32_t);
            if (required_vertex_bytes > frame.vertex_capacity) {
                frame.vertex_capacity = std::bit_ceil(std::max(required_vertex_bytes, std::size_t{4096}));
                frame.vertex_buffer = this->gpu->create_buffer(
                    frame.vertex_capacity,
                    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                    true);
                this->gpu->write_buffer(frame.vertex_descriptor, vk::DescriptorType::eStorageBuffer, frame.vertex_buffer);
            }
            if (required_index_bytes > frame.index_capacity) {
                frame.index_capacity = std::bit_ceil(std::max(required_index_bytes, std::size_t{4096}));
                frame.index_buffer = this->gpu->create_buffer(
                    frame.index_capacity,
                    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                    true);
                this->gpu->write_buffer(frame.index_descriptor, vk::DescriptorType::eStorageBuffer, frame.index_buffer);
            }

            std::byte* vertex_destination = static_cast<std::byte*>(frame.vertex_buffer.mapped);
            std::uint32_t* index_destination = static_cast<std::uint32_t*>(frame.index_buffer.mapped);
            for (const ImDrawList* draw_list : draw_data.CmdLists) {
                const std::size_t vertex_bytes = static_cast<std::size_t>(draw_list->VtxBuffer.Size) * sizeof(ImDrawVert);
                std::memcpy(vertex_destination, draw_list->VtxBuffer.Data, vertex_bytes);
                vertex_destination += vertex_bytes;
                for (const ImDrawIdx index : draw_list->IdxBuffer) *index_destination++ = index;
            }
        }

        const vk::ImageMemoryBarrier2 target_to_color{
            target_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eBottomOfPipe,
            {},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            target_layout,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            target_image,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &target_to_color});

        const vk::RenderingAttachmentInfo color_attachment{
            target_view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::ClearValue{vk::ClearColorValue{std::array{0.018f, 0.022f, 0.030f, 1.0f}}},
        };
        command_buffer.beginRendering(vk::RenderingInfo{
            {},
            vk::Rect2D{{0, 0}, extent},
            1,
            0,
            1,
            &color_attachment,
        });
        if (draw_data.TotalVtxCount != 0) {
            const vk::MemoryBarrier2 host_to_shader{
                vk::PipelineStageFlagBits2::eHost,
                vk::AccessFlagBits2::eHostWrite,
                vk::PipelineStageFlagBits2::eVertexShader,
                vk::AccessFlagBits2::eShaderStorageRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &host_to_shader});
            this->setup_render_state(command_buffer, draw_data, frame, extent);

            const std::array scale{
                2.0f / draw_data.DisplaySize.x,
                2.0f / draw_data.DisplaySize.y,
            };
            const std::array translation{
                -1.0f - draw_data.DisplayPos.x * scale[0],
                -1.0f - draw_data.DisplayPos.y * scale[1],
            };
            std::uint32_t global_index_offset = 0;
            std::uint32_t global_vertex_offset = 0;
            for (const ImDrawList* draw_list : draw_data.CmdLists) {
                for (const ImDrawCmd& draw_command : draw_list->CmdBuffer) {
                    if (draw_command.UserCallback != nullptr) {
                        if (draw_command.UserCallback == ImDrawCallback_ResetRenderState)
                            this->setup_render_state(command_buffer, draw_data, frame, extent);
                        else
                            draw_command.UserCallback(draw_list, &draw_command);
                        continue;
                    }

                    ImVec2 clip_min{
                        (draw_command.ClipRect.x - draw_data.DisplayPos.x) * draw_data.FramebufferScale.x,
                        (draw_command.ClipRect.y - draw_data.DisplayPos.y) * draw_data.FramebufferScale.y,
                    };
                    ImVec2 clip_max{
                        (draw_command.ClipRect.z - draw_data.DisplayPos.x) * draw_data.FramebufferScale.x,
                        (draw_command.ClipRect.w - draw_data.DisplayPos.y) * draw_data.FramebufferScale.y,
                    };
                    clip_min.x = std::max(clip_min.x, 0.0f);
                    clip_min.y = std::max(clip_min.y, 0.0f);
                    clip_max.x = std::min(clip_max.x, static_cast<float>(extent.width));
                    clip_max.y = std::min(clip_max.y, static_cast<float>(extent.height));
                    if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) continue;
                    command_buffer.setScissorWithCount(vk::Rect2D{
                        {
                            static_cast<std::int32_t>(clip_min.x),
                            static_cast<std::int32_t>(clip_min.y),
                        },
                        {
                            static_cast<std::uint32_t>(clip_max.x - clip_min.x),
                            static_cast<std::uint32_t>(clip_max.y - clip_min.y),
                        },
                    });

                    const ImGuiPushData push_data{
                        frame.vertex_descriptor,
                        frame.index_descriptor,
                        DescriptorHandle{static_cast<std::uint32_t>(draw_command.GetTexID() - 1u)},
                        this->sampler_descriptor,
                        global_index_offset + draw_command.IdxOffset,
                        global_vertex_offset + draw_command.VtxOffset,
                        scale,
                        translation,
                        {},
                    };
                    this->gpu->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
                    command_buffer.draw(draw_command.ElemCount, 1, 0, 0);
                }
                global_index_offset += static_cast<std::uint32_t>(draw_list->IdxBuffer.Size);
                global_vertex_offset += static_cast<std::uint32_t>(draw_list->VtxBuffer.Size);
            }
        }
        command_buffer.endRendering();

        const vk::ImageMemoryBarrier2 to_final{
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            final_layout == vk::ImageLayout::ePresentSrcKHR ? vk::PipelineStageFlagBits2::eBottomOfPipe : vk::PipelineStageFlagBits2::eCopy,
            final_layout == vk::ImageLayout::eTransferSrcOptimal ? vk::AccessFlagBits2::eTransferRead : vk::AccessFlags2{},
            vk::ImageLayout::eColorAttachmentOptimal,
            final_layout,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            target_image,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_final});
    }
} // namespace spectra::ui
