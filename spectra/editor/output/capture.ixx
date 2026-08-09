export module spectra.editor:output.capture;

import spectra.display;
import spectra.render;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export enum class CaptureFormat : std::uint8_t {
        Png,
        LinearExr,
        GBufferExr,
    };

    export struct FrameCapture {
        struct PendingCapture {
            CaptureFormat image_format{CaptureFormat::Png};
            std::filesystem::path output_path{};
            vk::Extent2D image_extent{};
            bool include_gbuffer{};
            scene::SpectrumColorSpace color_space{scene::SpectrumColorSpace::Srgb};
            bool gbuffer_camera_space{true};
        };

        struct FrameSlot {
            GpuBuffer readback_buffer{};
            std::optional<PendingCapture> pending{};
        };

        FrameCapture(VulkanRuntime& runtime, Renderers& renderers, DisplayRenderer& display, std::filesystem::path output_directory) noexcept;

        void request(CaptureFormat image_format, const scene::Film& film, const std::filesystem::path& scene_path);
        [[nodiscard]] std::optional<std::expected<std::filesystem::path, std::string>> begin_frame(std::uint32_t frame_slot_index);
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, RenderOutput render_output);

        struct {
            VulkanRuntime& runtime;
            Renderers& renderers;
            DisplayRenderer& display;
        } context;

        struct {
            std::vector<FrameSlot> slots{};
            std::optional<PendingCapture> pending{};
        } capture;
        std::filesystem::path output_directory{};
    };
} // namespace spectra
