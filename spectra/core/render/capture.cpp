module;

#include <exr.h>

#undef interface

module spectra.render.capture;

import std;
import vulkan;

namespace spectra {
    namespace {
        void append_u32_be(std::vector<std::byte>& destination, const std::uint32_t value) {
            destination.emplace_back(static_cast<std::byte>(value >> 24u));
            destination.emplace_back(static_cast<std::byte>(value >> 16u));
            destination.emplace_back(static_cast<std::byte>(value >> 8u));
            destination.emplace_back(static_cast<std::byte>(value));
        }

        [[nodiscard]] std::uint32_t png_crc(const std::span<const std::byte> bytes) noexcept {
            std::uint32_t crc = 0xffffffffu;
            for (const std::byte value : bytes) {
                crc ^= std::to_integer<std::uint8_t>(value);
                for (std::uint32_t bit = 0; bit < 8; ++bit) crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
            }
            return ~crc;
        }

        void append_png_chunk(std::vector<std::byte>& png, const std::array<char, 4>& type, const std::span<const std::byte> data) {
            append_u32_be(png, static_cast<std::uint32_t>(data.size()));
            const std::size_t crc_begin = png.size();
            for (const char value : type) png.emplace_back(static_cast<std::byte>(value));
            png.insert(png.end(), data.begin(), data.end());
            append_u32_be(png, png_crc(std::span{png}.subspan(crc_begin)));
        }

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

    void write_png(const std::filesystem::path& path, const std::span<const std::uint8_t> bgra, const vk::Extent2D extent) {
        std::vector<std::byte> scanlines((static_cast<std::size_t>(extent.width) * 4u + 1u) * extent.height);
        for (std::uint32_t y = 0; y < extent.height; ++y) {
            const std::size_t row = static_cast<std::size_t>(y) * (static_cast<std::size_t>(extent.width) * 4u + 1u);
            scanlines[row]        = std::byte{};
            for (std::uint32_t x = 0; x < extent.width; ++x) {
                const std::size_t source      = (static_cast<std::size_t>(y) * extent.width + x) * 4u;
                const std::size_t destination = row + 1u + static_cast<std::size_t>(x) * 4u;
                scanlines[destination + 0u]   = static_cast<std::byte>(bgra[source + 2u]);
                scanlines[destination + 1u]   = static_cast<std::byte>(bgra[source + 1u]);
                scanlines[destination + 2u]   = static_cast<std::byte>(bgra[source + 0u]);
                scanlines[destination + 3u]   = static_cast<std::byte>(bgra[source + 3u]);
            }
        }

        std::vector<std::byte> compressed{std::byte{0x78}, std::byte{0x01}};
        std::size_t offset{};
        while (offset < scanlines.size()) {
            const std::uint16_t size = static_cast<std::uint16_t>(std::min<std::size_t>(scanlines.size() - offset, 65535u));
            compressed.emplace_back(offset + size == scanlines.size() ? std::byte{0x01} : std::byte{});
            compressed.emplace_back(static_cast<std::byte>(size));
            compressed.emplace_back(static_cast<std::byte>(size >> 8u));
            compressed.emplace_back(static_cast<std::byte>(~size));
            compressed.emplace_back(static_cast<std::byte>(static_cast<std::uint16_t>(~size) >> 8u));
            compressed.insert(compressed.end(), scanlines.begin() + offset, scanlines.begin() + offset + size);
            offset += size;
        }
        std::uint32_t first = 1;
        std::uint32_t second{};
        for (const std::byte value : scanlines) {
            first  = (first + std::to_integer<std::uint8_t>(value)) % 65521u;
            second = (second + first) % 65521u;
        }
        append_u32_be(compressed, second << 16u | first);

        std::vector<std::byte> png{std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47}, std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a}};
        std::array<std::byte, 13> header{};
        header[0] = static_cast<std::byte>(extent.width >> 24u);
        header[1] = static_cast<std::byte>(extent.width >> 16u);
        header[2] = static_cast<std::byte>(extent.width >> 8u);
        header[3] = static_cast<std::byte>(extent.width);
        header[4] = static_cast<std::byte>(extent.height >> 24u);
        header[5] = static_cast<std::byte>(extent.height >> 16u);
        header[6] = static_cast<std::byte>(extent.height >> 8u);
        header[7] = static_cast<std::byte>(extent.height);
        header[8] = std::byte{8};
        header[9] = std::byte{6};
        append_png_chunk(png, {'I', 'H', 'D', 'R'}, header);
        append_png_chunk(png, {'I', 'D', 'A', 'T'}, compressed);
        append_png_chunk(png, {'I', 'E', 'N', 'D'}, std::span<const std::byte>{});
        std::ofstream stream{path, std::ios::binary};
        stream.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        if (!stream) throw std::runtime_error(std::format("Failed to write PNG file: {}", path.string()));
    }

    void record_linear_readback(VulkanRuntime& runtime, const vk::raii::CommandBuffer& command_buffer, const RenderOutput render_output, GpuBuffer& readback_buffer) {
        const vk::Extent2D extent          = render_output.image.extent;
        const vk::DeviceSize required_size = static_cast<vk::DeviceSize>(extent.width) * extent.height * sizeof(float) * 4u;
        if (readback_buffer.size < required_size) readback_buffer = runtime.resources.create_buffer(required_size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        const vk::ImageMemoryBarrier2 to_transfer{render_output.source_stage, render_output.source_access, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead, render_output.image_layout, vk::ImageLayout::eTransferSrcOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *render_output.image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_transfer});
        command_buffer.copyImageToBuffer(*render_output.image.image, vk::ImageLayout::eTransferSrcOptimal, *readback_buffer.buffer, vk::BufferImageCopy{0, 0, 0, {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {0, 0, 0}, {extent.width, extent.height, 1}});
        const vk::ImageMemoryBarrier2 restore{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead, render_output.source_stage, render_output.source_access, vk::ImageLayout::eTransferSrcOptimal, render_output.image_layout, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *render_output.image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        const vk::MemoryBarrier2 host_barrier{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &host_barrier, {}, {}, 1, &restore});
    }

    void record_display_readback(VulkanRuntime& runtime, const vk::raii::CommandBuffer& command_buffer, const GpuImage& image, const vk::ImageLayout image_layout, GpuBuffer& readback_buffer) {
        const vk::DeviceSize required_size = static_cast<vk::DeviceSize>(image.extent.width) * image.extent.height * 4u;
        if (readback_buffer.size < required_size) readback_buffer = runtime.resources.create_buffer(required_size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        const vk::PipelineStageFlags2 source_stage = image_layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::PipelineStageFlagBits2::eFragmentShader : vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        const vk::AccessFlags2 source_access       = image_layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::AccessFlagBits2::eShaderSampledRead : vk::AccessFlagBits2::eColorAttachmentWrite;
        const vk::ImageMemoryBarrier2 to_transfer{source_stage, source_access, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead, image_layout, vk::ImageLayout::eTransferSrcOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_transfer});
        command_buffer.copyImageToBuffer(*image.image, vk::ImageLayout::eTransferSrcOptimal, *readback_buffer.buffer, vk::BufferImageCopy{0, 0, 0, {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {0, 0, 0}, {image.extent.width, image.extent.height, 1}});
        const vk::ImageMemoryBarrier2 restore{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead, source_stage, source_access, vk::ImageLayout::eTransferSrcOptimal, image_layout, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        const vk::MemoryBarrier2 host_barrier{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &host_barrier, {}, {}, 1, &restore});
    }

    void write_linear_exr(const std::filesystem::path& path, const std::span<const float> rgba, const vk::Extent2D extent, const scene::SpectrumColorSpace color_space) {
        constexpr std::size_t pixel_stride = sizeof(float) * 4;
        const std::byte* pixels            = reinterpret_cast<const std::byte*>(rgba.data());
        const std::array channels{ExrChannelSource{"R", EXR_PIXEL_FLOAT, pixels, pixel_stride, 0}, ExrChannelSource{"G", EXR_PIXEL_FLOAT, pixels, pixel_stride, sizeof(float)}, ExrChannelSource{"B", EXR_PIXEL_FLOAT, pixels, pixel_stride, sizeof(float) * 2u}, ExrChannelSource{"A", EXR_PIXEL_FLOAT, pixels, pixel_stride, sizeof(float) * 3u}};
        write_exr(path, extent.width, extent.height, color_space, channels);
    }

    void write_gbuffer_exr(const std::filesystem::path& path, const RenderGBufferReadback& readback, const scene::SpectrumColorSpace color_space, const bool camera_space) {
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
        write_exr(path, readback.extent.width, readback.extent.height, color_space, channels, camera_space ? "camera" : "world", static_cast<std::int32_t>(readback.accumulated_samples));
    }
} // namespace spectra
