module;

#include <exr.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <Windows.h>

module spectra.app.image_io;

import std;

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

        void write_color_space(
            exr_header& header,
            const scene::SpectrumColorSpace color_space) {
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

        void write_exr(
            const std::filesystem::path& path,
            const std::uint32_t width,
            const std::uint32_t height,
            const scene::SpectrumColorSpace color_space,
            const std::span<const ExrChannelSource> channels,
            const std::optional<std::string_view> gbuffer_coordinates = std::nullopt,
            const std::optional<std::int32_t> accumulated_samples = std::nullopt) {
            std::vector<ExrChannelSource> sorted_channels{channels.begin(), channels.end()};
            std::ranges::sort(sorted_channels, {}, &ExrChannelSource::name);

            ExrPart part{};
            exr_header& header          = part.value.header;
            header.part_type            = EXR_PART_SCANLINE;
            header.compression          = EXR_COMPRESSION_ZIP;
            header.line_order           = EXR_LINEORDER_INCREASING_Y;
            header.data_window          = {0, 0, static_cast<std::int32_t>(width - 1u), static_cast<std::int32_t>(height - 1u)};
            header.display_window       = header.data_window;
            header.pixel_aspect_ratio   = 1.0f;
            header.screen_window_width  = 1.0f;
            header.num_channels         = static_cast<std::int32_t>(sorted_channels.size());
            header.channels             = static_cast<exr_channel*>(std::calloc(sorted_channels.size(), sizeof(exr_channel)));
            if (!header.channels) throw std::bad_alloc{};
            for (std::size_t index = 0; index != sorted_channels.size(); ++index) {
                const ExrChannelSource& source = sorted_channels[index];
                std::memcpy(header.channels[index].name, source.name.data(), source.name.size());
                header.channels[index].name[source.name.size()] = '\0';
                header.channels[index].pixel_type = source.type;
                header.channels[index].x_sampling = 1;
                header.channels[index].y_sampling = 1;
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
            const std::size_t block_capacity = static_cast<std::size_t>(width) * lines_per_block;
            std::vector<std::uint32_t> block_storage(sorted_channels.size() * block_capacity);
            std::vector<const void*> channel_rows(sorted_channels.size());
            for (std::uint32_t y = 0; y < height; y += lines_per_block) {
                const std::size_t block_size = static_cast<std::size_t>(width) * std::min(lines_per_block, height - y);
                for (std::size_t channel_index = 0; channel_index != sorted_channels.size(); ++channel_index) {
                    const ExrChannelSource& channel = sorted_channels[channel_index];
                    const std::byte* source = channel.data + static_cast<std::size_t>(y) * width * channel.pixel_stride + channel.component_offset;
                    std::uint32_t* destination = block_storage.data() + channel_index * block_capacity;
                    for (std::size_t pixel = 0; pixel != block_size; ++pixel)
                        std::memcpy(destination + pixel, source + pixel * channel.pixel_stride, sizeof(std::uint32_t));
                    channel_rows[channel_index] = destination;
                }
                check_exr(exr_writer_write_scanline_block(writer.value, 0, static_cast<std::int32_t>(y), channel_rows.data()), "write a scanline block");
            }
            check_exr(exr_writer_end_stream(writer.value), "finish the output file");
        }
    }

    void write_linear_exr(const std::filesystem::path& path, const std::span<const float> rgba, const std::uint32_t width, const std::uint32_t height, const scene::SpectrumColorSpace color_space) {
        constexpr std::size_t pixel_stride = sizeof(float) * 4;
        const std::byte* pixels = reinterpret_cast<const std::byte*>(rgba.data());
        const std::array channels{
            ExrChannelSource{"R", EXR_PIXEL_FLOAT, pixels, pixel_stride, 0},
            ExrChannelSource{"G", EXR_PIXEL_FLOAT, pixels, pixel_stride, sizeof(float)},
            ExrChannelSource{"B", EXR_PIXEL_FLOAT, pixels, pixel_stride, sizeof(float) * 2u},
            ExrChannelSource{"A", EXR_PIXEL_FLOAT, pixels, pixel_stride, sizeof(float) * 3u}};
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
        const std::byte* radiance         = reinterpret_cast<const std::byte*>(readback.radiance.data());
        const std::byte* albedo           = reinterpret_cast<const std::byte*>(readback.albedo.data());
        const std::byte* shading_normals  = reinterpret_cast<const std::byte*>(readback.shading_normals.data());
        const std::byte* geometric_normals = reinterpret_cast<const std::byte*>(readback.geometric_normals.data());
        const std::byte* positions        = reinterpret_cast<const std::byte*>(readback.positions.data());
        const std::byte* depths           = reinterpret_cast<const std::byte*>(readback.depths.data());
        const std::byte* texture_coordinates = reinterpret_cast<const std::byte*>(readback.texture_coordinates.data());
        const std::byte* object_ids       = reinterpret_cast<const std::byte*>(readback.object_ids.data());
        const std::byte* primitive_ids    = reinterpret_cast<const std::byte*>(readback.primitive_ids.data());
        const std::byte* material_ids     = reinterpret_cast<const std::byte*>(readback.material_ids.data());
        const std::array channels{
            ExrChannelSource{"R", EXR_PIXEL_FLOAT, radiance, sizeof(scene::Float4), 0},
            ExrChannelSource{"G", EXR_PIXEL_FLOAT, radiance, sizeof(scene::Float4), sizeof(float)},
            ExrChannelSource{"B", EXR_PIXEL_FLOAT, radiance, sizeof(scene::Float4), sizeof(float) * 2u},
            ExrChannelSource{"A", EXR_PIXEL_FLOAT, radiance, sizeof(scene::Float4), sizeof(float) * 3u},
            ExrChannelSource{"Albedo.R", EXR_PIXEL_FLOAT, albedo, sizeof(scene::Float3), 0},
            ExrChannelSource{"Albedo.G", EXR_PIXEL_FLOAT, albedo, sizeof(scene::Float3), sizeof(float)},
            ExrChannelSource{"Albedo.B", EXR_PIXEL_FLOAT, albedo, sizeof(scene::Float3), sizeof(float) * 2u},
            ExrChannelSource{"Nshading.X", EXR_PIXEL_FLOAT, shading_normals, sizeof(scene::Float3), 0},
            ExrChannelSource{"Nshading.Y", EXR_PIXEL_FLOAT, shading_normals, sizeof(scene::Float3), sizeof(float)},
            ExrChannelSource{"Nshading.Z", EXR_PIXEL_FLOAT, shading_normals, sizeof(scene::Float3), sizeof(float) * 2u},
            ExrChannelSource{"Ngeometric.X", EXR_PIXEL_FLOAT, geometric_normals, sizeof(scene::Float3), 0},
            ExrChannelSource{"Ngeometric.Y", EXR_PIXEL_FLOAT, geometric_normals, sizeof(scene::Float3), sizeof(float)},
            ExrChannelSource{"Ngeometric.Z", EXR_PIXEL_FLOAT, geometric_normals, sizeof(scene::Float3), sizeof(float) * 2u},
            ExrChannelSource{"P.X", EXR_PIXEL_FLOAT, positions, sizeof(scene::Float3), 0},
            ExrChannelSource{"P.Y", EXR_PIXEL_FLOAT, positions, sizeof(scene::Float3), sizeof(float)},
            ExrChannelSource{"P.Z", EXR_PIXEL_FLOAT, positions, sizeof(scene::Float3), sizeof(float) * 2u},
            ExrChannelSource{"Z", EXR_PIXEL_FLOAT, depths, sizeof(float), 0},
            ExrChannelSource{"UV.U", EXR_PIXEL_FLOAT, texture_coordinates, sizeof(scene::Float2), 0},
            ExrChannelSource{"UV.V", EXR_PIXEL_FLOAT, texture_coordinates, sizeof(scene::Float2), sizeof(float)},
            ExrChannelSource{"objectId.lo", EXR_PIXEL_UINT, object_ids, sizeof(std::uint64_t), 0},
            ExrChannelSource{"objectId.hi", EXR_PIXEL_UINT, object_ids, sizeof(std::uint64_t), sizeof(std::uint32_t)},
            ExrChannelSource{"primitiveId", EXR_PIXEL_UINT, primitive_ids, sizeof(std::uint32_t), 0},
            ExrChannelSource{"materialId.lo", EXR_PIXEL_UINT, material_ids, sizeof(std::uint64_t), 0},
            ExrChannelSource{"materialId.hi", EXR_PIXEL_UINT, material_ids, sizeof(std::uint64_t), sizeof(std::uint32_t)}};
        write_exr(path, readback.extent.width, readback.extent.height, color_space, channels, camera_space ? "camera" : "world", static_cast<std::int32_t>(readback.accumulated_samples));
    }
} // namespace spectra::app
