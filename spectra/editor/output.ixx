export module spectra.editor:output;

import :viewport;
import spectra.runtime;
import spectra.scene;
import spectra.render;
import std;
import vulkan;

namespace spectra {
    export struct EditorOutputResult {
        std::filesystem::path output_path{};
        std::string error_message{};
    };

    export struct EditorOutput {
        struct PendingCapture {
            CaptureFormat image_format{CaptureFormat::Png};
            std::filesystem::path output_path{};
            vk::Extent2D image_extent{};
            bool include_gbuffer{};
            scene::SpectrumColorSpace color_space{scene::SpectrumColorSpace::Srgb};
            bool gbuffer_camera_space{true};
        };

        struct CaptureFrameSlot {
            GpuBuffer readback_buffer{};
            std::optional<PendingCapture> pending{};
        };

        struct FrozenSceneExportSlot {
            std::optional<FrozenSceneSnapshot> snapshot{};
            std::filesystem::path output_path{};
            std::filesystem::path source_scene_path{};
        };

        EditorOutput(VulkanRuntime& runtime, GpuScene& gpu_scene, Renderers& renderers, EditorViewport& viewport, std::filesystem::path shader_directory) noexcept;
        ~EditorOutput();

        EditorOutput(const EditorOutput&)            = delete;
        EditorOutput(EditorOutput&&)                 = delete;
        EditorOutput& operator=(const EditorOutput&) = delete;
        EditorOutput& operator=(EditorOutput&&)      = delete;

        void initialize();
        void record_presenter(const vk::raii::CommandBuffer& command_buffer, RenderOutput render_output);
        void request_capture(CaptureFormat image_format, const scene::Film& film);
        [[nodiscard]] std::optional<EditorOutputResult> consume_capture(std::uint32_t frame_slot_index);
        void record_capture(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, RenderOutput render_output);
        void request_frozen_scene_export(const std::filesystem::path& path);
        [[nodiscard]] bool frozen_scene_export_in_progress() const noexcept;
        [[nodiscard]] std::optional<EditorOutputResult> take_frozen_scene_export_result();
        void wait_for_frozen_scene_export();
        [[nodiscard]] std::optional<EditorOutputResult> begin_frame(std::uint32_t frame_slot_index);
        void record_frozen_scene_snapshot(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, const scene::Camera& camera, vk::Extent2D extent, const std::filesystem::path& source_scene_path);

        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            Renderers& renderers;
            EditorViewport& viewport;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            vk::raii::ShaderEXTs shaders{nullptr};
            DescriptorHandle sampler_descriptor{};
            float exposure{};
        } presenter;

        struct {
            std::vector<CaptureFrameSlot> slots{};
            std::optional<PendingCapture> pending{};
        } capture;

        struct {
            std::vector<FrozenSceneExportSlot> slots{};
            std::optional<std::filesystem::path> pending_request{};
            std::future<EditorOutputResult> task{};
            std::optional<EditorOutputResult> completed_result{};
        } frozen_export;

    private:
        [[nodiscard]] static std::filesystem::path pictures_directory();
        void record_presenter_impl(const vk::raii::CommandBuffer& command_buffer, RenderOutput render_output, vk::Image target_image, vk::ImageView target_view, vk::Extent2D extent, vk::ImageLayout target_layout, vk::ImageLayout final_layout, float exposure);
    };
} // namespace spectra
