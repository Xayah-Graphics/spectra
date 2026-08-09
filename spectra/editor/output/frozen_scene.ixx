export module spectra.editor:output.frozen_scene;

import spectra.render;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export struct FrozenSceneExporter {
        struct FrameSlot {
            std::optional<FrozenSceneSnapshot> snapshot{};
            std::filesystem::path output_path{};
            std::filesystem::path source_scene_path{};
        };

        FrozenSceneExporter(GpuScene& gpu_scene) noexcept;
        ~FrozenSceneExporter();

        FrozenSceneExporter(const FrozenSceneExporter&)            = delete;
        FrozenSceneExporter(FrozenSceneExporter&&)                 = delete;
        FrozenSceneExporter& operator=(const FrozenSceneExporter&) = delete;
        FrozenSceneExporter& operator=(FrozenSceneExporter&&)      = delete;

        void request(const std::filesystem::path& path);
        [[nodiscard]] bool in_progress() const noexcept;
        [[nodiscard]] std::optional<std::expected<std::filesystem::path, std::string>> begin_frame(std::uint32_t frame_slot_index);
        void record_snapshot(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, const scene::Camera& camera, vk::Extent2D extent, float exposure, const std::filesystem::path& source_scene_path);
        void wait();

        GpuScene& gpu_scene;

        struct {
            std::vector<FrameSlot> slots{};
            std::optional<std::filesystem::path> pending_request{};
            std::future<std::expected<std::filesystem::path, std::string>> task{};
            std::optional<std::expected<std::filesystem::path, std::string>> completed_result{};
        } export_state;

    private:
        [[nodiscard]] std::optional<std::expected<std::filesystem::path, std::string>> take_result();
    };
} // namespace spectra
