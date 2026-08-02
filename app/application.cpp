module;

#include <Windows.h>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <exr.h>
#include <glaze/glaze.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>

module spectra.application;

import spectra.app.ui;
import spectra;
import spectra.render;
import spectra.scene.dynamics;
import spectra.scene;
import spectra.workspace;
import std;
import vulkan;

namespace spectra::app {
    namespace {
        struct ExrChannelSource {
            std::string_view name;
            exr_pixel_type type;
            const std::byte* data;
            std::size_t pixel_stride;
            std::size_t component_offset;
        };

        struct ExrPart {
            exr_part value{};

            ~ExrPart() {
                exr_part_free(nullptr, &this->value);
            }
        };

        struct ExrWriter {
            exr_writer* value{};

            ~ExrWriter() {
                exr_writer_destroy(this->value);
            }
        };

        void check_exr(const exr_result result, const std::string_view operation) {
            if (!EXR_OK(result)) throw std::runtime_error(std::format("TinyEXR failed to {}: {}", operation, exr_result_string(result)));
        }

        void write_color_space(exr_header& header, const scene::SpectrumColorSpace color_space) {
            header.has_chromaticities = 1;
            const char* name{};
            switch (color_space) {
            case scene::SpectrumColorSpace::Srgb:
                std::ranges::copy(std::array{0.64f, 0.33f, 0.30f, 0.60f, 0.15f, 0.06f, 0.3127f, 0.3290f}, header.chromaticities);
                name = "Linear sRGB";
                break;
            case scene::SpectrumColorSpace::Rec2020:
                std::ranges::copy(std::array{0.708f, 0.292f, 0.170f, 0.797f, 0.131f, 0.046f, 0.3127f, 0.3290f}, header.chromaticities);
                name = "Linear Rec.2020";
                break;
            case scene::SpectrumColorSpace::Aces2065_1:
                std::ranges::copy(std::array{0.7347f, 0.2653f, 0.0f, 1.0f, 0.0001f, -0.0770f, 0.32168f, 0.33767f}, header.chromaticities);
                name = "ACES2065-1";
                break;
            }
            check_exr(exr_header_set_string_attribute(nullptr, &header, "spectra:workingColorSpace", name), "set the working color space");
            check_exr(exr_header_set_string_attribute(nullptr, &header, "spectra:encoding", "scene-linear"), "set the image encoding");
        }

        void write_exr(const std::filesystem::path& path, const std::uint32_t width, const std::uint32_t height, const scene::SpectrumColorSpace color_space, const std::span<const ExrChannelSource> channels, const std::optional<std::string_view> gbuffer_coordinates = std::nullopt, const std::optional<std::int32_t> accumulated_samples = std::nullopt) {
            std::vector<ExrChannelSource> sorted_channels{channels.begin(), channels.end()};
            std::ranges::sort(sorted_channels, {}, &ExrChannelSource::name);

            ExrPart part{};
            exr_header& header         = part.value.header;
            header.part_type           = EXR_PART_SCANLINE;
            header.compression         = EXR_COMPRESSION_ZIP;
            header.line_order          = EXR_LINEORDER_INCREASING_Y;
            header.data_window         = {0, 0, static_cast<std::int32_t>(width - 1u), static_cast<std::int32_t>(height - 1u)};
            header.display_window      = header.data_window;
            header.pixel_aspect_ratio  = 1.0f;
            header.screen_window_width = 1.0f;
            header.num_channels        = static_cast<std::int32_t>(sorted_channels.size());
            header.channels            = static_cast<exr_channel*>(std::calloc(sorted_channels.size(), sizeof(exr_channel)));
            if (!header.channels) throw std::bad_alloc{};
            for (std::size_t index = 0; index != sorted_channels.size(); ++index) {
                const ExrChannelSource& source = sorted_channels[index];
                std::memcpy(header.channels[index].name, source.name.data(), source.name.size());
                header.channels[index].name[source.name.size()] = '\0';
                header.channels[index].pixel_type               = source.type;
                header.channels[index].x_sampling               = 1;
                header.channels[index].y_sampling               = 1;
            }
            write_color_space(header, color_space);
            if (gbuffer_coordinates) check_exr(exr_header_set_string_attribute(nullptr, &header, "spectra:gbufferCoordinates", gbuffer_coordinates->data()), "set the GBuffer coordinate space");
            if (accumulated_samples) check_exr(exr_header_set_attribute(nullptr, &header, "spectra:accumulatedSamples", "int", &*accumulated_samples, sizeof(*accumulated_samples)), "set the accumulated sample count");

            ExrWriter writer{};
            check_exr(exr_writer_create(nullptr, &writer.value), "create the writer");
            check_exr(exr_writer_add_part(writer.value, &header, nullptr), "add the image part");
            const std::string filename = path.string();
            check_exr(exr_writer_begin_stream_file(writer.value, filename.c_str(), EXR_COMPRESSION_ZIP), "open the output file");

            constexpr std::uint32_t lines_per_block = 16;
            const std::size_t block_capacity        = static_cast<std::size_t>(width) * lines_per_block;
            std::vector<std::uint32_t> block_storage(sorted_channels.size() * block_capacity);
            std::vector<const void*> channel_rows(sorted_channels.size());
            for (std::uint32_t y = 0; y < height; y += lines_per_block) {
                const std::size_t block_size = static_cast<std::size_t>(width) * std::min(lines_per_block, height - y);
                for (std::size_t channel_index = 0; channel_index != sorted_channels.size(); ++channel_index) {
                    const ExrChannelSource& channel = sorted_channels[channel_index];
                    const std::byte* source         = channel.data + static_cast<std::size_t>(y) * width * channel.pixel_stride + channel.component_offset;
                    std::uint32_t* destination      = block_storage.data() + channel_index * block_capacity;
                    for (std::size_t pixel = 0; pixel != block_size; ++pixel) std::memcpy(destination + pixel, source + pixel * channel.pixel_stride, sizeof(std::uint32_t));
                    channel_rows[channel_index] = destination;
                }
                check_exr(exr_writer_write_scanline_block(writer.value, 0, static_cast<std::int32_t>(y), channel_rows.data()), "write a scanline block");
            }
            check_exr(exr_writer_end_stream(writer.value), "finish the output file");
        }
    } // namespace

    void write_linear_exr(const std::filesystem::path& path, const std::span<const float> rgba, const std::uint32_t width, const std::uint32_t height, const scene::SpectrumColorSpace color_space) {
        constexpr std::size_t pixel_stride = sizeof(float) * 4;
        const std::byte* pixels            = reinterpret_cast<const std::byte*>(rgba.data());
        const std::array channels{ExrChannelSource{"R", EXR_PIXEL_FLOAT, pixels, pixel_stride, 0}, ExrChannelSource{"G", EXR_PIXEL_FLOAT, pixels, pixel_stride, sizeof(float)}, ExrChannelSource{"B", EXR_PIXEL_FLOAT, pixels, pixel_stride, sizeof(float) * 2u}, ExrChannelSource{"A", EXR_PIXEL_FLOAT, pixels, pixel_stride, sizeof(float) * 3u}};
        write_exr(path, width, height, color_space, channels);
    }

    void write_png(const std::filesystem::path& path, const std::span<const std::uint8_t> rgba, const std::uint32_t width, const std::uint32_t height) {
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory{};
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) throw std::runtime_error("Failed to create the Windows Imaging Component factory");
        Microsoft::WRL::ComPtr<IWICStream> stream{};
        if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) throw std::runtime_error(std::format("Failed to create PNG file: {}", path.string()));
        Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder{};
        if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) || FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) throw std::runtime_error("Failed to initialize the PNG encoder");
        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame{};
        Microsoft::WRL::ComPtr<IPropertyBag2> properties{};
        if (FAILED(encoder->CreateNewFrame(&frame, &properties)) || FAILED(frame->Initialize(properties.Get())) || FAILED(frame->SetSize(width, height))) throw std::runtime_error("Failed to initialize the PNG frame");
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA || FAILED(frame->WritePixels(height, width * 4u, static_cast<UINT>(rgba.size_bytes()), const_cast<BYTE*>(rgba.data()))) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) throw std::runtime_error(std::format("Failed to encode PNG file: {}", path.string()));
    }

    void write_render_readback_exr(const std::filesystem::path& path, const render::RenderReadback& readback, const scene::SpectrumColorSpace color_space, const bool camera_space) {
        const std::byte* radiance            = reinterpret_cast<const std::byte*>(readback.radiance.data());
        const std::byte* albedo              = reinterpret_cast<const std::byte*>(readback.albedo.data());
        const std::byte* shading_normals     = reinterpret_cast<const std::byte*>(readback.shading_normals.data());
        const std::byte* geometric_normals   = reinterpret_cast<const std::byte*>(readback.geometric_normals.data());
        const std::byte* positions           = reinterpret_cast<const std::byte*>(readback.positions.data());
        const std::byte* depths              = reinterpret_cast<const std::byte*>(readback.depths.data());
        const std::byte* texture_coordinates = reinterpret_cast<const std::byte*>(readback.texture_coordinates.data());
        const std::byte* object_ids          = reinterpret_cast<const std::byte*>(readback.object_ids.data());
        const std::byte* primitive_ids       = reinterpret_cast<const std::byte*>(readback.primitive_ids.data());
        const std::byte* material_ids        = reinterpret_cast<const std::byte*>(readback.material_ids.data());
        const std::array channels{ExrChannelSource{"R", EXR_PIXEL_FLOAT, radiance, sizeof(scene::Float4), 0}, ExrChannelSource{"G", EXR_PIXEL_FLOAT, radiance, sizeof(scene::Float4), sizeof(float)}, ExrChannelSource{"B", EXR_PIXEL_FLOAT, radiance, sizeof(scene::Float4), sizeof(float) * 2u}, ExrChannelSource{"A", EXR_PIXEL_FLOAT, radiance, sizeof(scene::Float4), sizeof(float) * 3u}, ExrChannelSource{"Albedo.R", EXR_PIXEL_FLOAT, albedo, sizeof(scene::Float3), 0}, ExrChannelSource{"Albedo.G", EXR_PIXEL_FLOAT, albedo, sizeof(scene::Float3), sizeof(float)}, ExrChannelSource{"Albedo.B", EXR_PIXEL_FLOAT, albedo, sizeof(scene::Float3), sizeof(float) * 2u}, ExrChannelSource{"Nshading.X", EXR_PIXEL_FLOAT, shading_normals, sizeof(scene::Float3), 0}, ExrChannelSource{"Nshading.Y", EXR_PIXEL_FLOAT, shading_normals, sizeof(scene::Float3), sizeof(float)}, ExrChannelSource{"Nshading.Z", EXR_PIXEL_FLOAT, shading_normals, sizeof(scene::Float3), sizeof(float) * 2u},
            ExrChannelSource{"Ngeometric.X", EXR_PIXEL_FLOAT, geometric_normals, sizeof(scene::Float3), 0}, ExrChannelSource{"Ngeometric.Y", EXR_PIXEL_FLOAT, geometric_normals, sizeof(scene::Float3), sizeof(float)}, ExrChannelSource{"Ngeometric.Z", EXR_PIXEL_FLOAT, geometric_normals, sizeof(scene::Float3), sizeof(float) * 2u}, ExrChannelSource{"P.X", EXR_PIXEL_FLOAT, positions, sizeof(scene::Float3), 0}, ExrChannelSource{"P.Y", EXR_PIXEL_FLOAT, positions, sizeof(scene::Float3), sizeof(float)}, ExrChannelSource{"P.Z", EXR_PIXEL_FLOAT, positions, sizeof(scene::Float3), sizeof(float) * 2u}, ExrChannelSource{"Z", EXR_PIXEL_FLOAT, depths, sizeof(float), 0}, ExrChannelSource{"UV.U", EXR_PIXEL_FLOAT, texture_coordinates, sizeof(scene::Float2), 0}, ExrChannelSource{"UV.V", EXR_PIXEL_FLOAT, texture_coordinates, sizeof(scene::Float2), sizeof(float)}, ExrChannelSource{"objectId.lo", EXR_PIXEL_UINT, object_ids, sizeof(std::uint64_t), 0},
            ExrChannelSource{"objectId.hi", EXR_PIXEL_UINT, object_ids, sizeof(std::uint64_t), sizeof(std::uint32_t)}, ExrChannelSource{"primitiveId", EXR_PIXEL_UINT, primitive_ids, sizeof(std::uint32_t), 0}, ExrChannelSource{"materialId.lo", EXR_PIXEL_UINT, material_ids, sizeof(std::uint64_t), 0}, ExrChannelSource{"materialId.hi", EXR_PIXEL_UINT, material_ids, sizeof(std::uint64_t), sizeof(std::uint32_t)}};
        write_exr(path, readback.extent.width, readback.extent.height, color_space, channels, camera_space ? "camera" : "world", static_cast<std::int32_t>(readback.accumulated_samples));
    }

    Presenter::Presenter(Spectra& runtime, const std::filesystem::path& shader_directory) : runtime(&runtime), sampler_descriptor(runtime.allocate_sampler_descriptor()) {
        const std::vector<std::uint32_t> vertex_code   = render::load_spirv(shader_directory / "presenter_vertex.spv");
        const std::vector<std::uint32_t> fragment_code = render::load_spirv(shader_directory / "presenter_fragment.spv");
        const std::array create_infos{
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eVertex,
                vk::ShaderStageFlagBits::eFragment,
                vk::ShaderCodeTypeEXT::eSpirv,
                vertex_code.size() * sizeof(std::uint32_t),
                vertex_code.data(),
                "presenter_vertex",
            },
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eFragment,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                fragment_code.size() * sizeof(std::uint32_t),
                fragment_code.data(),
                "presenter_fragment",
            },
        };
        this->shaders = vk::raii::ShaderEXTs{runtime.device, create_infos};
        runtime.write_sampler(this->sampler_descriptor, vk::SamplerCreateInfo{
                                                        {},
                                                        vk::Filter::eLinear,
                                                        vk::Filter::eLinear,
                                                        vk::SamplerMipmapMode::eNearest,
                                                        vk::SamplerAddressMode::eClampToEdge,
                                                        vk::SamplerAddressMode::eClampToEdge,
                                                        vk::SamplerAddressMode::eClampToEdge,
                                                    });
    }

    Presenter::~Presenter() {
        this->runtime->release_sampler_descriptor(this->sampler_descriptor);
    }

    void Presenter::record(const vk::raii::CommandBuffer& command_buffer, const render::RenderOutput source, const vk::Image target_image, const vk::ImageView target_view, const vk::Extent2D extent, const vk::ImageLayout target_layout, const vk::ImageLayout final_layout, const float exposure) {
        const std::array begin_barriers{
            vk::ImageMemoryBarrier2{
                source.stage,
                source.access,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderSampledRead,
                source.layout,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *source.image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            vk::ImageMemoryBarrier2{
                target_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eFragmentShader,
                target_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eShaderSampledRead,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                target_layout,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                target_image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(begin_barriers.size()), begin_barriers.data()});

        const vk::RenderingAttachmentInfo color_attachment{
            target_view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::ClearValue{vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 1.0f}}},
        };
        command_buffer.beginRendering(vk::RenderingInfo{
            {},
            vk::Rect2D{{0, 0}, extent},
            1,
            0,
            1,
            &color_attachment,
        });

        command_buffer.setViewportWithCount(vk::Viewport{
            0.0f,
            0.0f,
            static_cast<float>(extent.width),
            static_cast<float>(extent.height),
            0.0f,
            1.0f,
        });
        command_buffer.setScissorWithCount(vk::Rect2D{{0, 0}, extent});
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
        constexpr vk::Bool32 blend_enable = vk::False;
        command_buffer.setColorBlendEnableEXT(0, blend_enable);
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
            *this->shaders[0],
            *this->shaders[1],
        };
        command_buffer.bindShadersEXT(stages, shader_handles);
        this->runtime->bind_descriptor_heaps(command_buffer);
        struct alignas(16) PresenterPushData {
            DescriptorHandle source;
            DescriptorHandle sampler;
            float exposure;
            std::uint32_t reserved;
        };
        const PresenterPushData push_data{source.sampled_descriptor, this->sampler_descriptor, source.exposure + exposure, 0};
        this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.draw(3, 1, 0, 0);
        command_buffer.endRendering();

        const std::array end_barriers{
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderSampledRead,
                source.stage,
                source.access,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                source.layout,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *source.image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                final_layout == vk::ImageLayout::ePresentSrcKHR        ? vk::PipelineStageFlagBits2::eBottomOfPipe
                : final_layout == vk::ImageLayout::eTransferSrcOptimal ? vk::PipelineStageFlagBits2::eCopy
                                                                       : vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                final_layout == vk::ImageLayout::eTransferSrcOptimal       ? vk::AccessFlagBits2::eTransferRead
                : final_layout == vk::ImageLayout::eColorAttachmentOptimal ? vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite
                                                                           : vk::AccessFlags2{},
                vk::ImageLayout::eColorAttachmentOptimal,
                final_layout,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                target_image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(end_barriers.size()), end_barriers.data()});
    }
} // namespace spectra::app

namespace spectra {
    struct SceneLibraryConfiguration {
        std::vector<std::string> roots{};
    };

    namespace {
        struct ComApartment {
            ComApartment() {
                const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                if (result != S_OK && result != S_FALSE) throw std::runtime_error(std::format("COM initialization failed with HRESULT 0x{:08X}", static_cast<std::uint32_t>(result)));
            }

            ~ComApartment() {
                CoUninitialize();
            }

            ComApartment(const ComApartment&)            = delete;
            ComApartment(ComApartment&&)                 = delete;
            ComApartment& operator=(const ComApartment&) = delete;
            ComApartment& operator=(ComApartment&&)      = delete;
        };

        [[nodiscard]] std::filesystem::path known_folder(const KNOWNFOLDERID& identifier) {
            PWSTR value{};
            if (FAILED(SHGetKnownFolderPath(identifier, KF_FLAG_CREATE, nullptr, &value))) throw std::runtime_error("Failed to locate a required Windows known folder");
            const std::filesystem::path path{value};
            CoTaskMemFree(value);
            return path;
        }

        struct ImGuiPlatform {
            explicit ImGuiPlatform(GLFWwindow* window) {
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                ImGuiIO& io    = ImGui::GetIO();
                io.IniFilename = nullptr;
                ImGui::StyleColorsDark();
                ImGuiStyle& style               = ImGui::GetStyle();
                style.WindowRounding            = 10.0f;
                style.ChildRounding             = 8.0f;
                style.FrameRounding             = 7.0f;
                style.PopupRounding             = 9.0f;
                style.ScrollbarRounding         = 9.0f;
                style.WindowBorderSize          = 0.0f;
                style.FrameBorderSize           = 0.0f;
                style.WindowPadding             = ImVec2{12.0f, 10.0f};
                style.FramePadding              = ImVec2{9.0f, 5.0f};
                style.ItemSpacing               = ImVec2{7.0f, 6.0f};
                ImVec4* colors                  = style.Colors;
                colors[ImGuiCol_WindowBg]       = ImVec4{0.018f, 0.023f, 0.030f, 0.94f};
                colors[ImGuiCol_PopupBg]        = ImVec4{0.025f, 0.031f, 0.040f, 0.98f};
                colors[ImGuiCol_FrameBg]        = ImVec4{0.080f, 0.092f, 0.110f, 0.90f};
                colors[ImGuiCol_FrameBgHovered] = ImVec4{0.115f, 0.135f, 0.160f, 0.95f};
                colors[ImGuiCol_FrameBgActive]  = ImVec4{0.135f, 0.155f, 0.185f, 1.0f};
                colors[ImGuiCol_Header]         = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};
                colors[ImGuiCol_HeaderHovered]  = ImVec4{0.72f, 0.80f, 0.90f, 0.11f};
                colors[ImGuiCol_HeaderActive]   = ImVec4{0.72f, 0.80f, 0.90f, 0.16f};
                colors[ImGuiCol_Separator]      = ImVec4{0.25f, 0.29f, 0.34f, 0.55f};
                if (!ImGui_ImplGlfw_InitForVulkan(window, true)) throw std::runtime_error("ImGui GLFW platform initialization failed");
            }

            ~ImGuiPlatform() {
                ImGui_ImplGlfw_Shutdown();
                ImGui::DestroyContext();
            }

            ImGuiPlatform(const ImGuiPlatform&)            = delete;
            ImGuiPlatform(ImGuiPlatform&&)                 = delete;
            ImGuiPlatform& operator=(const ImGuiPlatform&) = delete;
            ImGuiPlatform& operator=(ImGuiPlatform&&)      = delete;
        };

        [[nodiscard]] std::optional<std::filesystem::path> open_source_dialog(HWND owner) {
            Microsoft::WRL::ComPtr<IFileOpenDialog> dialog{};
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) throw std::runtime_error("Failed to create the Windows open dialog");
            constexpr std::array filters{COMDLG_FILTERSPEC{L"Spectra Scene", L"*.spectra"}};
            dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
            dialog->SetTitle(L"Open Spectra Scene");
            const HRESULT shown = dialog->Show(owner);
            if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return std::nullopt;
            if (FAILED(shown)) throw std::runtime_error("The Windows open dialog failed");
            Microsoft::WRL::ComPtr<IShellItem> item{};
            if (FAILED(dialog->GetResult(&item))) throw std::runtime_error("The Windows open dialog returned no item");
            PWSTR path{};
            if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) throw std::runtime_error("The Windows open dialog returned no filesystem path");
            const std::filesystem::path result{path};
            CoTaskMemFree(path);
            return result;
        }

        [[nodiscard]] std::optional<std::filesystem::path> save_scene_dialog(HWND owner, const std::filesystem::path& current, const bool frozen = false) {
            Microsoft::WRL::ComPtr<IFileSaveDialog> dialog{};
            if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) throw std::runtime_error("Failed to create the Windows save dialog");
            constexpr std::array filters{
                COMDLG_FILTERSPEC{L"Spectra Scene", L"*.spectra"},
            };
            dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
            dialog->SetDefaultExtension(L"spectra");
            dialog->SetTitle(frozen ? L"Export Frozen Spectra Scene" : L"Save Spectra Scene");
            const std::filesystem::path filename = frozen ? current.parent_path() / std::format("{}-snapshot.spectra", current.stem().string()) : current.filename();
            dialog->SetFileName(filename.filename().c_str());
            const HRESULT shown = dialog->Show(owner);
            if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return std::nullopt;
            if (FAILED(shown)) throw std::runtime_error("The Windows save dialog failed");
            Microsoft::WRL::ComPtr<IShellItem> item{};
            if (FAILED(dialog->GetResult(&item))) throw std::runtime_error("The Windows save dialog returned no item");
            PWSTR path{};
            if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) throw std::runtime_error("The Windows save dialog returned no filesystem path");
            const std::filesystem::path result{path};
            CoTaskMemFree(path);
            return result;
        }

        struct ViewportDisplay {
            explicit ViewportDisplay(Spectra& runtime) : runtime(&runtime), descriptor(runtime.allocate_resource_descriptor()), texture_id(static_cast<std::uint64_t>(this->descriptor.index) + 1u) {}

            ~ViewportDisplay() {
                this->runtime->release_resource_descriptor(this->descriptor);
            }

            ViewportDisplay(const ViewportDisplay&)            = delete;
            ViewportDisplay(ViewportDisplay&&)                 = delete;
            ViewportDisplay& operator=(const ViewportDisplay&) = delete;
            ViewportDisplay& operator=(ViewportDisplay&&)      = delete;

            void ensure(const vk::Extent2D extent) {
                if (*this->image.image && this->image.extent == extent) return;
                this->image = this->runtime->create_image_2d(extent, vk::Format::eB8G8R8A8Srgb, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);
                this->runtime->write_sampled_image(this->descriptor, this->image, vk::ImageLayout::eShaderReadOnlyOptimal);
                this->layout = vk::ImageLayout::eUndefined;
            }

            Spectra* runtime{};
            GpuImage image{};
            DescriptorHandle descriptor{};
            vk::ImageLayout layout{vk::ImageLayout::eUndefined};
            std::uint64_t texture_id{};
        };

        struct CaptureManager {
            struct Completed {
                std::filesystem::path path{};
                std::string error{};
            };

            CaptureManager(Spectra& runtime, const std::uint32_t frame_count) : runtime(&runtime), slots(frame_count) {}

            void request(const render::ImageFileFormat format, const workspace::RenderMode mode, const scene::Film& film, std::optional<std::filesystem::path> path = std::nullopt) {
                const std::int64_t timestamp         = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                const std::string_view renderer_name = mode == workspace::RenderMode::Rasterizer ? "rasterizer" : "pathtracer";
                const std::string_view extension     = format == render::ImageFileFormat::Png ? "png" : "exr";
                if (!path) {
                    const std::filesystem::path directory = known_folder(FOLDERID_Pictures) / "Spectra";
                    std::filesystem::create_directories(directory);
                    path = directory / std::format("spectra-{}-{}.{}", renderer_name, timestamp, extension);
                }
                this->requested = Pending{
                    format,
                    std::move(*path),
                    {},
                    format == render::ImageFileFormat::Exr && mode == workspace::RenderMode::PathTracer && film.gbuffer,
                    film.color_space,
                    film.gbuffer_camera_space,
                };
            }

            [[nodiscard]] std::optional<Completed> consume(const std::uint32_t frame_index, workspace::Workspace& workspace) noexcept {
                Slot& slot = this->slots[frame_index];
                if (!slot.pending) return std::nullopt;
                Completed completed{slot.pending->path, {}};
                try {
                    if (slot.pending->gbuffer) {
                        app::write_render_readback_exr(slot.pending->path, workspace.readback(), slot.pending->color_space, slot.pending->camera_space);
                    } else if (slot.pending->format == render::ImageFileFormat::Png) {
                        const std::size_t pixel_count = static_cast<std::size_t>(slot.pending->extent.width) * slot.pending->extent.height;
                        app::write_png(slot.pending->path, std::span{static_cast<const std::uint8_t*>(slot.buffer.mapped), pixel_count * 4u}, slot.pending->extent.width, slot.pending->extent.height);
                    } else {
                        const std::size_t pixel_count = static_cast<std::size_t>(slot.pending->extent.width) * slot.pending->extent.height;
                        app::write_linear_exr(slot.pending->path, std::span{static_cast<const float*>(slot.buffer.mapped), pixel_count * 4u}, slot.pending->extent.width, slot.pending->extent.height, slot.pending->color_space);
                    }
                } catch (const std::exception& error) {
                    completed.error = error.what();
                }
                slot.pending.reset();
                return completed;
            }

            void record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_index, const render::RenderOutput source, const ViewportDisplay& display) {
                if (!this->requested) return;
                Slot& slot                           = this->slots[frame_index];
                const render::ImageFileFormat format = this->requested->format;
                if (this->requested->gbuffer) {
                    slot.pending         = std::exchange(this->requested, std::nullopt);
                    slot.pending->extent = source.image.extent;
                    return;
                }
                const vk::Extent2D extent          = format == render::ImageFileFormat::Png ? display.image.extent : source.image.extent;
                const vk::DeviceSize required_size = static_cast<vk::DeviceSize>(extent.width) * extent.height * (format == render::ImageFileFormat::Png ? 4u : sizeof(float) * 4u);
                if (slot.buffer.size < required_size) slot.buffer = this->runtime->create_buffer(required_size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
                slot.pending                        = std::exchange(this->requested, std::nullopt);
                slot.pending->extent                = extent;
                const vk::Image image               = format == render::ImageFileFormat::Png ? *display.image.image : *source.image.image;
                const vk::ImageLayout layout        = format == render::ImageFileFormat::Png ? vk::ImageLayout::eShaderReadOnlyOptimal : source.layout;
                const vk::PipelineStageFlags2 stage = format == render::ImageFileFormat::Png ? vk::PipelineStageFlagBits2::eFragmentShader : source.stage;
                const vk::AccessFlags2 access       = format == render::ImageFileFormat::Png ? vk::AccessFlagBits2::eShaderSampledRead : source.access;
                const vk::ImageMemoryBarrier2 to_transfer{
                    stage,
                    access,
                    vk::PipelineStageFlagBits2::eCopy,
                    vk::AccessFlagBits2::eTransferRead,
                    layout,
                    vk::ImageLayout::eTransferSrcOptimal,
                    vk::QueueFamilyIgnored,
                    vk::QueueFamilyIgnored,
                    image,
                    {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
                };
                command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_transfer});
                command_buffer.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, *slot.buffer.buffer,
                    vk::BufferImageCopy{
                        0,
                        0,
                        0,
                        {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                        {0, 0, 0},
                        {extent.width, extent.height, 1},
                    });
                const std::array completion_barriers{
                    vk::ImageMemoryBarrier2{
                        vk::PipelineStageFlagBits2::eCopy,
                        vk::AccessFlagBits2::eTransferRead,
                        stage,
                        access,
                        vk::ImageLayout::eTransferSrcOptimal,
                        layout,
                        vk::QueueFamilyIgnored,
                        vk::QueueFamilyIgnored,
                        image,
                        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
                    },
                };
                const vk::MemoryBarrier2 host_barrier{
                    vk::PipelineStageFlagBits2::eCopy,
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eHost,
                    vk::AccessFlagBits2::eHostRead,
                };
                command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &host_barrier, {}, {}, static_cast<std::uint32_t>(completion_barriers.size()), completion_barriers.data()});
            }

        private:
            struct Pending {
                render::ImageFileFormat format{render::ImageFileFormat::Png};
                std::filesystem::path path{};
                vk::Extent2D extent{};
                bool gbuffer{};
                scene::SpectrumColorSpace color_space{scene::SpectrumColorSpace::Srgb};
                bool camera_space{true};
            };

            struct Slot {
                GpuBuffer buffer{};
                std::optional<Pending> pending{};
            };

            Spectra* runtime{};
            std::vector<Slot> slots{};
            std::optional<Pending> requested{};
        };

        struct Application {
            enum class ExportPhase : std::uint8_t {
                Prepare,
                Advance,
                Render,
                Capture,
                Complete,
            };

            Application(std::optional<std::filesystem::path> scene_path, const std::filesystem::path& scene_library, std::vector<std::filesystem::path> scene_roots, const std::filesystem::path& shader_directory, const bool start_pathtracer, std::optional<SequenceExport> sequence_export) : com{}, runtime{}, imgui_platform(this->runtime.window), scene_library_path(scene_library), session_scene_roots(std::move(scene_roots)), shader_directory(shader_directory), presenter(this->runtime, shader_directory), imgui_renderer(this->runtime, shader_directory, Spectra::frames_in_flight), viewport_display(this->runtime), capture(this->runtime, Spectra::frames_in_flight), sequence_export(std::move(sequence_export)) {
                if (scene_path) {
                    this->workspace.emplace(this->runtime, shader_directory, *scene_path, Spectra::frames_in_flight);
                    if (start_pathtracer) this->workspace->mode = workspace::RenderMode::PathTracer;
                } else {
                    this->scene_library_visible = true;
                    this->refresh_scene_library();
                }
                if (this->sequence_export) {
                    if (!this->workspace->has_dynamic_setup()) throw std::runtime_error("Animation sequence export requires a Scene Dynamic Setup");
                    std::filesystem::create_directories(this->sequence_export->directory);
                    this->sequence_frame = this->sequence_export->first_frame;
                }
            }

            ~Application() {
                this->runtime.wait_idle();
                if (this->workspace) this->workspace->wait_for_export();
            }

            Application(const Application&)            = delete;
            Application(Application&&)                 = delete;
            Application& operator=(const Application&) = delete;
            Application& operator=(Application&&)      = delete;

            void run(const std::optional<std::uint64_t> maximum_frame_count) {
                while (true) {
                    if (maximum_frame_count && this->frame_number >= *maximum_frame_count) break;
                    this->runtime.poll_events();
                    if (this->runtime.take_close_request()) break;
                    this->handle_dropped_paths();

                    const std::optional<FrameContext> frame = this->runtime.begin_frame();
                    if (!frame) {
                        this->runtime.wait_events();
                        this->simulation_clock_valid = false;
                        continue;
                    }
                    const std::chrono::steady_clock::time_point simulation_time = std::chrono::steady_clock::now();
                    const bool simulation_clock_active                         = this->workspace && !this->sequence_export && !this->scene_library_visible;
                    if (this->workspace) {
                        if (const std::optional<CaptureManager::Completed> completed = this->capture.consume(frame->index, *this->workspace)) {
                            if (this->sequence_export) this->complete_sequence_frame(*completed);
                            if (completed->error.empty()) {
                                this->workspace_ui.status       = std::format("Capture written  {}", completed->path.filename().string());
                                this->workspace_ui.status_error = false;
                            } else {
                                this->workspace_ui.status       = completed->error;
                                this->workspace_ui.status_error = true;
                            }
                        }
                        this->workspace->begin_frame(frame->index);
                        if (const std::optional<workspace::FrozenExportResult> completed = this->workspace->take_export_result()) {
                            this->workspace_ui.status       = completed->error.empty() ? std::format("Frozen Scene written  {}", completed->path.filename().string()) : completed->error;
                            this->workspace_ui.status_error = !completed->error.empty();
                        }
                        if (simulation_clock_active && this->simulation_clock_valid) this->workspace->update(simulation_time - this->simulation_clock);
                        this->resize_viewport(frame->target.extent);
                    }
                    this->simulation_clock       = simulation_time;
                    this->simulation_clock_valid = simulation_clock_active;

                    ImGui_ImplGlfw_NewFrame();
                    ImGui::NewFrame();
                    const app::WorkspaceUiActions actions = this->scene_library_visible ? this->workspace_ui.draw_scene_library(this->scene_library_scenes, this->scene_library_problems, this->workspace.has_value()) : this->workspace_ui.draw(*this->workspace, this->viewport_display.texture_id);
                    this->runtime.drag_regions            = actions.drag_regions;
                    this->handle_actions(actions);
                    if (!this->simulation_clock_valid && this->workspace && !this->sequence_export && !this->scene_library_visible) {
                        this->simulation_clock       = std::chrono::steady_clock::now();
                        this->simulation_clock_valid = true;
                    }
                    ImGui::Render();
                    if (this->workspace) {
                        this->update_sequence_export();
                        this->resize_viewport(frame->target.extent);
                        const vk::Extent2D viewport_extent = this->viewport_display.image.extent;
                        this->workspace->prepare(frame->command_buffer, viewport_extent);
                        this->workspace->record(frame->command_buffer, frame->index);
                        const render::RenderOutput output = this->workspace->output();
                        this->presenter.record(frame->command_buffer, output, *this->viewport_display.image.image, *this->viewport_display.image.view, viewport_extent, this->viewport_display.layout, vk::ImageLayout::eColorAttachmentOptimal, this->workspace->exposure);
                        this->viewport_display.layout = vk::ImageLayout::eColorAttachmentOptimal;
                        this->workspace->record_overlays(frame->command_buffer, *this->viewport_display.image.image, *this->viewport_display.image.view, viewport_extent, actions.show_axes);
                        this->viewport_display.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
                        this->capture.record(frame->command_buffer, frame->index, output, this->viewport_display);
                    }
                    this->imgui_renderer.record(*ImGui::GetDrawData(), frame->command_buffer, frame->index, frame->target.image, frame->target.view, frame->target.extent, frame->target.layout, vk::ImageLayout::ePresentSrcKHR);
                    if (this->runtime.present_frame()) ++this->frame_number;
                    if (this->export_phase == ExportPhase::Complete) break;
                }
            }

            void update_sequence_export() {
                if (!this->sequence_export) return;
                if (this->export_phase == ExportPhase::Prepare) {
                    this->workspace->set_export_frame(this->sequence_frame, this->sequence_frame == this->sequence_export->first_frame);
                    this->export_phase = ExportPhase::Advance;
                    return;
                }
                if (this->export_phase == ExportPhase::Advance) {
                    if (this->workspace->timeline().step != this->sequence_frame) return;
                    this->workspace->pathtracer_paused = false;
                    this->export_phase                = ExportPhase::Render;
                    return;
                }
                if (this->export_phase != ExportPhase::Render) return;
                if (this->workspace->mode == workspace::RenderMode::PathTracer && this->workspace->accumulated_path_samples() < this->workspace->scene.sampler().samples_per_pixel) return;
                const render::ImageFileFormat format = this->sequence_export->format;
                const std::string_view extension     = format == render::ImageFileFormat::Png ? "png" : "exr";
                this->sequence_path                  = this->sequence_export->directory / std::format("frame-{:06}.{}", this->sequence_frame, extension);
                this->capture.request(format, this->workspace->mode, this->workspace->scene.film(), this->sequence_path);
                this->workspace->pathtracer_paused = true;
                this->export_phase                = ExportPhase::Capture;
            }

            void complete_sequence_frame(const CaptureManager::Completed& completed) {
                if (this->export_phase != ExportPhase::Capture) return;
                if (!completed.error.empty()) throw std::runtime_error(completed.error);
                const scene::dynamics::TimelineState timeline = this->workspace->timeline();
                const scene::Sampler& sampler                   = this->workspace->scene.sampler();
                std::ofstream metadata{this->sequence_path.replace_extension(".json")};
                if (!metadata) throw std::runtime_error("Failed to create animation frame metadata");
                metadata << "{\n"
                         << "  \"simulationStep\": " << this->sequence_frame << ",\n"
                         << "  \"simulationSeconds\": " << std::setprecision(17) << timeline.seconds << ",\n"
                         << "  \"renderer\": \"" << (this->workspace->mode == workspace::RenderMode::Rasterizer ? "rasterizer" : "pathtracer") << "\",\n"
                         << "  \"samplesPerPixel\": " << (this->workspace->mode == workspace::RenderMode::PathTracer ? this->workspace->accumulated_path_samples() : 1u) << ",\n"
                         << "  \"seed\": " << sampler.seed << "\n"
                         << "}\n";
                if (!metadata) throw std::runtime_error("Failed to write animation frame metadata");
                if (this->sequence_frame == this->sequence_export->last_frame) {
                    this->export_phase = ExportPhase::Complete;
                    return;
                }
                ++this->sequence_frame;
                this->export_phase = ExportPhase::Prepare;
            }

            void resize_viewport(const vk::Extent2D requested) {
                if (*this->viewport_display.image.image && requested == this->viewport_display.image.extent) return;
                this->runtime.wait_idle();
                this->viewport_display.ensure(requested);
            }

            [[nodiscard]] bool confirm_scene_replacement() {
                if (!this->workspace || !this->workspace->dirty()) return true;
                this->simulation_clock_valid = false;
                const int result = MessageBoxW(glfwGetWin32Window(this->runtime.window), L"The current Spectra scene has unsaved changes.\n\nSave before continuing?", L"Spectra", MB_ICONWARNING | MB_YESNOCANCEL);
                if (result == IDCANCEL) return false;
                if (result == IDYES) this->workspace->save();
                return true;
            }

            void open_source(const std::filesystem::path& path) {
                this->simulation_clock_valid = false;
                if (path.extension() != ".spectra") throw std::runtime_error("Spectra accepts only .spectra scenes");
                if (!this->confirm_scene_replacement()) return;
                if (this->workspace)
                    this->workspace->open_scene(path);
                else
                    this->workspace.emplace(this->runtime, this->shader_directory, path, Spectra::frames_in_flight);
                this->scene_library_visible = false;
            }

            void reload_source() {
                this->simulation_clock_valid = false;
                if (!this->confirm_scene_replacement()) return;
                this->workspace->open_scene(this->workspace->source_path);
                this->workspace_ui.status       = "Scene reloaded";
                this->workspace_ui.status_error = false;
            }

            void handle_dropped_paths() {
                const std::vector<std::filesystem::path> paths = this->runtime.take_dropped_paths();
                if (paths.empty()) return;
                try {
                    if (paths.size() != 1u) throw std::runtime_error("Drop exactly one .spectra scene");
                    this->open_source(paths.front());
                } catch (const std::exception& error) {
                    this->workspace_ui.status       = error.what();
                    this->workspace_ui.status_error = true;
                }
            }

            void handle_actions(const app::WorkspaceUiActions& actions) {
                if (actions.exit_application) this->runtime.request_close();
                try {
                    if (actions.open_scene_library) {
                        this->simulation_clock_valid = false;
                        this->refresh_scene_library();
                        this->scene_library_visible = true;
                    }
                    if (actions.close_scene_library) this->scene_library_visible = false;
                    if (actions.refresh_scene_library) {
                        this->simulation_clock_valid = false;
                        this->refresh_scene_library();
                    }
                    if (actions.open_scene_file) {
                        this->simulation_clock_valid = false;
                        if (const std::optional<std::filesystem::path> path = open_source_dialog(glfwGetWin32Window(this->runtime.window))) this->open_source(*path);
                    }
                    if (actions.selected_scene) this->open_source(*actions.selected_scene);
                    if (actions.reload_scene) this->reload_source();
                    if (actions.save_scene) {
                        this->simulation_clock_valid = false;
                        this->workspace->save();
                        this->workspace_ui.status       = "Scene saved";
                        this->workspace_ui.status_error = false;
                    }
                    if (actions.save_scene_as) {
                        this->simulation_clock_valid = false;
                        if (const std::optional<std::filesystem::path> path = save_scene_dialog(glfwGetWin32Window(this->runtime.window), this->workspace->source_path)) {
                            this->workspace->save_as(*path);
                            this->workspace_ui.status       = "Scene saved";
                            this->workspace_ui.status_error = false;
                        }
                    }
                    if (actions.export_frozen_scene) {
                        this->simulation_clock_valid = false;
                        if (const std::optional<std::filesystem::path> path = save_scene_dialog(glfwGetWin32Window(this->runtime.window), this->workspace->source_path, true)) {
                            this->workspace->export_frozen(*path);
                            this->workspace_ui.status       = "Capturing Frozen Scene";
                            this->workspace_ui.status_error = false;
                        }
                    }
                    if (!this->sequence_export && actions.capture) this->capture.request(*actions.capture, this->workspace->mode, this->workspace->scene.film());
                } catch (const std::exception& error) {
                    this->workspace_ui.status       = error.what();
                    this->workspace_ui.status_error = true;
                }
            }

            void refresh_scene_library() {
                std::ifstream stream{this->scene_library_path, std::ios::binary};
                if (!stream) throw std::runtime_error(std::format("Failed to open Spectra Scene Library configuration: {}", this->scene_library_path.string()));
                const std::string json{
                    std::istreambuf_iterator<char>{stream},
                    std::istreambuf_iterator<char>{},
                };
                SceneLibraryConfiguration configuration{};
                constexpr glz::opts options{
                    .error_on_unknown_keys = true,
                    .error_on_missing_keys = true,
                };
                const glz::error_ctx error = glz::read<options>(configuration, json);
                if (error) throw std::runtime_error(std::format("Failed to parse Spectra Scene Library configuration {}:\n{}", this->scene_library_path.string(), glz::format_error(error, json)));

                std::vector<std::filesystem::path> roots{};
                roots.reserve(configuration.roots.size() + this->session_scene_roots.size());
                for (const std::string& configured : configuration.roots) {
                    const std::filesystem::path relative{configured};
                    if (relative.is_absolute()) throw std::runtime_error("Spectra Scene Library roots must be relative to library.json");
                    roots.emplace_back(this->scene_library_path.parent_path() / relative);
                }
                for (const std::filesystem::path& root : this->session_scene_roots) roots.emplace_back(std::filesystem::absolute(root));

                this->scene_library_scenes.clear();
                this->scene_library_problems.clear();
                std::vector<std::filesystem::path> discovered{};
                for (const std::filesystem::path& declared_root : roots) {
                    std::error_code root_error{};
                    const std::filesystem::path root = std::filesystem::weakly_canonical(declared_root, root_error);
                    if (root_error || !std::filesystem::is_directory(root)) {
                        this->scene_library_problems.emplace_back(declared_root, "Scene root is unavailable");
                        continue;
                    }
                    try {
                        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator{root}) {
                            if (!entry.is_regular_file() || entry.path().extension() != ".spectra") continue;
                            const std::filesystem::path path = std::filesystem::weakly_canonical(entry.path());
                            if (std::ranges::contains(discovered, path)) continue;
                            discovered.emplace_back(path);
                            try {
                                this->scene_library_scenes.emplace_back(scene::inspect_scene(path), root);
                            } catch (const std::exception& inspection_error) {
                                this->scene_library_problems.emplace_back(path, inspection_error.what());
                            }
                        }
                    } catch (const std::exception& scan_error) {
                        this->scene_library_problems.emplace_back(root, scan_error.what());
                    }
                }
                std::ranges::sort(this->scene_library_scenes, [](const app::SceneLibraryEntry& left, const app::SceneLibraryEntry& right) {
                    const std::string left_name  = left.scene.name.empty() ? left.scene.path.string() : left.scene.name;
                    const std::string right_name = right.scene.name.empty() ? right.scene.path.string() : right.scene.name;
                    return std::tie(left_name, left.scene.path) < std::tie(right_name, right.scene.path);
                });
                std::ranges::sort(this->scene_library_problems, {}, &app::SceneLibraryProblem::path);
            }

            ComApartment com{};
            Spectra runtime{};
            ImGuiPlatform imgui_platform;
            std::optional<workspace::Workspace> workspace{};
            app::WorkspaceUi workspace_ui{};
            std::filesystem::path scene_library_path{};
            std::vector<std::filesystem::path> session_scene_roots{};
            std::filesystem::path shader_directory{};
            std::vector<app::SceneLibraryEntry> scene_library_scenes{};
            std::vector<app::SceneLibraryProblem> scene_library_problems{};
            bool scene_library_visible{};
            app::Presenter presenter;
            app::ImGuiRenderer imgui_renderer;
            ViewportDisplay viewport_display;
            CaptureManager capture;
            std::optional<SequenceExport> sequence_export{};
            std::filesystem::path sequence_path{};
            std::uint64_t sequence_frame{};
            ExportPhase export_phase{ExportPhase::Prepare};
            std::uint64_t frame_number{};
            std::chrono::steady_clock::time_point simulation_clock{};
            bool simulation_clock_valid{};
        };
    } // namespace

    void run_application(std::optional<std::filesystem::path> scene_path, const std::filesystem::path& scene_library, std::vector<std::filesystem::path> scene_roots, const std::filesystem::path& shader_directory, const std::optional<std::uint64_t> maximum_frame_count, const bool start_pathtracer, std::optional<SequenceExport> sequence_export) {
        Application application{std::move(scene_path), scene_library, std::move(scene_roots), shader_directory, start_pathtracer, std::move(sequence_export)};
        application.run(maximum_frame_count);
    }
} // namespace spectra
