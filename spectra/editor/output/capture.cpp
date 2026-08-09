module spectra.editor;

import :output.capture;
import std;
import vulkan;

namespace spectra {
    FrameCapture::FrameCapture(VulkanRuntime& runtime, Renderers& renderers, DisplayRenderer& display, std::filesystem::path output_directory) noexcept : context{runtime, renderers, display}, output_directory{std::move(output_directory)} {
        this->capture.slots.resize(VulkanFrames::frames_in_flight);
    }

    void FrameCapture::request(const CaptureFormat image_format, const scene::Film& film, const std::filesystem::path& scene_path) {
        if (image_format == CaptureFormat::GBufferExr && (!this->context.renderers.gbuffer_available() || !film.gbuffer)) throw std::runtime_error("GBuffer EXR capture requires an active GBuffer Film");
        const std::chrono::sys_time<std::chrono::milliseconds> now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
        const std::chrono::sys_seconds second                      = std::chrono::floor<std::chrono::seconds>(now);
        const std::string timestamp                                = std::format("{:%Y-%m-%d_%H-%M-%S}-{:03}Z", second, (now - second).count());
        const std::string_view renderer_name                       = this->context.renderers.active_descriptor().id;
        const std::string_view extension                           = image_format == CaptureFormat::Png ? "png" : "exr";
        const std::filesystem::path directory                      = this->output_directory / "captures" / scene_path.stem();
        std::filesystem::create_directories(directory);
        this->capture.pending = PendingCapture{
            image_format,
            directory / std::format("{}-{}.{}", renderer_name, timestamp, extension),
            {},
            image_format == CaptureFormat::GBufferExr,
            film.color_space,
            film.gbuffer_camera_space,
        };
    }

    std::optional<std::expected<std::filesystem::path, std::string>> FrameCapture::begin_frame(const std::uint32_t frame_slot_index) {
        FrameSlot& slot = this->capture.slots[frame_slot_index];
        if (!slot.pending) return std::nullopt;
        const std::filesystem::path output_path = slot.pending->output_path;
        try {
            if (slot.pending->include_gbuffer) {
                write_gbuffer_exr(output_path, this->context.renderers.readback(), slot.pending->color_space, slot.pending->gbuffer_camera_space);
            } else if (slot.pending->image_format == CaptureFormat::Png) {
                const std::size_t pixel_count = static_cast<std::size_t>(slot.pending->image_extent.width) * slot.pending->image_extent.height;
                write_png(output_path, std::span{static_cast<const std::uint8_t*>(slot.readback_buffer.mapped), pixel_count * 4u}, slot.pending->image_extent);
            } else {
                const std::size_t pixel_count = static_cast<std::size_t>(slot.pending->image_extent.width) * slot.pending->image_extent.height;
                write_linear_exr(output_path, std::span{static_cast<const float*>(slot.readback_buffer.mapped), pixel_count * 4u}, slot.pending->image_extent, slot.pending->color_space);
            }
        } catch (const std::exception& error) {
            slot.pending.reset();
            return std::expected<std::filesystem::path, std::string>{std::unexpected{std::string{error.what()}}};
        }
        slot.pending.reset();
        return std::expected<std::filesystem::path, std::string>{output_path};
    }

    void FrameCapture::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index, const RenderOutput render_output) {
        if (!this->capture.pending) return;
        FrameSlot& slot                  = this->capture.slots[frame_slot_index];
        const CaptureFormat image_format = this->capture.pending->image_format;
        if (this->capture.pending->include_gbuffer) {
            slot.pending               = std::exchange(this->capture.pending, std::nullopt);
            slot.pending->image_extent = render_output.image.extent;
            return;
        }
        if (image_format == CaptureFormat::LinearExr) {
            record_linear_readback(this->context.runtime, command_buffer, render_output, slot.readback_buffer);
            slot.pending               = std::exchange(this->capture.pending, std::nullopt);
            slot.pending->image_extent = render_output.image.extent;
            return;
        }
        record_display_readback(this->context.runtime, command_buffer, this->context.display.image, this->context.display.layout, slot.readback_buffer);
        slot.pending               = std::exchange(this->capture.pending, std::nullopt);
        slot.pending->image_extent = this->context.display.image.extent;
    }
} // namespace spectra
