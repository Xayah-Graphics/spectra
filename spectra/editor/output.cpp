module;

#include <Windows.h>
#include <exr.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>

module spectra.editor;

import :output;

import std;
import vulkan;

namespace spectra {
    EditorOutputResult write_frozen_scene_package(scene::Scene scene, const std::filesystem::path& requested_path, const std::filesystem::path& source_scene_path) {
        EditorOutputResult result{};
        const std::filesystem::path parent      = std::filesystem::absolute(requested_path.parent_path());
        const std::string name                  = requested_path.stem().string();
        const std::filesystem::path destination = parent / name;
        const std::filesystem::path temporary   = parent / std::format(".{}.spectra-export-{}", name, std::chrono::steady_clock::now().time_since_epoch().count());
        try {
            if (std::filesystem::exists(destination)) throw std::runtime_error(std::format("Frozen Scene destination '{}' already exists", destination.string()));
            if (temporary.parent_path() != parent) throw std::runtime_error("Frozen Scene temporary directory escaped its destination");
            std::filesystem::create_directory(temporary);
            const std::filesystem::path scene_path = temporary / std::format("{}.spectra", name);
            scene::save_scene(std::move(scene), scene_path, source_scene_path);
            std::filesystem::rename(temporary, destination);
            result.output_path = destination / scene_path.filename();
        } catch (const std::exception& error) {
            result.error_message = error.what();
            std::error_code cleanup_error{};
            if (temporary.parent_path() == parent) std::filesystem::remove_all(temporary, cleanup_error);
        }
        return result;
    }

} // namespace spectra

namespace spectra {
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

    void write_path_tracer_exr(const std::filesystem::path& path, const RenderGBufferReadback& readback, const scene::SpectrumColorSpace color_space, const bool gbuffer_camera_space) {
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
        const std::array channels{ExrChannelSource{"R", EXR_PIXEL_FLOAT, radiance, sizeof(math::Float4), 0}, ExrChannelSource{"G", EXR_PIXEL_FLOAT, radiance, sizeof(math::Float4), sizeof(float)}, ExrChannelSource{"B", EXR_PIXEL_FLOAT, radiance, sizeof(math::Float4), sizeof(float) * 2u}, ExrChannelSource{"A", EXR_PIXEL_FLOAT, radiance, sizeof(math::Float4), sizeof(float) * 3u}, ExrChannelSource{"Albedo.R", EXR_PIXEL_FLOAT, albedo, sizeof(math::Float3), 0}, ExrChannelSource{"Albedo.G", EXR_PIXEL_FLOAT, albedo, sizeof(math::Float3), sizeof(float)}, ExrChannelSource{"Albedo.B", EXR_PIXEL_FLOAT, albedo, sizeof(math::Float3), sizeof(float) * 2u}, ExrChannelSource{"Nshading.X", EXR_PIXEL_FLOAT, shading_normals, sizeof(math::Float3), 0}, ExrChannelSource{"Nshading.Y", EXR_PIXEL_FLOAT, shading_normals, sizeof(math::Float3), sizeof(float)}, ExrChannelSource{"Nshading.Z", EXR_PIXEL_FLOAT, shading_normals, sizeof(math::Float3), sizeof(float) * 2u},
            ExrChannelSource{"Ngeometric.X", EXR_PIXEL_FLOAT, geometric_normals, sizeof(math::Float3), 0}, ExrChannelSource{"Ngeometric.Y", EXR_PIXEL_FLOAT, geometric_normals, sizeof(math::Float3), sizeof(float)}, ExrChannelSource{"Ngeometric.Z", EXR_PIXEL_FLOAT, geometric_normals, sizeof(math::Float3), sizeof(float) * 2u}, ExrChannelSource{"P.X", EXR_PIXEL_FLOAT, positions, sizeof(math::Float3), 0}, ExrChannelSource{"P.Y", EXR_PIXEL_FLOAT, positions, sizeof(math::Float3), sizeof(float)}, ExrChannelSource{"P.Z", EXR_PIXEL_FLOAT, positions, sizeof(math::Float3), sizeof(float) * 2u}, ExrChannelSource{"Z", EXR_PIXEL_FLOAT, depths, sizeof(float), 0}, ExrChannelSource{"UV.U", EXR_PIXEL_FLOAT, texture_coordinates, sizeof(math::Float2), 0}, ExrChannelSource{"UV.V", EXR_PIXEL_FLOAT, texture_coordinates, sizeof(math::Float2), sizeof(float)}, ExrChannelSource{"objectId.lo", EXR_PIXEL_UINT, object_ids, sizeof(std::uint64_t), 0},
            ExrChannelSource{"objectId.hi", EXR_PIXEL_UINT, object_ids, sizeof(std::uint64_t), sizeof(std::uint32_t)}, ExrChannelSource{"primitiveId", EXR_PIXEL_UINT, primitive_ids, sizeof(std::uint32_t), 0}, ExrChannelSource{"materialId.lo", EXR_PIXEL_UINT, material_ids, sizeof(std::uint64_t), 0}, ExrChannelSource{"materialId.hi", EXR_PIXEL_UINT, material_ids, sizeof(std::uint64_t), sizeof(std::uint32_t)}};
        write_exr(path, readback.extent.width, readback.extent.height, color_space, channels, gbuffer_camera_space ? "camera" : "world", static_cast<std::int32_t>(readback.accumulated_samples));
    }

} // namespace spectra

namespace spectra {
    void EditorOutput::initialize() {
        this->capture.slots.resize(VulkanFrames::frames_in_flight);
        this->frozen_export.slots.resize(VulkanFrames::frames_in_flight);
        this->presenter.sampler_descriptor = this->context.runtime.resources.allocate_sampler_descriptor();
        const std::vector<std::uint32_t> vertex_code    = load_spirv(this->context.shader_directory / "presenter_vertex.spv");
        const std::vector<std::uint32_t> fragment_code  = load_spirv(this->context.shader_directory / "presenter_fragment.spv");
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
        this->presenter.shaders = vk::raii::ShaderEXTs{this->context.runtime.graphics.device, create_infos};
        this->context.runtime.resources.write_sampler_descriptor(this->presenter.sampler_descriptor, vk::SamplerCreateInfo{
                                                                                                            {},
                                                                                                            vk::Filter::eLinear,
                                                                                                            vk::Filter::eLinear,
                                                                                                            vk::SamplerMipmapMode::eNearest,
                                                                                                            vk::SamplerAddressMode::eClampToEdge,
                                                                                                            vk::SamplerAddressMode::eClampToEdge,
                                                                                                            vk::SamplerAddressMode::eClampToEdge,
                                                                                                        });
    }

    void EditorOutput::record_presenter_impl(const vk::raii::CommandBuffer& command_buffer, const RenderOutput render_output, const vk::Image target_image, const vk::ImageView target_view, const vk::Extent2D extent, const vk::ImageLayout target_layout, const vk::ImageLayout final_layout, const float exposure) {
        const std::array begin_barriers{
            vk::ImageMemoryBarrier2{
                render_output.source_stage,
                render_output.source_access,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderSampledRead,
                render_output.image_layout,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *render_output.image.image,
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
            *this->presenter.shaders[0],
            *this->presenter.shaders[1],
        };
        command_buffer.bindShadersEXT(stages, shader_handles);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        struct alignas(16) PresenterPushData {
            DescriptorHandle source;
            DescriptorHandle sampler;
            float exposure;
            std::uint32_t reserved;
        };
        const PresenterPushData push_data{render_output.sampled_descriptor, this->presenter.sampler_descriptor, render_output.exposure + exposure, 0};
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.draw(3, 1, 0, 0);
        command_buffer.endRendering();

        const std::array end_barriers{
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderSampledRead,
                render_output.source_stage,
                render_output.source_access,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                render_output.image_layout,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *render_output.image.image,
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

    EditorOutput::EditorOutput(VulkanRuntime& runtime, GpuScene& gpu_scene, Renderers& renderers, EditorViewport& viewport, std::filesystem::path shader_directory) noexcept : context{runtime, gpu_scene, renderers, viewport, std::move(shader_directory)} {}

    EditorOutput::~EditorOutput() {
        this->wait_for_frozen_scene_export();
        this->context.runtime.frames.retire_sampler_descriptor(this->presenter.sampler_descriptor);
    }

    void EditorOutput::record_presenter(const vk::raii::CommandBuffer& command_buffer, const RenderOutput render_output) {
        this->record_presenter_impl(command_buffer, render_output, *this->context.viewport.target.image.image, *this->context.viewport.target.image.view, this->context.viewport.target.image.extent, this->context.viewport.target.layout, vk::ImageLayout::eColorAttachmentOptimal, this->presenter.exposure);
        this->context.viewport.target.layout = vk::ImageLayout::eColorAttachmentOptimal;
    }

    std::filesystem::path EditorOutput::pictures_directory() {
        PWSTR value{};
        if (FAILED(SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_DEFAULT, nullptr, &value))) throw std::runtime_error("Failed to locate the Windows Pictures directory");
        const std::filesystem::path result{value};
        CoTaskMemFree(value);
        return result;
    }

    void EditorOutput::request_capture(const CaptureFormat image_format, const scene::Film& film) {
        const std::int64_t timestamp          = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const std::string_view renderer_name  = this->context.renderers.active_descriptor().id;
        const std::string_view extension      = image_format == CaptureFormat::Png ? "png" : "exr";
        const std::filesystem::path directory = this->pictures_directory() / "Spectra";
        std::filesystem::create_directories(directory);
        this->capture.pending = PendingCapture{
            image_format,
            directory / std::format("spectra-{}-{}.{}", renderer_name, timestamp, extension),
            {},
            image_format == CaptureFormat::Exr && this->context.renderers.gbuffer_available() && film.gbuffer,
            film.color_space,
            film.gbuffer_camera_space,
        };
    }

    std::optional<EditorOutputResult> EditorOutput::consume_capture(const std::uint32_t frame_slot_index) {
        CaptureFrameSlot& slot = this->capture.slots[frame_slot_index];
        if (!slot.pending) return std::nullopt;
        EditorOutputResult result{.output_path = slot.pending->output_path};
        try {
            if (slot.pending->include_gbuffer) {
                write_path_tracer_exr(slot.pending->output_path, this->context.renderers.readback(), slot.pending->color_space, slot.pending->gbuffer_camera_space);
            } else if (slot.pending->image_format == CaptureFormat::Png) {
                const std::size_t pixel_count = static_cast<std::size_t>(slot.pending->image_extent.width) * slot.pending->image_extent.height;
                write_png(slot.pending->output_path, std::span{static_cast<const std::uint8_t*>(slot.readback_buffer.mapped), pixel_count * 4u}, slot.pending->image_extent.width, slot.pending->image_extent.height);
            } else {
                const std::size_t pixel_count = static_cast<std::size_t>(slot.pending->image_extent.width) * slot.pending->image_extent.height;
                write_linear_exr(slot.pending->output_path, std::span{static_cast<const float*>(slot.readback_buffer.mapped), pixel_count * 4u}, slot.pending->image_extent.width, slot.pending->image_extent.height, slot.pending->color_space);
            }
        } catch (const std::exception& error) {
            result.error_message = error.what();
        }
        slot.pending.reset();
        return result;
    }

    void EditorOutput::record_capture(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index, const RenderOutput render_output) {
        if (!this->capture.pending) return;
        CaptureFrameSlot& slot           = this->capture.slots[frame_slot_index];
        const CaptureFormat image_format = this->capture.pending->image_format;
        if (this->capture.pending->include_gbuffer) {
            slot.pending               = std::exchange(this->capture.pending, std::nullopt);
            slot.pending->image_extent = render_output.image.extent;
            return;
        }
        const vk::Extent2D extent           = image_format == CaptureFormat::Png ? this->context.viewport.target.image.extent : render_output.image.extent;
        const vk::DeviceSize required_size = static_cast<vk::DeviceSize>(extent.width) * extent.height * (image_format == CaptureFormat::Png ? 4u : sizeof(float) * 4u);
        if (slot.readback_buffer.size < required_size) slot.readback_buffer = this->context.runtime.resources.create_buffer(required_size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        slot.pending                               = std::exchange(this->capture.pending, std::nullopt);
        slot.pending->image_extent                 = extent;
        const vk::Image image                      = image_format == CaptureFormat::Png ? *this->context.viewport.target.image.image : *render_output.image.image;
        const vk::ImageLayout source_layout        = image_format == CaptureFormat::Png ? vk::ImageLayout::eShaderReadOnlyOptimal : render_output.image_layout;
        const vk::PipelineStageFlags2 source_stage = image_format == CaptureFormat::Png ? vk::PipelineStageFlagBits2::eFragmentShader : render_output.source_stage;
        const vk::AccessFlags2 source_access       = image_format == CaptureFormat::Png ? vk::AccessFlagBits2::eShaderSampledRead : render_output.source_access;
        const vk::ImageMemoryBarrier2 to_transfer{source_stage, source_access, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead, source_layout, vk::ImageLayout::eTransferSrcOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_transfer});
        command_buffer.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, *slot.readback_buffer.buffer, vk::BufferImageCopy{0, 0, 0, {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {0, 0, 0}, {extent.width, extent.height, 1}});
        const vk::ImageMemoryBarrier2 restore{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead, source_stage, source_access, vk::ImageLayout::eTransferSrcOptimal, source_layout, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        const vk::MemoryBarrier2 host_barrier{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &host_barrier, {}, {}, 1, &restore});
    }

    void EditorOutput::request_frozen_scene_export(const std::filesystem::path& path) {
        if (this->frozen_scene_export_in_progress()) throw std::runtime_error("A Frozen Scene export is already in progress");
        this->frozen_export.completed_result.reset();
        this->frozen_export.pending_request = path;
    }

    bool EditorOutput::frozen_scene_export_in_progress() const noexcept {
        if (this->frozen_export.pending_request) return true;
        if (std::ranges::any_of(this->frozen_export.slots, [](const FrozenSceneExportSlot& slot) { return slot.snapshot.has_value(); })) return true;
        return this->frozen_export.task.valid();
    }

    std::optional<EditorOutputResult> EditorOutput::take_frozen_scene_export_result() {
        if (this->frozen_export.task.valid() && this->frozen_export.task.wait_for(std::chrono::seconds{0}) == std::future_status::ready) this->frozen_export.completed_result = this->frozen_export.task.get();
        return std::exchange(this->frozen_export.completed_result, std::nullopt);
    }

    void EditorOutput::wait_for_frozen_scene_export() {
        for (FrozenSceneExportSlot& slot : this->frozen_export.slots)
            if (slot.snapshot) {
                slot.snapshot->materialize();
                if (this->frozen_export.task.valid()) this->frozen_export.task.get();
                this->frozen_export.task = std::async(std::launch::async, write_frozen_scene_package, std::move(slot.snapshot->frozen_scene), slot.output_path, slot.source_scene_path);
                slot.snapshot.reset();
            }
        if (this->frozen_export.task.valid()) this->frozen_export.completed_result = this->frozen_export.task.get();
    }

    std::optional<EditorOutputResult> EditorOutput::begin_frame(const std::uint32_t frame_slot_index) {
        FrozenSceneExportSlot& slot = this->frozen_export.slots[frame_slot_index];
        if (slot.snapshot) {
            slot.snapshot->materialize();
            this->frozen_export.task = std::async(std::launch::async, write_frozen_scene_package, std::move(slot.snapshot->frozen_scene), slot.output_path, slot.source_scene_path);
            slot.snapshot.reset();
            slot.output_path.clear();
            slot.source_scene_path.clear();
        }
        if (const std::optional<EditorOutputResult> capture_result = this->consume_capture(frame_slot_index)) return capture_result;
        return this->take_frozen_scene_export_result();
    }

    void EditorOutput::record_frozen_scene_snapshot(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index, const scene::Camera& camera, const vk::Extent2D extent, const std::filesystem::path& source_scene_path) {
        if (!this->frozen_export.pending_request) return;
        FrozenSceneExportSlot& slot = this->frozen_export.slots[frame_slot_index];
        slot.output_path            = *std::exchange(this->frozen_export.pending_request, std::nullopt);
        slot.source_scene_path      = source_scene_path;
        slot.snapshot               = this->context.gpu_scene.record_frozen_scene_snapshot(command_buffer, camera, extent, this->presenter.exposure);
    }
} // namespace spectra
