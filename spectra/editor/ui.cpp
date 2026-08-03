module;

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

module spectra.editor;

import :ui;

import std;
import vulkan;

namespace spectra {
    EditorUi::EditorUi(VulkanRuntime& runtime, SceneDocument& document, DynamicWorld& dynamics, Renderers& renderers, EditorInteraction& interaction, EditorViewport& viewport, EditorOutput& output, std::filesystem::path shader_directory) noexcept : context{runtime, document, dynamics, renderers, interaction, viewport, output, std::move(shader_directory)} {}

    EditorUi::~EditorUi() {
        if (!this->lifetime.initialized) return;
        this->context.runtime.graphics.device.waitIdle();
        for (const ImGuiFrameResources& frame : this->renderer.frames) {
            this->context.runtime.frames.retire_resource_descriptor(frame.vertex_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(frame.index_descriptor);
        }
        this->context.runtime.frames.retire_sampler_descriptor(this->renderer.sampler_descriptor);
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void EditorUi::initialize() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io    = ImGui::GetIO();
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGuiStyle& style       = ImGui::GetStyle();
        style.WindowRounding    = 0.0f;
        style.WindowBorderSize  = 0.0f;
        style.ChildRounding     = 0.0f;
        style.FrameRounding     = 6.0f;
        style.PopupRounding     = 8.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding      = 5.0f;
        style.FramePadding      = {8.0f, 5.0f};
        style.ItemSpacing       = {8.0f, 6.0f};
        style.Colors[ImGuiCol_WindowBg] = ImVec4{0.025f, 0.035f, 0.045f, 1.0f};
        if (!ImGui_ImplGlfw_InitForVulkan(this->context.runtime.platform.window, true)) throw std::runtime_error("Failed to initialize ImGui GLFW platform backend");
        io.BackendRendererName                  = "spectra.editor.ui";
        io.BackendFlags                        |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        this->initialize_renderer();
        this->lifetime.initialized = true;
    }

    void EditorUi::notify(std::string message, const bool error) {
        this->controls.status       = std::move(message);
        this->controls.status_error = error;
    }
} // namespace spectra

namespace spectra {
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

    void EditorUi::initialize_renderer() {
        this->renderer.sampler_descriptor = this->context.runtime.resources.allocate_sampler_descriptor();
        this->renderer.frames.reserve(VulkanFrames::frames_in_flight);
        for (std::uint32_t index = 0; index < VulkanFrames::frames_in_flight; ++index) this->renderer.frames.emplace_back(GpuBuffer{}, GpuBuffer{}, this->context.runtime.resources.allocate_resource_descriptor(), this->context.runtime.resources.allocate_resource_descriptor());
        const std::vector<std::uint32_t> vertex_code   = load_spirv(this->context.shader_directory / "imgui_vertex.spv");
        const std::vector<std::uint32_t> fragment_code = load_spirv(this->context.shader_directory / "imgui_fragment.spv");
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
        this->renderer.shaders = vk::raii::ShaderEXTs{this->context.runtime.graphics.device, create_infos};
        this->context.runtime.resources.write_sampler_descriptor(this->renderer.sampler_descriptor, vk::SamplerCreateInfo{
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

        ImGuiIO& io                = ImGui::GetIO();
        io.BackendRendererName     = "spectra_shader_object";
        io.BackendRendererUserData = this;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    }

    void EditorUi::update_imgui_texture(ImTextureData& texture) {
        if (texture.Status == ImTextureStatus_WantDestroy) {
            if (texture.UnusedFrames >= static_cast<int>(this->renderer.frames.size())) this->destroy_imgui_texture(texture);
            return;
        }
        if (texture.Status != ImTextureStatus_WantCreate && texture.Status != ImTextureStatus_WantUpdates) return;

        if (texture.Status == ImTextureStatus_WantCreate) {
            ImGuiTexture* backend_texture = new ImGuiTexture{
                this->context.runtime.resources.create_image_2d({static_cast<std::uint32_t>(texture.Width), static_cast<std::uint32_t>(texture.Height)}, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst),
                this->context.runtime.resources.allocate_resource_descriptor(),
            };
            this->context.runtime.resources.write_sampled_image_descriptor(backend_texture->descriptor, backend_texture->image, vk::ImageLayout::eShaderReadOnlyOptimal);
            texture.BackendUserData = backend_texture;
            texture.SetTexID(static_cast<ImTextureID>(backend_texture->descriptor.slot_index) + 1u);
        }

        ImGuiTexture& backend_texture    = *static_cast<ImGuiTexture*>(texture.BackendUserData);
        const int upload_x               = texture.Status == ImTextureStatus_WantCreate ? 0 : texture.UpdateRect.x;
        const int upload_y               = texture.Status == ImTextureStatus_WantCreate ? 0 : texture.UpdateRect.y;
        const int upload_width           = texture.Status == ImTextureStatus_WantCreate ? texture.Width : texture.UpdateRect.w;
        const int upload_height          = texture.Status == ImTextureStatus_WantCreate ? texture.Height : texture.UpdateRect.h;
        const vk::DeviceSize upload_size = static_cast<vk::DeviceSize>(upload_width) * static_cast<vk::DeviceSize>(upload_height) * 4u;
        GpuBuffer upload                 = this->context.runtime.resources.create_buffer(upload_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        std::byte* destination           = static_cast<std::byte*>(upload.mapped);
        for (int row = 0; row < upload_height; ++row) {
            const unsigned char* source = static_cast<const unsigned char*>(texture.GetPixelsAt(upload_x, upload_y + row));
            if (texture.Format == ImTextureFormat_RGBA32) {
                std::memcpy(destination + static_cast<std::size_t>(row * upload_width * 4), source, static_cast<std::size_t>(upload_width * 4));
                continue;
            }
            for (int column = 0; column < upload_width; ++column) {
                const std::size_t offset = static_cast<std::size_t>((row * upload_width + column) * 4);
                destination[offset]      = std::byte{0xff};
                destination[offset + 1]  = std::byte{0xff};
                destination[offset + 2]  = std::byte{0xff};
                destination[offset + 3]  = static_cast<std::byte>(source[column]);
            }
        }

        const bool creating = texture.Status == ImTextureStatus_WantCreate;
        this->context.runtime.resources.submit_immediate([&](const vk::raii::CommandBuffer& command_buffer) {
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
            command_buffer.copyBufferToImage(*upload.buffer, *backend_texture.image.image, vk::ImageLayout::eTransferDstOptimal,
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

    void EditorUi::destroy_imgui_texture(ImTextureData& texture) {
        ImGuiTexture* backend_texture = static_cast<ImGuiTexture*>(texture.BackendUserData);
        this->context.runtime.frames.retire_resource_descriptor(backend_texture->descriptor);
        delete backend_texture;
        texture.BackendUserData = nullptr;
        texture.SetTexID(ImTextureID_Invalid);
        texture.SetStatus(ImTextureStatus_Destroyed);
    }

    void EditorUi::setup_imgui_render_state(const vk::raii::CommandBuffer& command_buffer, const ImDrawData& draw_data, const ImGuiFrameResources& frame, const vk::Extent2D extent) {
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
        constexpr vk::ColorComponentFlags color_components = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
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
            *this->renderer.shaders[0],
            *this->renderer.shaders[1],
        };
        command_buffer.bindShadersEXT(stages, shader_handles);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);

        const ImGuiPushData push_data{
            frame.vertex_descriptor,
            frame.index_descriptor,
            {},
            this->renderer.sampler_descriptor,
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
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
    }

    void EditorUi::record_imgui(ImDrawData& draw_data, const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index, const vk::Image target_image, const vk::ImageView target_view, const vk::Extent2D extent, const vk::ImageLayout target_layout, const vk::ImageLayout final_layout) {
        ImGuiFrameResources& frame = this->renderer.frames[frame_slot_index];
        if (draw_data.Textures != nullptr)
            for (ImTextureData* texture : *draw_data.Textures) this->update_imgui_texture(*texture);

        if (draw_data.TotalVtxCount != 0) {
            static_assert(sizeof(ImDrawVert) == 20);
            static_assert(offsetof(ImDrawVert, pos) == 0);
            static_assert(offsetof(ImDrawVert, uv) == 8);
            static_assert(offsetof(ImDrawVert, col) == 16);
            const std::size_t required_vertex_bytes = static_cast<std::size_t>(draw_data.TotalVtxCount) * sizeof(ImDrawVert);
            const std::size_t required_index_bytes  = static_cast<std::size_t>(draw_data.TotalIdxCount) * sizeof(std::uint32_t);
            if (required_vertex_bytes > frame.vertex_capacity) {
                frame.vertex_capacity = std::bit_ceil(std::max(required_vertex_bytes, std::size_t{4096}));
                frame.vertex_buffer   = this->context.runtime.resources.create_buffer(frame.vertex_capacity, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
                this->context.runtime.resources.write_buffer_descriptor(frame.vertex_descriptor, vk::DescriptorType::eStorageBuffer, frame.vertex_buffer);
            }
            if (required_index_bytes > frame.index_capacity) {
                frame.index_capacity = std::bit_ceil(std::max(required_index_bytes, std::size_t{4096}));
                frame.index_buffer   = this->context.runtime.resources.create_buffer(frame.index_capacity, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
                this->context.runtime.resources.write_buffer_descriptor(frame.index_descriptor, vk::DescriptorType::eStorageBuffer, frame.index_buffer);
            }

            std::byte* vertex_destination    = static_cast<std::byte*>(frame.vertex_buffer.mapped);
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
            this->setup_imgui_render_state(command_buffer, draw_data, frame, extent);

            const std::array scale{
                2.0f / draw_data.DisplaySize.x,
                2.0f / draw_data.DisplaySize.y,
            };
            const std::array translation{
                -1.0f - draw_data.DisplayPos.x * scale[0],
                -1.0f - draw_data.DisplayPos.y * scale[1],
            };
            std::uint32_t global_index_offset  = 0;
            std::uint32_t global_vertex_offset = 0;
            for (const ImDrawList* draw_list : draw_data.CmdLists) {
                for (const ImDrawCmd& draw_command : draw_list->CmdBuffer) {
                    if (draw_command.UserCallback != nullptr) {
                        if (draw_command.UserCallback == ImDrawCallback_ResetRenderState)
                            this->setup_imgui_render_state(command_buffer, draw_data, frame, extent);
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
                        this->renderer.sampler_descriptor,
                        global_index_offset + draw_command.IdxOffset,
                        global_vertex_offset + draw_command.VtxOffset,
                        scale,
                        translation,
                        {},
                    };
                    this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
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

    namespace {
        constexpr float top_strip_height = 36.0f;

        enum class Icon : std::uint8_t {
            Translate,
            Rotate,
            Scale,
            Capture,
            Close,
            Play,
            Pause,
            Step,
            Reset,
        };

        struct FlatButtonInteraction {
            bool clicked{};
            bool hovered{};
            bool active{};
            ImVec2 minimum{};
            ImVec2 maximum{};
            ImU32 color{};
            ImU32 shadow{};
            float vertical_offset{};
        };

        [[nodiscard]] std::array<float, 16> column_major(const std::array<float, 16>& row_major) noexcept {
            std::array<float, 16> result{};
            for (std::uint32_t row = 0; row < 4; ++row)
                for (std::uint32_t column = 0; column < 4; ++column) result[column * 4u + row] = row_major[row * 4u + column];
            return result;
        }

        [[nodiscard]] math::Transform row_major_transform(const std::array<float, 16>& column_matrix) noexcept {
            math::Transform result{};
            for (std::uint32_t row = 0; row < 4; ++row)
                for (std::uint32_t column = 0; column < 4; ++column) result.matrix[row * 4u + column] = column_matrix[column * 4u + row];
            return result;
        }

        [[nodiscard]] const scene::Instance* selected_instance(const EditorUi& editor) noexcept {
            if (!editor.context.interaction.view.selection.active_instance) return nullptr;
            const std::vector<scene::Instance>::const_iterator found = std::ranges::find(editor.context.document.content.source.resources.instances, *editor.context.interaction.view.selection.active_instance, &scene::Instance::id);
            return found == editor.context.document.content.source.resources.instances.end() ? nullptr : std::to_address(found);
        }

        [[nodiscard]] std::optional<ImVec2> project(const math::Float3 point, const scene::CameraMatrices& matrices, const ImVec2 minimum, const ImVec2 size) noexcept {
            const std::array<float, 4> clip{
                matrices.view_projection[0] * point.x + matrices.view_projection[1] * point.y + matrices.view_projection[2] * point.z + matrices.view_projection[3],
                matrices.view_projection[4] * point.x + matrices.view_projection[5] * point.y + matrices.view_projection[6] * point.z + matrices.view_projection[7],
                matrices.view_projection[8] * point.x + matrices.view_projection[9] * point.y + matrices.view_projection[10] * point.z + matrices.view_projection[11],
                matrices.view_projection[12] * point.x + matrices.view_projection[13] * point.y + matrices.view_projection[14] * point.z + matrices.view_projection[15],
            };
            if (clip[3] <= 0.0f) return std::nullopt;
            return ImVec2{
                minimum.x + (clip[0] / clip[3] * 0.5f + 0.5f) * size.x,
                minimum.y + (0.5f - clip[1] / clip[3] * 0.5f) * size.y,
            };
        }

        [[nodiscard]] FlatButtonInteraction flat_button(const char* identifier, const ImVec2 size, const ImVec4 semantic_color = {}, const bool dangerous = false, const float default_alpha = 0.68f) {
            FlatButtonInteraction interaction{};
            interaction.clicked = ImGui::InvisibleButton(identifier, size);
            interaction.hovered = ImGui::IsItemHovered();
            interaction.active  = ImGui::IsItemActive();
            interaction.minimum = ImGui::GetItemRectMin();
            interaction.maximum = ImGui::GetItemRectMax();
            const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
            ImVec4 color{0.88f, 0.91f, 0.95f, default_alpha};
            if (disabled)
                color.w = 0.28f / ImGui::GetStyle().DisabledAlpha;
            else if (dangerous && interaction.hovered)
                color = {0.94f, 0.35f, 0.35f, interaction.active ? 0.85f : 1.0f};
            else if (semantic_color.w > 0.0f) {
                color = semantic_color;
                if (interaction.active) color.w = 0.85f;
            } else if (interaction.active)
                color.w = 0.85f;
            else if (interaction.hovered)
                color.w = 1.0f;
            interaction.color           = ImGui::GetColorU32(color);
            interaction.shadow          = ImGui::GetColorU32(ImVec4{0.0f, 0.0f, 0.0f, disabled ? color.w : std::min(color.w, 0.78f)});
            interaction.vertical_offset = interaction.active && !disabled ? 1.0f : 0.0f;
            if (ImGui::IsItemFocused()) {
                const float center = (interaction.minimum.x + interaction.maximum.x) * 0.5f;
                ImGui::GetWindowDrawList()->AddLine(ImVec2{center - 6.0f, interaction.maximum.y - 1.0f}, ImVec2{center + 6.0f, interaction.maximum.y - 1.0f}, ImGui::GetColorU32(ImVec4{0.88f, 0.91f, 0.95f, 0.85f}), 1.0f);
            }
            return interaction;
        }

        [[nodiscard]] bool text_button(const char* identifier, const char* text, const ImVec2 size, const bool selected = false, const bool dangerous = false) {
            const FlatButtonInteraction interaction = flat_button(identifier, size, selected ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{}, dangerous);
            const ImVec2 text_size                  = ImGui::CalcTextSize(text);
            const ImVec2 text_position{
                interaction.minimum.x + (size.x - text_size.x) * 0.5f,
                interaction.minimum.y + (size.y - text_size.y) * 0.5f + interaction.vertical_offset,
            };
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->PushClipRect(interaction.minimum, interaction.maximum, true);
            draw_list->AddText(ImVec2{text_position.x + 1.0f, text_position.y + 1.0f}, interaction.shadow, text);
            draw_list->AddText(text_position, interaction.color, text);
            draw_list->PopClipRect();
            if (selected) {
                const float center     = (interaction.minimum.x + interaction.maximum.x) * 0.5f;
                const float half_width = std::clamp(text_size.x, 28.0f, 36.0f) * 0.5f;
                draw_list->AddLine(ImVec2{center - half_width, interaction.maximum.y - 3.0f}, ImVec2{center + half_width, interaction.maximum.y - 3.0f}, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 1.0f}), 2.0f);
            }
            return interaction.clicked;
        }

        void draw_scene_camera(const EditorUi& editor, const ImVec2 minimum, const ImVec2 size, ImDrawList& draw_list) {
            if (!editor.context.interaction.view.overlays_visible || editor.context.interaction.view.source == CameraSource::Scene) return;
            const scene::Camera& camera      = editor.context.document.content.source.camera();
            const scene::CameraFrame frame   = camera.frame();
            constexpr float distance         = 0.5f;
            const math::Float3 plane_center = frame.position + frame.forward * distance;
            std::array<math::Float2, 4> screen_corners{};
            if (const scene::PerspectiveCameraData* perspective = std::get_if<scene::PerspectiveCameraData>(&camera.data)) {
                const float scale = std::tan(perspective->vertical_fov * std::numbers::pi_v<float> / 360.0f) * distance;
                screen_corners    = {{
                    {perspective->screen_window.minimum.x * scale, perspective->screen_window.minimum.y * scale},
                    {perspective->screen_window.maximum.x * scale, perspective->screen_window.minimum.y * scale},
                    {perspective->screen_window.maximum.x * scale, perspective->screen_window.maximum.y * scale},
                    {perspective->screen_window.minimum.x * scale, perspective->screen_window.maximum.y * scale},
                }};
            } else {
                const scene::ScreenWindow& screen_window = std::get<scene::OrthographicCameraData>(camera.data).screen_window;
                screen_corners                           = {{{screen_window.minimum.x, screen_window.minimum.y}, {screen_window.maximum.x, screen_window.minimum.y}, {screen_window.maximum.x, screen_window.maximum.y}, {screen_window.minimum.x, screen_window.maximum.y}}};
            }
            const bool orthographic = std::holds_alternative<scene::OrthographicCameraData>(camera.data);
            std::array<math::Float3, 4> bases{};
            std::array<math::Float3, 4> corners{};
            for (std::size_t index = 0; index < corners.size(); ++index) {
                bases[index]   = orthographic ? frame.position + frame.right * screen_corners[index].x + frame.up * screen_corners[index].y : frame.position;
                corners[index] = plane_center + frame.right * screen_corners[index].x + frame.up * screen_corners[index].y;
            }
            const scene::CameraMatrices matrices = editor.context.interaction.view.render_camera.matrices();
            const ImU32 color                    = ImGui::GetColorU32(ImVec4{1.0f, 0.67f, 0.16f, 0.72f});
            for (std::size_t index = 0; index < corners.size(); ++index) {
                const std::optional<ImVec2> base      = project(bases[index], matrices, minimum, size);
                const std::optional<ImVec2> next_base = project(bases[(index + 1) % bases.size()], matrices, minimum, size);
                const std::optional<ImVec2> corner    = project(corners[index], matrices, minimum, size);
                const std::optional<ImVec2> next      = project(corners[(index + 1) % corners.size()], matrices, minimum, size);
                if (base && corner) draw_list.AddLine(*base, *corner, color, 1.4f);
                if (orthographic && base && next_base) draw_list.AddLine(*base, *next_base, color, 1.4f);
                if (corner && next) draw_list.AddLine(*corner, *next, color, 1.4f);
            }
        }

        [[nodiscard]] bool icon_button(const char* identifier, const ImVec2 size, const Icon icon, const char* text = nullptr, const ImVec4 semantic_color = {}, const bool selected = false, const bool dangerous = false, const float default_alpha = 0.68f) {
            const FlatButtonInteraction interaction = flat_button(identifier, size, semantic_color, dangerous, default_alpha);
            const ImVec2 text_size                  = text ? ImGui::CalcTextSize(text) : ImVec2{};
            const float content_width               = 14.0f + (text ? text_size.x + 4.0f : 0.0f);
            const ImVec2 center{
                interaction.minimum.x + (size.x - content_width) * 0.5f + 7.0f,
                interaction.minimum.y + size.y * 0.5f + interaction.vertical_offset,
            };
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const auto draw_icon  = [&](const ImVec2 icon_center, const ImU32 color) {
                if (icon == Icon::Translate) {
                    draw_list->AddLine(ImVec2{icon_center.x - 8.0f, icon_center.y + 7.0f}, ImVec2{icon_center.x + 7.0f, icon_center.y - 8.0f}, color, 1.7f);
                    draw_list->AddTriangleFilled(ImVec2{icon_center.x + 7.0f, icon_center.y - 8.0f}, ImVec2{icon_center.x + 2.0f, icon_center.y - 6.0f}, ImVec2{icon_center.x + 5.0f, icon_center.y - 3.0f}, color);
                    draw_list->AddLine(ImVec2{icon_center.x - 8.0f, icon_center.y + 7.0f}, ImVec2{icon_center.x + 6.0f, icon_center.y + 7.0f}, color, 1.7f);
                    draw_list->AddLine(ImVec2{icon_center.x - 8.0f, icon_center.y + 7.0f}, ImVec2{icon_center.x - 8.0f, icon_center.y - 6.0f}, color, 1.7f);
                } else if (icon == Icon::Rotate) {
                    draw_list->PathArcTo(icon_center, 8.0f, 0.25f, 5.5f, 24);
                    draw_list->PathStroke(color, ImDrawFlags_None, 1.7f);
                    draw_list->AddTriangleFilled(ImVec2{icon_center.x + 7.0f, icon_center.y - 5.0f}, ImVec2{icon_center.x + 9.0f, icon_center.y + 1.0f}, ImVec2{icon_center.x + 3.0f, icon_center.y - 1.0f}, color);
                } else if (icon == Icon::Scale) {
                    draw_list->AddLine(ImVec2{icon_center.x - 6.0f, icon_center.y + 6.0f}, ImVec2{icon_center.x + 6.0f, icon_center.y - 6.0f}, color, 1.7f);
                    draw_list->AddRectFilled(ImVec2{icon_center.x - 9.0f, icon_center.y + 3.0f}, ImVec2{icon_center.x - 3.0f, icon_center.y + 9.0f}, color, 1.0f);
                    draw_list->AddRectFilled(ImVec2{icon_center.x + 3.0f, icon_center.y - 9.0f}, ImVec2{icon_center.x + 9.0f, icon_center.y - 3.0f}, color, 1.0f);
                } else if (icon == Icon::Capture) {
                    draw_list->AddRect(ImVec2{icon_center.x - 9.0f, icon_center.y - 6.0f}, ImVec2{icon_center.x + 9.0f, icon_center.y + 7.0f}, color, 2.0f, ImDrawFlags_None, 1.6f);
                    draw_list->AddCircle(icon_center, 3.5f, color, 16, 1.5f);
                    draw_list->AddRectFilled(ImVec2{icon_center.x - 5.0f, icon_center.y - 9.0f}, ImVec2{icon_center.x + 1.0f, icon_center.y - 6.0f}, color, 1.0f);
                } else if (icon == Icon::Close) {
                    draw_list->AddLine(ImVec2{icon_center.x - 6.0f, icon_center.y - 6.0f}, ImVec2{icon_center.x + 6.0f, icon_center.y + 6.0f}, color, 1.6f);
                    draw_list->AddLine(ImVec2{icon_center.x + 6.0f, icon_center.y - 6.0f}, ImVec2{icon_center.x - 6.0f, icon_center.y + 6.0f}, color, 1.6f);
                } else if (icon == Icon::Play) {
                    draw_list->AddTriangleFilled(ImVec2{icon_center.x - 5.0f, icon_center.y - 7.0f}, ImVec2{icon_center.x + 7.0f, icon_center.y}, ImVec2{icon_center.x - 5.0f, icon_center.y + 7.0f}, color);
                } else if (icon == Icon::Pause) {
                    draw_list->AddRectFilled(ImVec2{icon_center.x - 6.0f, icon_center.y - 7.0f}, ImVec2{icon_center.x - 2.0f, icon_center.y + 7.0f}, color, 1.0f);
                    draw_list->AddRectFilled(ImVec2{icon_center.x + 2.0f, icon_center.y - 7.0f}, ImVec2{icon_center.x + 6.0f, icon_center.y + 7.0f}, color, 1.0f);
                } else if (icon == Icon::Step) {
                    draw_list->AddTriangleFilled(ImVec2{icon_center.x - 7.0f, icon_center.y - 7.0f}, ImVec2{icon_center.x + 4.0f, icon_center.y}, ImVec2{icon_center.x - 7.0f, icon_center.y + 7.0f}, color);
                    draw_list->AddLine(ImVec2{icon_center.x + 7.0f, icon_center.y - 7.0f}, ImVec2{icon_center.x + 7.0f, icon_center.y + 7.0f}, color, 1.8f);
                } else {
                    draw_list->PathArcTo(icon_center, 7.0f, 0.55f, 5.7f, 24);
                    draw_list->PathStroke(color, ImDrawFlags_None, 1.7f);
                    draw_list->AddTriangleFilled(ImVec2{icon_center.x + 6.0f, icon_center.y - 6.0f}, ImVec2{icon_center.x + 9.0f, icon_center.y - 1.0f}, ImVec2{icon_center.x + 3.0f, icon_center.y - 1.0f}, color);
                }
            };
            draw_icon(ImVec2{center.x + 1.0f, center.y + 1.0f}, interaction.shadow);
            draw_icon(center, interaction.color);
            if (text) {
                const ImVec2 text_position{center.x + 11.0f, center.y - text_size.y * 0.5f};
                draw_list->AddText(ImVec2{text_position.x + 1.0f, text_position.y + 1.0f}, interaction.shadow, text);
                draw_list->AddText(text_position, interaction.color, text);
            }
            if (selected) {
                const float item_center = (interaction.minimum.x + interaction.maximum.x) * 0.5f;
                draw_list->AddLine(ImVec2{item_center - 8.0f, interaction.maximum.y - 3.0f}, ImVec2{item_center + 8.0f, interaction.maximum.y - 3.0f}, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 1.0f}), 2.0f);
            }
            return interaction.clicked;
        }
    } // namespace

} // namespace spectra

namespace spectra {
    template <class Operation>
    bool EditorUi::apply_setup_edit(Operation&& operation, std::string_view success_message, const bool clear_drafts) {
        try {
            std::invoke(std::forward<Operation>(operation));
            this->controls.status       = success_message;
            this->controls.status_error = false;
            if (clear_drafts) {
                this->controls.parameter_drafts.clear();
                this->controls.pending_reset_systems.clear();
            }
            return true;
        } catch (const std::exception& error) {
            this->controls.status       = error.what();
            this->controls.status_error = true;
            return false;
        }
    }

    bool EditorUi::render_progress_visible() const noexcept {
        const std::optional<RenderProgress> progress = this->context.renderers.progress();
        return progress && progress->completed < progress->target;
    }

    bool EditorUi::pointer_over_interface(const ImVec2 position, const ImVec2 size, const bool show_axes) const noexcept {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (mouse.y <= position.y + top_strip_height + 5.0f) return true;
        if (show_axes && mouse.x >= position.x + size.x - 116.0f && mouse.y <= position.y + 126.0f) return true;
        if (!this->controls.expanded) return false;
        if (mouse.x <= position.x + 56.0f && mouse.y >= position.y + size.y * 0.5f - 74.0f && mouse.y <= position.y + size.y * 0.5f + 74.0f) return true;
        if (mouse.x >= position.x + size.x - 444.0f && mouse.y >= position.y + 52.0f) return true;
        return false;
    }

    void EditorUi::synchronize_transform() {
        const scene::Instance* instance = selected_instance(*this);
        if (!instance) {
            this->controls.transform_instance.reset();
            return;
        }
        if (this->controls.transform_interaction || this->controls.gizmo_using) return;
        if (this->controls.transform_instance == instance->id && this->controls.transform_revision == this->context.document.content.source.revision().number) return;
        std::array<float, 16> matrix = column_major(instance->transform.matrix);
        ImGuizmo::DecomposeMatrixToComponents(matrix.data(), this->controls.translation.data(), this->controls.rotation.data(), this->controls.scale.data());
        this->controls.transform_instance = instance->id;
        this->controls.transform_revision = this->context.document.content.source.revision().number;
    }

    void EditorUi::apply_transform() {
        std::array<float, 16> matrix{};
        ImGuizmo::RecomposeMatrixFromComponents(this->controls.translation.data(), this->controls.rotation.data(), this->controls.scale.data(), matrix.data());
        this->context.interaction.update_transform_edit(row_major_transform(matrix));
    }

    void EditorUi::transform_field(const char* label, std::array<float, 3>& value, const float speed) {
        ImGui::SetNextItemWidth(-1.0f);
        const bool changed = ImGui::DragFloat3(label, value.data(), speed, 0.0f, 0.0f, "%.3f");
        if (ImGui::IsItemActivated() && this->controls.transform_instance) {
            this->context.interaction.begin_transform_edit(*this->controls.transform_instance);
            this->controls.transform_interaction = true;
        }
        if (changed) this->apply_transform();
        if (ImGui::IsItemDeactivated() && this->controls.transform_interaction) {
            this->context.interaction.finish_transform_edit();
            this->controls.transform_interaction = false;
            this->controls.transform_revision    = 0;
        }
    }

    void EditorUi::handle_shortcuts(EditorActions& actions, const float aspect) {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !io.WantTextInput) this->controls.expanded = !this->controls.expanded;

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            actions.exit_application = true;
            return;
        }
        if (io.WantTextInput) return;

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) actions.open_scene_library = true;
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) actions.save_scene = true;
        if (io.KeyCtrl && io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_S, false) && !this->context.output.frozen_scene_export_in_progress())
            actions.export_frozen_scene = true;
        else if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
            actions.save_scene_as = true;
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false) && this->context.interaction.can_undo()) this->context.interaction.undo();
        if ((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) || (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))
            if (this->context.interaction.can_redo()) this->context.interaction.redo();
        const std::span<const RendererDescriptor> renderers = this->context.renderers.enabled_renderers();
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_1, false) && !renderers.empty()) this->context.renderers.activate(renderers[0].id);
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_2, false) && renderers.size() > 1) this->context.renderers.activate(renderers[1].id);
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) this->controls.gizmo_operation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) this->controls.gizmo_operation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) this->controls.gizmo_operation = ImGuizmo::SCALE;
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) this->context.interaction.frame_selection(aspect);
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) this->context.interaction.frame_scene(aspect);
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) this->context.interaction.view_axis({0.0f, 0.0f, 1.0f}, aspect);
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) this->context.interaction.view_axis({1.0f, 0.0f, 0.0f}, aspect);
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) this->context.interaction.view_axis({0.0f, 1.0f, 0.0f}, aspect);
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) this->context.interaction.view.source = this->context.interaction.view.source == CameraSource::Scene ? CameraSource::Viewport : CameraSource::Scene;
        if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false)) this->context.interaction.view.overlays_visible = !this->context.interaction.view.overlays_visible;
        if (this->context.dynamics.configuration.initialized && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            if (this->context.dynamics.running())
                this->context.dynamics.pause();
            else
                this->context.dynamics.start();
        }
    }

    void EditorUi::draw_orientation(const ImVec2 position, const ImVec2 size, const bool show_axes) {
        if (!show_axes) return;
        const ImVec2 center{position.x + size.x - 56.0f, position.y + 70.0f};
        const scene::CameraFrame frame = this->context.interaction.view.render_camera.frame();
        struct Axis {
            const char* label;
            math::Float3 world;
            ImVec4 color;
        };
        constexpr std::array axes{
            Axis{"X", {1.0f, 0.0f, 0.0f}, {0.95f, 0.28f, 0.24f, 1.0f}},
            Axis{"Y", {0.0f, 1.0f, 0.0f}, {0.30f, 0.83f, 0.38f, 1.0f}},
            Axis{"Z", {0.0f, 0.0f, 1.0f}, {0.29f, 0.50f, 0.96f, 1.0f}},
        };
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        for (const Axis& axis : axes) {
            const float x = axis.world.dot(frame.right);
            const float y = axis.world.dot(frame.up);
            const ImVec2 tip{center.x + x * 25.0f, center.y - y * 25.0f};
            ImGui::SetCursorScreenPos(ImVec2{tip.x - 11.0f, tip.y - 11.0f});
            ImGui::PushID(axis.label);
            const FlatButtonInteraction interaction = flat_button("##Axis", ImVec2{22.0f, 22.0f});
            if (interaction.clicked) this->context.interaction.view_axis(axis.world, size.x / size.y);
            ImGui::PopID();
            const ImVec2 rendered_tip{tip.x, tip.y + interaction.vertical_offset};
            const float radius = interaction.hovered ? 9.775f : 8.5f;
            const ImU32 shadow = ImGui::GetColorU32(ImVec4{0.0f, 0.0f, 0.0f, 0.72f});
            draw_list->AddLine(center, rendered_tip, shadow, 3.6f);
            draw_list->AddCircleFilled(rendered_tip, radius + 1.0f, shadow, 20);
            draw_list->AddLine(center, rendered_tip, ImGui::GetColorU32(axis.color), 2.0f);
            draw_list->AddCircleFilled(rendered_tip, radius, ImGui::GetColorU32(axis.color), 20);
            const ImVec2 text_size = ImGui::CalcTextSize(axis.label);
            const ImVec2 text_position{rendered_tip.x - text_size.x * 0.5f, rendered_tip.y - text_size.y * 0.5f};
            draw_list->AddText(ImVec2{text_position.x + 1.0f, text_position.y + 1.0f}, shadow, axis.label);
            draw_list->AddText(text_position, IM_COL32_WHITE, axis.label);
        }
    }

    void EditorUi::draw_gizmo(const ImVec2 minimum, const ImVec2 size, const bool blocked) {
        if (!this->controls.expanded) return;
        const scene::Instance* instance = selected_instance(*this);
        if (!instance) return;
        const std::optional<math::Bounds3> local_bounds = this->context.document.content.source.view().local_bounds(instance->id);
        if (!local_bounds) return;
        const math::Float3 pivot = local_bounds->center();
        math::Transform to_pivot{};
        to_pivot.matrix[3]  = pivot.x;
        to_pivot.matrix[7]  = pivot.y;
        to_pivot.matrix[11] = pivot.z;
        math::Transform from_pivot{};
        from_pivot.matrix[3]                 = -pivot.x;
        from_pivot.matrix[7]                 = -pivot.y;
        from_pivot.matrix[11]                = -pivot.z;
        std::array<float, 16> matrix         = column_major((instance->transform * to_pivot).matrix);
        const scene::CameraMatrices matrices = this->context.interaction.view.render_camera.matrices();
        std::array<float, 16> view           = column_major(matrices.view);
        std::array<float, 16> projection     = column_major(matrices.projection);
        ImGuizmo::SetOrthographic(std::holds_alternative<scene::OrthographicCameraData>(this->context.interaction.view.render_camera.data));
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(minimum.x, minimum.y, size.x, size.y);
        ImGuizmo::Enable(!blocked);
        const bool changed   = ImGuizmo::Manipulate(view.data(), projection.data(), this->controls.gizmo_operation, ImGuizmo::WORLD, matrix.data());
        const bool using_now = ImGuizmo::IsUsing();
        if (using_now && !this->controls.gizmo_using) this->context.interaction.begin_transform_edit(instance->id);
        if (changed) this->context.interaction.update_transform_edit(row_major_transform(matrix) * from_pivot);
        if (!using_now && this->controls.gizmo_using) {
            this->context.interaction.finish_transform_edit();
            this->controls.transform_revision = 0;
        }
        this->controls.gizmo_using = using_now;
        ImGuizmo::Enable(true);
    }

    void EditorUi::handle_viewport_input(const ImVec2 minimum, const ImVec2 size, const bool blocked) {
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 maximum{minimum.x + size.x, minimum.y + size.y};
        const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(minimum, maximum);
        if (!hovered || blocked || io.WantTextInput) {
            this->context.interaction.clear_hover();
            return;
        }
        if (this->context.interaction.view.source == CameraSource::Viewport && !ImGuizmo::IsUsing()) {
            if (io.MouseWheel != 0.0f) this->context.interaction.zoom_viewport_camera(io.MouseWheel);
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                if (io.KeyShift)
                    this->context.interaction.pan_viewport_camera(io.MouseDelta.x, io.MouseDelta.y, size.y);
                else
                    this->context.interaction.orbit_viewport_camera(io.MouseDelta.x, io.MouseDelta.y);
            }
        }
        const bool pick_surface = !ImGuizmo::IsOver() && !ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
        if (!pick_surface) {
            this->context.interaction.clear_hover();
            return;
        }
        const float x = std::clamp((io.MousePos.x - minimum.x) / size.x, 0.0f, 1.0f);
        const float y = std::clamp((io.MousePos.y - minimum.y) / size.y, 0.0f, 1.0f);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false))
            this->context.viewport.submit_pick(x, y, true, io.KeyShift);
        else
            this->context.viewport.submit_pick(x, y, false, false);
    }

    void EditorUi::draw_viewport(const ImVec2 position, const ImVec2 size, const bool show_axes) {
        ImGui::SetNextWindowPos(position);
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::Begin("##SpectraViewport", nullptr, flags);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddImage(static_cast<ImTextureID>(this->context.viewport.target.texture_id), position, ImVec2{position.x + size.x, position.y + size.y});
        ImGui::PushClipRect(position, ImVec2{position.x + size.x, position.y + size.y}, true);
        if (this->controls.expanded) draw_scene_camera(*this, position, size, *draw_list);
        const bool blocked = this->pointer_over_interface(position, size, show_axes);
        this->draw_gizmo(position, size, blocked);
        this->draw_orientation(position, size, show_axes);
        this->handle_viewport_input(position, size, blocked);
        ImGui::PopClipRect();
        ImGui::SetCursorScreenPos(ImVec2{position.x, position.y + size.y});
        ImGui::Dummy({});
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    float EditorUi::draw_render_status(const float right_edge) {
        const bool dynamic                                  = this->context.dynamics.configuration.initialized;
        const std::optional<RenderProgress> render_progress = this->context.renderers.progress();
        if (!dynamic && !render_progress) return right_edge;

        const float spacing    = ImGui::GetStyle().ItemSpacing.x;
        const auto button_size = [](const char* label) {
            const ImVec2 text_size = ImGui::CalcTextSize(label);
            const ImVec2 padding   = ImGui::GetStyle().FramePadding;
            return ImVec2{text_size.x + padding.x * 2.0f, 27.0f};
        };
        const auto draw_status_text = [](const std::string& text, const ImVec4 color = ImVec4{0.88f, 0.91f, 0.95f, 0.55f}) {
            const ImVec2 minimum   = ImGui::GetCursorScreenPos();
            const ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
            const ImVec2 text_position{minimum.x, minimum.y + (27.0f - text_size.y) * 0.5f};
            ImGui::GetWindowDrawList()->AddText(ImVec2{text_position.x + 1.0f, text_position.y + 1.0f}, ImGui::GetColorU32(ImVec4{0.0f, 0.0f, 0.0f, 0.55f}), text.c_str());
            ImGui::GetWindowDrawList()->AddText(text_position, ImGui::GetColorU32(color), text.c_str());
            ImGui::Dummy(ImVec2{text_size.x, 27.0f});
        };

        const dynamics::SimulationTimeline timeline = dynamic ? this->context.dynamics.timeline() : dynamics::SimulationTimeline{};
        const std::string status                    = dynamic ? std::format("step {}  ·  {:.3f} s", timeline.simulation_step, timeline.simulation_seconds) : std::format("{} / {} spp", render_progress->completed, render_progress->target);
        const bool simulation_playing               = dynamic && this->context.dynamics.running();
        const char* playback_label                  = dynamic ? simulation_playing ? "Pause" : "Play" : render_progress->paused ? "Resume" : "Pause";
        const char* secondary_label                 = dynamic ? "Step" : "Reset";
        const char* reset_label                     = "Reset";
        ImVec2 playback_size                        = button_size(playback_label);
        playback_size.x                             = std::max(button_size("Pause").x, button_size(dynamic ? "Play" : "Resume").x);
        const ImVec2 secondary_size                 = button_size(secondary_label);
        const ImVec2 reset_size                     = button_size(reset_label);
        const float status_width                    = ImGui::CalcTextSize(status.c_str()).x + playback_size.x + secondary_size.x + (dynamic ? reset_size.x + spacing : 0.0f) + spacing * 2.0f;
        const float status_start                    = right_edge - status_width - 8.0f;
        ImGui::SameLine(status_start);

        if (dynamic) {
            if (icon_button("##SimulationPlayback", playback_size, simulation_playing ? Icon::Pause : Icon::Play, playback_label, simulation_playing ? ImVec4{0.35f, 0.84f, 0.55f, 1.0f} : ImVec4{})) {
                if (simulation_playing)
                    this->context.dynamics.pause();
                else
                    this->context.dynamics.start();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(simulation_playing);
            if (icon_button("##SimulationStep", secondary_size, Icon::Step, secondary_label)) this->context.dynamics.step();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(simulation_playing);
            if (icon_button("##SimulationReset", reset_size, Icon::Reset, reset_label)) this->context.dynamics.reset();
            ImGui::EndDisabled();
            ImGui::SameLine();
            draw_status_text(status);
        } else {
            draw_status_text(status);
            ImGui::SameLine();
            if (icon_button("##PathPlayback", playback_size, render_progress->paused ? Icon::Play : Icon::Pause, playback_label, render_progress->paused ? ImVec4{0.91f, 0.72f, 0.29f, 1.0f} : ImVec4{})) this->context.renderers.set_paused(!render_progress->paused);
            ImGui::SameLine();
            if (icon_button("##PathReset", secondary_size, Icon::Reset, secondary_label)) this->context.renderers.reset();
        }
        return status_start;
    }

    void EditorUi::draw_top_strip(const ImVec2 position, const ImVec2 size, EditorActions& actions) {
        ImGui::SetNextWindowPos(ImVec2{position.x + 6.0f, position.y + 5.0f});
        ImGui::SetNextWindowSize(ImVec2{size.x - 12.0f, top_strip_height});
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6.0f, 4.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{4.0f, 4.0f});
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("##ApplicationStrip", nullptr, flags);
        if (this->render_progress_visible()) {
            const std::optional<RenderProgress> render_progress = this->context.renderers.progress();
            const float progress                                = static_cast<float>(render_progress->completed) / static_cast<float>(render_progress->target);
            const ImVec2 minimum                                = ImGui::GetWindowPos();
            const ImVec2 maximum{minimum.x + ImGui::GetWindowWidth(), minimum.y + ImGui::GetWindowHeight()};
            const float progress_x = minimum.x + (maximum.x - minimum.x) * std::clamp(progress, 0.0f, 1.0f);
            ImDrawList* draw_list  = ImGui::GetWindowDrawList();
            draw_list->PushClipRect(minimum, maximum, false);
            draw_list->AddRectFilled(minimum, ImVec2{progress_x, maximum.y}, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 0.07f}));
            if (progress_x > minimum.x) draw_list->AddLine(ImVec2{progress_x, minimum.y + 2.0f}, ImVec2{progress_x, maximum.y - 2.0f}, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 0.72f}), 1.0f);
            draw_list->PopClipRect();
        }

        std::string identity = this->context.document.content.path.filename().string();
        if (identity.empty()) identity = this->context.document.content.source.name;
        const float identity_width = std::clamp(ImGui::CalcTextSize(identity.c_str()).x + 24.0f, 96.0f, 240.0f);
        if (text_button("##SceneIdentity", identity.c_str(), ImVec2{identity_width, 27.0f})) ImGui::OpenPopup("##SceneMenu");
        const float source_end = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
        if (this->context.interaction.scene_modified()) {
            const ImVec2 minimum           = ImGui::GetItemRectMin();
            const ImVec2 maximum           = ImGui::GetItemRectMax();
            const float visible_text_width = std::min(ImGui::CalcTextSize(identity.c_str()).x, identity_width - 20.0f);
            const float text_right         = minimum.x + (identity_width + visible_text_width) * 0.5f;
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2{std::min(text_right + 7.0f, maximum.x - 4.0f), (minimum.y + maximum.y) * 0.5f}, 2.5f, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 1.0f}), 12);
        }
        if (ImGui::BeginPopup("##SceneMenu")) {
            if (ImGui::MenuItem("Scene Library...", "Ctrl+O")) actions.open_scene_library = true;
            if (ImGui::MenuItem("Open File...")) actions.open_scene_file = true;
            if (ImGui::MenuItem("Reload")) actions.reload_scene = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) actions.save_scene = true;
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) actions.save_scene_as = true;
            ImGui::BeginDisabled(this->context.output.frozen_scene_export_in_progress());
            if (ImGui::MenuItem("Export Frozen Scene...", "Ctrl+Alt+S")) actions.export_frozen_scene = true;
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::BeginMenu("Renderers")) {
                const bool rasterizer_enabled  = this->context.renderers.enabled(Rasterizer::descriptor.id);
                const bool path_tracer_enabled = this->context.renderers.enabled(PathTracer::descriptor.id);
                ImGui::BeginDisabled(rasterizer_enabled && !path_tracer_enabled);
                if (ImGui::MenuItem(Rasterizer::descriptor.name.data(), nullptr, rasterizer_enabled)) actions.toggle_renderer = std::string{Rasterizer::descriptor.id};
                ImGui::EndDisabled();
                ImGui::BeginDisabled(path_tracer_enabled && !rasterizer_enabled);
                if (ImGui::MenuItem(PathTracer::descriptor.name.data(), nullptr, path_tracer_enabled)) actions.toggle_renderer = std::string{PathTracer::descriptor.id};
                ImGui::EndDisabled();
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        const float center                                          = ImGui::GetWindowWidth() * 0.5f;
        const float right                                           = ImGui::GetWindowWidth();
        constexpr float mode_button_width                           = 80.0f;
        const std::span<const RendererDescriptor> enabled_renderers = this->context.renderers.enabled_renderers();
        const float mode_width                                      = mode_button_width * static_cast<float>(enabled_renderers.size()) + ImGui::GetStyle().ItemSpacing.x * static_cast<float>(enabled_renderers.size() - 1u);
        const float mode_start                                      = center - mode_width * 0.5f;
        ImGui::SameLine(mode_start);
        const RendererDescriptor active_renderer = this->context.renderers.active_descriptor();
        for (std::size_t index = 0; index < enabled_renderers.size(); ++index) {
            const RendererDescriptor renderer = enabled_renderers[index];
            const std::string identifier      = std::format("##Renderer{}", renderer.id);
            if (text_button(identifier.c_str(), renderer.name.data(), ImVec2{mode_button_width, 27.0f}, active_renderer == renderer)) this->context.renderers.activate(renderer.id);
            if (index + 1u < enabled_renderers.size()) ImGui::SameLine();
        }

        constexpr float exposure_width = 84.0f;
        constexpr float capture_width  = 28.0f;
        constexpr float edge_margin    = 4.0f;
        const float exposure_start     = right - edge_margin - capture_width - ImGui::GetStyle().ItemSpacing.x - exposure_width;
        const float status_start       = this->draw_render_status(exposure_start);
        ImGui::SameLine(exposure_start);
        const FlatButtonInteraction exposure = flat_button("##Exposure", ImVec2{exposure_width, 27.0f});
        if (exposure.active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) this->context.output.presenter.exposure = std::clamp(this->context.output.presenter.exposure + ImGui::GetIO().MouseDelta.x * 0.05f, -20.0f, 20.0f);
        if (exposure.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) this->context.output.presenter.exposure = 0.0f;
        const std::string exposure_text = std::format("{:+.2f} EV", this->context.output.presenter.exposure);
        const ImVec2 exposure_text_size = ImGui::CalcTextSize(exposure_text.c_str());
        const ImVec2 exposure_text_position{
            exposure.minimum.x + (exposure_width - exposure_text_size.x) * 0.5f,
            exposure.minimum.y + (27.0f - exposure_text_size.y) * 0.5f + exposure.vertical_offset,
        };
        ImGui::GetWindowDrawList()->AddText(ImVec2{exposure_text_position.x + 1.0f, exposure_text_position.y + 1.0f}, exposure.shadow, exposure_text.c_str());
        ImGui::GetWindowDrawList()->AddText(exposure_text_position, exposure.color, exposure_text.c_str());
        ImGui::SameLine();
        if (icon_button("##Capture", ImVec2{capture_width, 27.0f}, Icon::Capture)) ImGui::OpenPopup("##CaptureMenu");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Capture");
        if (ImGui::BeginPopup("##CaptureMenu")) {
            if (ImGui::MenuItem("Viewport PNG")) actions.capture_format = CaptureFormat::Png;
            if (ImGui::MenuItem("Linear EXR")) actions.capture_format = CaptureFormat::Exr;
            ImGui::EndPopup();
        }

        const float strip_offset_x      = ImGui::GetWindowPos().x - position.x;
        const float left_drag_start     = strip_offset_x + source_end + 4.0f;
        const float mode_client_start   = strip_offset_x + mode_start;
        const float mode_client_end     = mode_client_start + mode_width;
        const float right_drag_start    = mode_client_end + 4.0f;
        const float status_client_start = strip_offset_x + status_start;
        actions.window_drag_regions     = {{
            {left_drag_start, 0.0f, std::max(left_drag_start, mode_client_start - 4.0f), top_strip_height + 5.0f},
            {right_drag_start, 0.0f, std::max(right_drag_start, status_client_start - 4.0f), top_strip_height + 5.0f},
        }};
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void EditorUi::draw_transform_tools(const ImVec2 position, const ImVec2 size) {
        ImGui::SetNextWindowPos(ImVec2{position.x + 8.0f, position.y + size.y * 0.5f - 68.0f});
        ImGui::SetNextWindowSize(ImVec2{44.0f, 136.0f});
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{4.0f, 4.0f});
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("##TransformTools", nullptr, flags);
        const bool translating = this->controls.gizmo_operation == ImGuizmo::TRANSLATE;
        const bool rotating    = this->controls.gizmo_operation == ImGuizmo::ROTATE;
        const bool scaling     = this->controls.gizmo_operation == ImGuizmo::SCALE;
        if (icon_button("##Translate", ImVec2{36.0f, 38.0f}, Icon::Translate, nullptr, translating ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{}, translating, false, 0.55f)) this->controls.gizmo_operation = ImGuizmo::TRANSLATE;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translate  W");
        if (icon_button("##Rotate", ImVec2{36.0f, 38.0f}, Icon::Rotate, nullptr, rotating ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{}, rotating, false, 0.55f)) this->controls.gizmo_operation = ImGuizmo::ROTATE;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate  E");
        if (icon_button("##Scale", ImVec2{36.0f, 38.0f}, Icon::Scale, nullptr, scaling ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{}, scaling, false, 0.55f)) this->controls.gizmo_operation = ImGuizmo::SCALE;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale  R");
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorUi::draw_transform_hud(const ImVec2 position, const ImVec2 size) {
        const scene::Instance* instance = selected_instance(*this);
        if (!instance) return;
        this->synchronize_transform();
        ImGui::SetNextWindowPos(ImVec2{position.x + size.x - 260.0f, position.y + 52.0f});
        ImGui::SetNextWindowSize(ImVec2{248.0f, 214.0f});
        ImGui::SetNextWindowBgAlpha(0.91f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{12.0f, 10.0f});
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("##TransformHud", nullptr, flags);
        ImGui::TextUnformatted(instance->name.c_str());
        ImGui::Separator();
        ImGui::TextDisabled("POSITION");
        this->transform_field("##Position", this->controls.translation, 0.01f);
        ImGui::TextDisabled("ROTATION");
        this->transform_field("##Rotation", this->controls.rotation, 0.1f);
        ImGui::TextDisabled("SCALE");
        this->transform_field("##Scale", this->controls.scale, 0.01f);
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorUi::draw_simulation_hud(const ImVec2 position, const ImVec2 size) {
        std::string& status = this->controls.status;
        bool& status_error  = this->controls.status_error;
        if (this->controls.observed_dynamic_revision != this->context.document.content.source.revision().number && !ImGui::IsAnyItemActive()) {
            this->controls.observed_dynamic_revision = this->context.document.content.source.revision().number;
            this->controls.parameter_drafts.clear();
            this->controls.pending_reset_systems.clear();
            this->controls.clock_draft.reset();
            this->controls.seed_draft.reset();
        }
        ImGui::SetNextWindowPos(ImVec2{position.x + size.x - 432.0f, position.y + 52.0f});
        ImGui::SetNextWindowSize(ImVec2{420.0f, std::max(220.0f, size.y - 64.0f)});
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{12.0f, 10.0f});
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("##DynamicEditor", nullptr, flags);
        ImGui::TextUnformatted("Systems");
        ImGui::Separator();

        const std::span<const dynamics::ProviderDescriptor> providers = this->context.dynamics.available_providers();
        const std::optional<scene::DynamicSetup>& dynamic_setup       = this->context.dynamics.setup();
        struct ResourceChoice {
            scene::DynamicResourceKind kind{};
            std::uint64_t id{};
            std::string name{};
        };
        const auto resource_choices = [this](const dynamics::ResourceKind kind) {
            std::vector<ResourceChoice> choices{};
            if (kind == dynamics::ResourceKind::InstanceTransform || kind == dynamics::ResourceKind::DebugDraw)
                for (const scene::Instance& resource : this->context.document.content.source.resources.instances) choices.emplace_back(scene::DynamicResourceKind::Instance, resource.id.value, resource.name);
            else if (kind == dynamics::ResourceKind::TriangleMesh)
                for (const scene::Geometry& resource : this->context.document.content.source.resources.geometries) choices.emplace_back(scene::DynamicResourceKind::Geometry, resource.id.value, resource.name);
            else if (kind == dynamics::ResourceKind::ParticleSet)
                for (const scene::ParticleSet& resource : this->context.document.content.source.resources.particle_sets) choices.emplace_back(scene::DynamicResourceKind::ParticleSet, resource.id.value, resource.name);
            else if (kind == dynamics::ResourceKind::Volume)
                for (const scene::Volume& resource : this->context.document.content.source.resources.volumes) choices.emplace_back(scene::DynamicResourceKind::Volume, resource.id.value, resource.name);
            return choices;
        };

        if (dynamic_setup) {
            if (!this->controls.clock_draft) this->controls.clock_draft = dynamic_setup->clock;
            if (!this->controls.seed_draft) this->controls.seed_draft = dynamic_setup->seed;
            ImGui::TextDisabled("SYNCHRONIZED CLOCK");
            ImGui::SetNextItemWidth(128.0f);
            static_cast<void>(ImGui::InputDouble("Step seconds", &this->controls.clock_draft->step_seconds, 0.0, 0.0, "%.9f"));
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const scene::DynamicClock clock = *this->controls.clock_draft;
                this->apply_setup_edit([this, clock] { this->context.interaction.set_dynamic_clock(clock); }, "Dynamic clock updated");
                this->controls.clock_draft.reset();
                ImGui::End();
                ImGui::PopStyleVar();
                return;
            }
            ImGui::SetNextItemWidth(128.0f);
            static_cast<void>(ImGui::InputScalar("Start step", ImGuiDataType_U64, &this->controls.clock_draft->start_step));
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const scene::DynamicClock clock = *this->controls.clock_draft;
                this->apply_setup_edit([this, clock] { this->context.interaction.set_dynamic_clock(clock); }, "Dynamic clock updated");
                this->controls.clock_draft.reset();
                ImGui::End();
                ImGui::PopStyleVar();
                return;
            }
            bool finite = this->controls.clock_draft->end_step.has_value();
            if (ImGui::Checkbox("Finite range", &finite)) {
                this->controls.clock_draft->end_step  = finite ? std::optional{this->controls.clock_draft->start_step + 1} : std::nullopt;
                const scene::DynamicClock clock = *this->controls.clock_draft;
                this->apply_setup_edit([this, clock] { this->context.interaction.set_dynamic_clock(clock); }, "Dynamic clock updated");
                this->controls.clock_draft.reset();
                ImGui::End();
                ImGui::PopStyleVar();
                return;
            }
            if (this->controls.clock_draft->end_step) {
                ImGui::SetNextItemWidth(128.0f);
                static_cast<void>(ImGui::InputScalar("End step", ImGuiDataType_U64, &*this->controls.clock_draft->end_step));
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    const scene::DynamicClock clock = *this->controls.clock_draft;
                    this->apply_setup_edit([this, clock] { this->context.interaction.set_dynamic_clock(clock); }, "Dynamic clock updated");
                    this->controls.clock_draft.reset();
                    ImGui::End();
                    ImGui::PopStyleVar();
                    return;
                }
                if (ImGui::Checkbox("Loop", &this->controls.clock_draft->loop)) {
                    const scene::DynamicClock clock = *this->controls.clock_draft;
                    this->apply_setup_edit([this, clock] { this->context.interaction.set_dynamic_clock(clock); }, "Dynamic clock updated");
                    this->controls.clock_draft.reset();
                    ImGui::End();
                    ImGui::PopStyleVar();
                    return;
                }
            }
            ImGui::SetNextItemWidth(128.0f);
            static_cast<void>(ImGui::InputScalar("Seed", ImGuiDataType_U64, &*this->controls.seed_draft));
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const std::uint64_t seed = *this->controls.seed_draft;
                this->apply_setup_edit([this, seed] { this->context.interaction.set_dynamic_seed(seed); }, "Dynamic seed updated");
                this->controls.seed_draft.reset();
                ImGui::End();
                ImGui::PopStyleVar();
                return;
            }
            ImGui::Separator();
        }

        if (text_button("##AddDynamicSystem", "Add System", ImVec2{104.0f, 27.0f})) ImGui::OpenPopup("##AddDynamicSystemPopup");
        if (ImGui::BeginPopup("##AddDynamicSystemPopup")) {
            for (const dynamics::ProviderDescriptor& provider : providers)
                if (ImGui::MenuItem(provider.name.c_str())) {
                    this->controls.pending_system_provider = provider.id;
                    this->controls.pending_system_bindings.clear();
                    this->controls.pending_replacement_system.reset();
                    this->controls.open_system_configuration = true;
                    ImGui::CloseCurrentPopup();
                }
            ImGui::EndPopup();
        }
        bool system_changed{};
        if (this->controls.open_system_configuration) {
            ImGui::OpenPopup("Configure Dynamic System");
            this->controls.open_system_configuration = false;
        }
        if (ImGui::BeginPopupModal("Configure Dynamic System", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const dynamics::ProviderDescriptor* pending_provider{};
            const auto found = std::ranges::find(providers, this->controls.pending_system_provider, &dynamics::ProviderDescriptor::id);
            if (found != providers.end()) pending_provider = std::to_address(found);
            if (!pending_provider) {
                ImGui::TextUnformatted("The selected Provider is no longer available.");
                if (ImGui::MenuItem("Close")) {
                    this->controls.pending_system_provider.clear();
                    this->controls.pending_system_bindings.clear();
                    this->controls.pending_replacement_system.reset();
                    ImGui::CloseCurrentPopup();
                }
            } else {
                ImGui::TextUnformatted(pending_provider->name.c_str());
                ImGui::TextDisabled("%s@%u", pending_provider->interface_id.c_str(), pending_provider->interface_version);
                ImGui::Separator();
                bool complete = true;
                for (std::size_t port_index = 0; port_index < pending_provider->ports.size(); ++port_index) {
                    const dynamics::PortDescriptor& port      = pending_provider->ports[port_index];
                    const std::vector<ResourceChoice> choices = resource_choices(port.resource_kind);
                    std::vector<scene::DynamicPortBinding> bindings{};
                    for (const scene::DynamicPortBinding& binding : this->controls.pending_system_bindings)
                        if (binding.port_id == port.id) bindings.emplace_back(binding);
                    complete = complete && bindings.size() == 1;
                    ImGui::PushID(static_cast<int>(port_index));
                    ImGui::Text("%s", port.name.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", port.resource_kind == dynamics::ResourceKind::DebugDraw ? "Display anchor" : port.direction == dynamics::PortDirection::Input ? "Input" : "Output");
                    const char* preview = "Unbound";
                    if (!bindings.empty()) {
                        const auto choice = std::ranges::find_if(choices, [&bindings](const ResourceChoice& value) { return value.kind == bindings.front().resource_kind && value.id == bindings.front().resource_id; });
                        if (choice != choices.end()) preview = choice->name.c_str();
                    }
                    if (ImGui::BeginCombo("##Resource", preview)) {
                        for (const ResourceChoice& choice : choices)
                            if (ImGui::Selectable(choice.name.c_str(), !bindings.empty() && bindings.front().resource_kind == choice.kind && bindings.front().resource_id == choice.id)) {
                                std::erase_if(this->controls.pending_system_bindings, [&port](const scene::DynamicPortBinding& binding) { return binding.port_id == port.id; });
                                this->controls.pending_system_bindings.emplace_back(port.id, choice.kind, choice.id);
                            }
                        ImGui::EndCombo();
                    }
                    if (choices.empty()) ImGui::TextDisabled("No compatible Scene resources");
                    ImGui::PopID();
                }
                ImGui::Separator();
                ImGui::BeginDisabled(!complete);
                if (ImGui::MenuItem(this->controls.pending_replacement_system ? "Replace Provider" : "Add System")) {
                    const std::vector<scene::DynamicPortBinding> bindings = this->controls.pending_system_bindings;
                    const std::optional<std::size_t> replacement          = this->controls.pending_replacement_system;
                    if (this->apply_setup_edit(
                            [this, pending_provider, bindings, replacement] {
                                if (replacement)
                                    this->context.interaction.set_dynamic_system_provider(*replacement, *pending_provider, bindings);
                                else
                                    this->context.interaction.add_dynamic_system(*pending_provider, bindings);
                            },
                            replacement ? "Dynamic System Provider replaced" : "Dynamic System added")) {
                        this->controls.pending_system_provider.clear();
                        if (!replacement) this->controls.selected_dynamic_system = this->context.dynamics.setup()->systems.size() - 1;
                        this->controls.pending_replacement_system.reset();
                        system_changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndDisabled();
                if (ImGui::MenuItem("Cancel")) {
                    this->controls.pending_system_provider.clear();
                    this->controls.pending_system_bindings.clear();
                    this->controls.pending_replacement_system.reset();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        if (system_changed) {
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }
        if (!dynamic_setup || dynamic_setup->systems.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("No Dynamic Systems in this Scene");
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }

        this->controls.selected_dynamic_system = std::min(this->controls.selected_dynamic_system, dynamic_setup->systems.size() - 1);
        if (ImGui::BeginCombo("##DynamicSystem", dynamic_setup->systems[this->controls.selected_dynamic_system].name.c_str())) {
            for (std::size_t index = 0; index < dynamic_setup->systems.size(); ++index)
                if (ImGui::Selectable(dynamic_setup->systems[index].name.c_str(), index == this->controls.selected_dynamic_system)) {
                    this->controls.selected_dynamic_system = index;
                    this->controls.parameter_drafts.clear();
                    this->controls.pending_reset_systems.clear();
                }
            ImGui::EndCombo();
        }
        const scene::DynamicSystem& scene_system = dynamic_setup->systems[this->controls.selected_dynamic_system];
        bool enabled                             = scene_system.enabled;
        if (ImGui::Checkbox("Enabled", &enabled)) {
            this->apply_setup_edit([this, enabled] { this->context.interaction.set_dynamic_system_enabled(this->controls.selected_dynamic_system, enabled); }, enabled ? "Dynamic System enabled" : "Dynamic System disabled");
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }
        ImGui::SameLine();
        bool visible = scene_system.visible;
        if (ImGui::Checkbox("Visible", &visible)) {
            this->apply_setup_edit([this, visible] { this->context.interaction.set_dynamic_system_visible(this->controls.selected_dynamic_system, visible); }, visible ? "Dynamic System shown" : "Dynamic System hidden");
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }
        ImGui::SameLine();
        if (text_button("##RemoveDynamicSystem", "Remove", ImVec2{76.0f, 27.0f}, false, true)) ImGui::OpenPopup("##ConfirmRemoveSystem");
        if (ImGui::BeginPopup("##ConfirmRemoveSystem")) {
            ImGui::Text("Remove %s?", scene_system.name.c_str());
            if (ImGui::MenuItem("Remove")) {
                this->apply_setup_edit([this] { this->context.interaction.remove_dynamic_system(this->controls.selected_dynamic_system); }, "Dynamic System removed");
                ImGui::EndPopup();
                ImGui::End();
                ImGui::PopStyleVar();
                return;
            }
            if (ImGui::MenuItem("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        const dynamics::ProviderDescriptor* provider{};
        const auto found_provider = std::ranges::find(providers, scene_system.provider_id, &dynamics::ProviderDescriptor::id);
        if (found_provider != providers.end()) provider = std::to_address(found_provider);
        if (ImGui::BeginCombo("##SystemProvider", provider ? provider->name.c_str() : scene_system.provider_id.c_str())) {
            for (const dynamics::ProviderDescriptor& replacement : providers)
                if ((!provider || (replacement.interface_id == provider->interface_id && replacement.interface_version == provider->interface_version)) && ImGui::Selectable(replacement.name.c_str(), replacement.id == scene_system.provider_id) && replacement.id != scene_system.provider_id) {
                    this->controls.pending_system_provider = replacement.id;
                    this->controls.pending_system_bindings.clear();
                    this->controls.pending_replacement_system = this->controls.selected_dynamic_system;
                    this->controls.open_system_configuration  = true;
                }
            ImGui::EndCombo();
        }
        if (provider)
            ImGui::TextDisabled("%s@%u  ·  %s", provider->interface_id.c_str(), provider->interface_version, provider->id.c_str());
        else
            ImGui::TextColored(ImVec4{0.94f, 0.35f, 0.35f, 1.0f}, "Missing Provider: %s", scene_system.provider_id.c_str());

        if (provider) {
            ImGui::Separator();
            ImGui::TextDisabled("PORT BINDINGS");
            for (std::size_t port_index = 0; port_index < provider->ports.size(); ++port_index) {
                const dynamics::PortDescriptor& port = provider->ports[port_index];
                ImGui::PushID(static_cast<int>(port_index));
                ImGui::Text("%s", port.name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%s / %s", port.resource_kind == dynamics::ResourceKind::DebugDraw ? "Display anchor" : port.direction == dynamics::PortDirection::Input ? "Input" : "Output", port.memory_domain == dynamics::MemoryDomain::Host ? "Host" : "CUDA External");
                const std::vector<ResourceChoice> choices = resource_choices(port.resource_kind);
                const auto binding                        = std::ranges::find(scene_system.bindings, port.id, &scene::DynamicPortBinding::port_id);
                const auto choice_name                    = [&choices](const scene::DynamicPortBinding& binding) -> const char* {
                    const auto choice = std::ranges::find_if(choices, [&binding](const ResourceChoice& item) { return item.kind == binding.resource_kind && item.id == binding.resource_id; });
                    return choice == choices.end() ? "Unknown Resource" : choice->name.c_str();
                };
                const char* preview = binding == scene_system.bindings.end() ? "Unbound" : choice_name(*binding);
                if (ImGui::BeginCombo("##Resource", preview)) {
                    for (const ResourceChoice& choice : choices)
                        if (ImGui::Selectable(choice.name.c_str(), binding != scene_system.bindings.end() && binding->resource_kind == choice.kind && binding->resource_id == choice.id)) {
                            scene::DynamicPortBinding replacement{port.id, choice.kind, choice.id};
                            this->apply_setup_edit([this, port_id = port.id, replacement = std::move(replacement)]() mutable { this->context.interaction.set_dynamic_port_binding(this->controls.selected_dynamic_system, std::move(port_id), std::move(replacement)); }, "Port binding updated");
                            ImGui::EndCombo();
                            ImGui::PopID();
                            ImGui::End();
                            ImGui::PopStyleVar();
                            return;
                        }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }

            if (!provider->parameters.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("PARAMETERS");
            }
            const std::string system_key = scene_system.id.value;
            const auto parameter_values  = [this, provider, &scene_system, &system_key](const bool include_reset_drafts) {
                std::vector<scene::DynamicParameterSetting> values{};
                values.reserve(provider->parameters.size());
                for (const dynamics::ParameterDescriptor& descriptor : provider->parameters) {
                    const auto stored                  = std::ranges::find(scene_system.parameters, descriptor.id, &scene::DynamicParameterSetting::parameter_id);
                    scene::DynamicParameterValue value = stored == scene_system.parameters.end() ? descriptor.value : stored->value;
                    const auto draft                   = this->controls.parameter_drafts.find(std::format("{}/{}", system_key, descriptor.id));
                    if (draft != this->controls.parameter_drafts.end() && (include_reset_drafts || descriptor.application_mode == dynamics::ParameterApplication::Live)) value = draft->second;
                    values.emplace_back(descriptor.id, value);
                }
                return values;
            };
            for (std::size_t parameter_index = 0; parameter_index < provider->parameters.size(); ++parameter_index) {
                const dynamics::ParameterDescriptor& parameter = provider->parameters[parameter_index];
                const auto configured                          = std::ranges::find(scene_system.parameters, parameter.id, &scene::DynamicParameterSetting::parameter_id);
                const scene::DynamicParameterValue stored      = configured == scene_system.parameters.end() ? parameter.value : configured->value;
                const std::string key                          = std::format("{}/{}", system_key, parameter.id);
                scene::DynamicParameterValue& value            = this->controls.parameter_drafts.try_emplace(key, stored).first->second;
                ImGui::PushID(static_cast<int>(parameter_index));
                ImGui::Text("%s", parameter.name.c_str());
                if (!parameter.unit.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", parameter.unit.c_str());
                }
                bool changed{};
                if (parameter.value.kind == scene::DynamicParameterKind::Boolean) {
                    bool selected = value.integer != 0;
                    changed       = ImGui::Checkbox("##Value", &selected);
                    if (changed) value.integer = selected ? 1 : 0;
                } else if (parameter.value.kind == scene::DynamicParameterKind::Integer)
                    changed = ImGui::SliderScalar("##Value", ImGuiDataType_S64, &value.integer, &parameter.minimum.integer, &parameter.maximum.integer, "%lld");
                else if (parameter.value.kind == scene::DynamicParameterKind::Float)
                    changed = ImGui::SliderScalar("##Value", ImGuiDataType_Double, value.floating.data(), parameter.minimum.floating.data(), parameter.maximum.floating.data(), "%.6g");
                else if (parameter.value.kind == scene::DynamicParameterKind::Float3)
                    changed = ImGui::DragScalarN("##Value", ImGuiDataType_Double, value.floating.data(), 3, 0.01f, parameter.minimum.floating.data(), parameter.maximum.floating.data(), "%.5g");
                else {
                    const char* preview = value.integer >= 0 && static_cast<std::size_t>(value.integer) < parameter.enumerators.size() ? parameter.enumerators[value.integer].c_str() : "";
                    if (ImGui::BeginCombo("##Value", preview)) {
                        for (std::size_t option = 0; option < parameter.enumerators.size(); ++option)
                            if (ImGui::Selectable(parameter.enumerators[option].c_str(), value.integer == static_cast<std::int64_t>(option))) {
                                value.integer = static_cast<std::int64_t>(option);
                                changed       = true;
                            }
                        ImGui::EndCombo();
                    }
                }
                if (parameter.application_mode == dynamics::ParameterApplication::ResetRequired) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("reset");
                    if (changed) this->controls.pending_reset_systems.insert(system_key);
                } else if ((changed && !ImGui::IsItemActive()) || ImGui::IsItemDeactivatedAfterEdit()) {
                    std::vector<scene::DynamicParameterSetting> parameters = parameter_values(false);
                    this->apply_setup_edit([this, parameters = std::move(parameters)]() mutable { this->context.interaction.set_dynamic_system_parameters(this->controls.selected_dynamic_system, std::move(parameters), false); }, "Parameter applied", false);
                    this->controls.observed_dynamic_revision = this->context.document.content.source.revision().number;
                }
                ImGui::PopID();
            }
            if (this->controls.pending_reset_systems.contains(system_key))
                if (text_button("##ApplySystemReset", "Apply & Reset", ImVec2{124.0f, 27.0f})) {
                    std::vector<scene::DynamicParameterSetting> parameters = parameter_values(true);
                    this->apply_setup_edit([this, parameters = std::move(parameters)]() mutable { this->context.interaction.set_dynamic_system_parameters(this->controls.selected_dynamic_system, std::move(parameters), true); }, "Parameters applied and Dynamic Setup reset");
                    ImGui::End();
                    ImGui::PopStyleVar();
                    return;
                }
        }

        if (this->context.dynamics.configuration.initialized) {
            std::vector<dynamics::SystemStatus>& system_statuses = this->context.dynamics.system_statuses();
            dynamics::SystemStatus& system_status                = system_statuses[this->controls.selected_dynamic_system];
            ImGui::Separator();
            const dynamics::SimulationTimeline timeline = this->context.dynamics.timeline();
            if (dynamic_setup->clock.end_step) {
                if (!this->controls.simulation_step_edit) this->controls.simulation_step_edit = timeline.simulation_step;
                const std::uint64_t minimum = dynamic_setup->clock.start_step;
                const std::uint64_t maximum = *dynamic_setup->clock.end_step;
                ImGui::SetNextItemWidth(-1.0f);
                static_cast<void>(ImGui::SliderScalar("##SimulationStep", ImGuiDataType_U64, &*this->controls.simulation_step_edit, &minimum, &maximum, "%llu"));
                if (ImGui::IsItemDeactivatedAfterEdit()) this->context.dynamics.seek(*this->controls.simulation_step_edit);
                if (timeline.simulation_step == *this->controls.simulation_step_edit && !ImGui::IsItemActive()) this->controls.simulation_step_edit.reset();
            }
            ImGui::TextDisabled("%s  ·  %s", scene_system.enabled ? "Enabled" : "Disabled", scene_system.visible ? "Visible" : "Hidden");
            ImGui::TextDisabled("COMPARISON");
            if (ImGui::BeginTable("##DynamicComparison", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("System");
                ImGui::TableSetupColumn("Steps");
                ImGui::TableSetupColumn("Time");
                ImGui::TableSetupColumn("Last batch");
                ImGui::TableSetupColumn("Mean / step");
                ImGui::TableHeadersRow();
                for (const dynamics::SystemStatus& system : system_statuses) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(system.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%llu", system.completed_steps);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.4f s", system.simulation_seconds);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f ms", system.last_batch_milliseconds);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f ms", system.average_step_milliseconds);
                }
                ImGui::EndTable();
            }
            if (scene_system.enabled && !system_status.telemetry_values.empty()) {
                ImGui::TextDisabled("TELEMETRY");
                for (const dynamics::TelemetryValue& telemetry : system_status.telemetry_values) {
                    ImGui::Text("%s", telemetry.name.c_str());
                    ImGui::SameLine(ImGui::GetWindowWidth() - 118.0f);
                    ImGui::Text("%.4g %s", telemetry.value, telemetry.unit.c_str());
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorUi::draw_status_toast(const ImVec2 position, const ImVec2 size) {
        std::string& status = this->controls.status;
        bool& status_error  = this->controls.status_error;
        if (status != this->controls.observed_status || status_error != this->controls.observed_status_error) {
            this->controls.observed_status       = status;
            this->controls.observed_status_error = status_error;
            this->controls.status_since          = ImGui::GetTime();
        }
        if (status.empty()) return;
        const double age = ImGui::GetTime() - this->controls.status_since;
        if (!status_error && age > 3.0) {
            status.clear();
            this->controls.observed_status.clear();
            return;
        }
        const float alpha      = status_error ? 1.0f : std::clamp(static_cast<float>((3.0 - age) / 0.35), 0.0f, 1.0f);
        const ImVec2 text_size = ImGui::CalcTextSize(status.c_str());
        const float width      = std::min(size.x - 24.0f, text_size.x + (status_error ? 54.0f : 30.0f));
        ImGui::SetNextWindowPos(ImVec2{position.x + (size.x - width) * 0.5f, position.y + 18.0f});
        ImGui::SetNextWindowSize(ImVec2{width, 38.0f});
        ImGui::SetNextWindowBgAlpha((status_error ? 0.96f : 0.88f) * alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 8.0f});
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("##StatusToast", nullptr, flags);
        ImGui::TextColored(status_error ? ImVec4{1.0f, 0.38f, 0.33f, 1.0f} : ImVec4{0.55f, 0.94f, 0.80f, 1.0f}, "%s", status.c_str());
        if (status_error) {
            ImGui::SameLine(ImGui::GetWindowWidth() - 26.0f);
            if (icon_button("##DismissError", ImVec2{18.0f, 18.0f}, Icon::Close, nullptr, {}, false, true)) {
                status.clear();
                this->controls.observed_status.clear();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    EditorActions EditorUi::draw_scene_library(const std::span<const SceneLibraryEntry> scenes, const std::span<const SceneLibraryProblem> problems, const bool active_scene) {
        EditorActions actions{};
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 position         = viewport->Pos;
        const ImVec2 size             = viewport->Size;
        if (!this->controls.selected_scene || std::ranges::find(scenes, *this->controls.selected_scene, [](const SceneLibraryEntry& entry) { return entry.summary.scene_path; }) == scenes.end()) this->controls.selected_scene = scenes.empty() ? std::nullopt : std::optional{scenes.front().summary.scene_path};

        const ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) actions.refresh_scene_library = true;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                if (active_scene)
                    actions.close_scene_library = true;
                else
                    actions.exit_application = true;
            }
            if (!scenes.empty() && (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false) || ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))) {
                const auto selected = std::ranges::find(scenes, *this->controls.selected_scene, [](const SceneLibraryEntry& entry) { return entry.summary.scene_path; });
                std::size_t index   = selected == scenes.end() ? 0 : static_cast<std::size_t>(selected - scenes.begin());
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
                    index = index == 0 ? scenes.size() - 1 : index - 1;
                else
                    index = (index + 1) % scenes.size();
                this->controls.selected_scene = scenes[index].summary.scene_path;
            }
            if (this->controls.selected_scene && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) actions.selected_scene_path = this->controls.selected_scene;
        }

        ImGui::SetNextWindowPos(position);
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.018f, 0.022f, 0.030f, 1.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::Begin("##SceneLibrary", nullptr, flags);

        constexpr float header_height = 48.0f;
        ImGui::SetCursorScreenPos(ImVec2{position.x + 18.0f, position.y + 10.0f});
        ImGui::TextColored(ImVec4{0.88f, 0.91f, 0.95f, 0.92f}, "Spectra");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{0.88f, 0.91f, 0.95f, 0.38f}, "/ Scene Library");
        const float open_width    = 94.0f;
        const float refresh_width = 72.0f;
        ImGui::SetCursorScreenPos(ImVec2{position.x + size.x - open_width - refresh_width - 34.0f, position.y + 6.0f});
        if (text_button("##RefreshSceneLibrary", "Refresh", ImVec2{refresh_width, 32.0f})) actions.refresh_scene_library = true;
        ImGui::SameLine();
        if (text_button("##OpenSceneFile", "Open File...", ImVec2{open_width, 32.0f})) actions.open_scene_file = true;
        actions.window_drag_regions = {{{148.0f, 0.0f, std::max(148.0f, size.x - open_width - refresh_width - 42.0f), header_height}, {0.0f, 0.0f, 0.0f, 0.0f}}};

        const float content_width = std::min(980.0f, size.x - 48.0f);
        const float content_x     = position.x + (size.x - content_width) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2{content_x, position.y + std::max(78.0f, size.y * 0.14f)});
        ImGui::TextColored(ImVec4{0.88f, 0.91f, 0.95f, 0.92f}, "Scenes");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{0.88f, 0.91f, 0.95f, 0.38f}, "%zu detected", scenes.size());
        ImGui::SetCursorScreenPos(ImVec2{content_x, ImGui::GetCursorScreenPos().y + 12.0f});
        ImGui::TextColored(ImVec4{0.88f, 0.91f, 0.95f, 0.34f}, "NAME");
        ImGui::SameLine(content_x - position.x + content_width * 0.38f);
        ImGui::TextColored(ImVec4{0.88f, 0.91f, 0.95f, 0.34f}, "TYPE");
        ImGui::SameLine(content_x - position.x + content_width * 0.50f);
        ImGui::TextColored(ImVec4{0.88f, 0.91f, 0.95f, 0.34f}, "PROVIDERS");
        ImGui::SameLine(content_x - position.x + content_width * 0.80f);
        ImGui::TextColored(ImVec4{0.88f, 0.91f, 0.95f, 0.34f}, "SOURCE");
        ImGui::SameLine(content_x - position.x + content_width * 0.92f);
        ImGui::TextColored(ImVec4{0.88f, 0.91f, 0.95f, 0.34f}, "STATUS");

        const float row_start      = ImGui::GetCursorScreenPos().y + 8.0f;
        constexpr float row_height = 44.0f;
        ImDrawList* draw_list      = ImGui::GetWindowDrawList();
        for (std::size_t index = 0; index != scenes.size(); ++index) {
            const SceneLibraryEntry& entry = scenes[index];
            const ImVec2 minimum{content_x, row_start + static_cast<float>(index) * row_height};
            const ImVec2 maximum{content_x + content_width, minimum.y + row_height};
            ImGui::SetCursorScreenPos(minimum);
            ImGui::PushID(static_cast<int>(index));
            const bool clicked  = ImGui::InvisibleButton("##Scene", ImVec2{content_width, row_height});
            const bool hovered  = ImGui::IsItemHovered();
            const bool selected = this->controls.selected_scene == entry.summary.scene_path;
            if (clicked) this->controls.selected_scene = entry.summary.scene_path;
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) actions.selected_scene_path = entry.summary.scene_path;
            if (selected) {
                draw_list->AddRectFilled(minimum, maximum, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 0.08f}));
                draw_list->AddLine(ImVec2{minimum.x, minimum.y + 7.0f}, ImVec2{minimum.x, maximum.y - 7.0f}, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 1.0f}), 2.0f);
            } else if (hovered)
                draw_list->AddRectFilled(minimum, maximum, ImGui::GetColorU32(ImVec4{0.88f, 0.91f, 0.95f, 0.035f}));
            draw_list->AddLine(ImVec2{minimum.x, maximum.y}, maximum, ImGui::GetColorU32(ImVec4{0.88f, 0.91f, 0.95f, 0.08f}), 1.0f);

            std::string providers{};
            for (const std::string& provider : entry.summary.provider_ids) {
                if (!providers.empty()) providers += ", ";
                providers += provider;
            }
            const std::string name = entry.summary.name.empty() ? entry.summary.scene_path.stem().string() : entry.summary.name;
            const ImU32 primary    = ImGui::GetColorU32(selected ? ImVec4{0.45f, 0.88f, 0.96f, 1.0f} : ImVec4{0.88f, 0.91f, 0.95f, hovered ? 0.96f : 0.76f});
            const ImU32 secondary  = ImGui::GetColorU32(ImVec4{0.88f, 0.91f, 0.95f, hovered ? 0.62f : 0.42f});
            const float text_y     = minimum.y + (row_height - ImGui::GetTextLineHeight()) * 0.5f;
            draw_list->AddText(ImVec2{minimum.x + 14.0f, text_y}, primary, name.c_str());
            draw_list->AddText(ImVec2{minimum.x + content_width * 0.38f, text_y}, entry.summary.has_dynamics ? ImGui::GetColorU32(ImVec4{0.35f, 0.84f, 0.55f, 0.78f}) : secondary, entry.summary.has_dynamics ? "Dynamic" : "Static");
            draw_list->AddText(ImVec2{minimum.x + content_width * 0.50f, text_y}, secondary, providers.empty() ? "—" : providers.c_str());
            const std::string source = entry.root_path.filename().string();
            draw_list->AddText(ImVec2{minimum.x + content_width * 0.80f, text_y}, secondary, source.c_str());
            draw_list->AddText(ImVec2{minimum.x + content_width * 0.92f, text_y}, ImGui::GetColorU32(ImVec4{0.35f, 0.84f, 0.55f, 0.78f}), "Ready");
            if (hovered) ImGui::SetTooltip("%s", entry.summary.scene_path.string().c_str());
            ImGui::PopID();
        }

        if (!problems.empty()) {
            const float problems_y = row_start + static_cast<float>(scenes.size()) * row_height + 34.0f;
            ImGui::SetCursorScreenPos(ImVec2{content_x, problems_y});
            ImGui::TextColored(ImVec4{0.94f, 0.35f, 0.35f, 0.92f}, "Problems");
            for (const SceneLibraryProblem& problem : problems) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
                ImGui::TextColored(ImVec4{0.88f, 0.91f, 0.95f, 0.62f}, "%s", problem.scene_path.string().c_str());
                ImGui::SameLine();
                ImGui::TextColored(ImVec4{0.94f, 0.35f, 0.35f, 0.72f}, "%s", problem.error_message.c_str());
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        this->draw_status_toast(position, size);
        return actions;
    }

    EditorActions EditorUi::draw_editor_ui() {
        ImGuizmo::BeginFrame();
        EditorActions actions{};
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 position         = viewport->Pos;
        const ImVec2 size             = viewport->Size;
        const float aspect            = size.x / size.y;
        const ImGuiIO& io             = ImGui::GetIO();
        actions.show_axes             = ImGui::IsKeyDown(ImGuiKey_G) && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper && !io.WantTextInput;

        this->synchronize_transform();
        this->handle_shortcuts(actions, aspect);
        this->draw_viewport(position, size, actions.show_axes);
        this->draw_top_strip(position, size, actions);
        if (this->controls.expanded && selected_instance(*this)) {
            this->draw_transform_tools(position, size);
            this->draw_transform_hud(position, size);
        }
        if (this->controls.expanded) this->draw_simulation_hud(position, size);
        this->draw_status_toast(position, size);
        return actions;
    }
} // namespace spectra
