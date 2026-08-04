export module spectra;

export import spectra.runtime;
export import spectra.scene;
export import spectra.scene.dynamics;
export import spectra.render;
export import spectra.editor;

import std;
import vulkan;

namespace spectra {
    export struct Spectra {
        Spectra(std::optional<std::filesystem::path> scene_path, const std::filesystem::path& scene_library_path, std::vector<std::filesystem::path> session_scene_roots, const std::filesystem::path& shader_directory, std::optional<std::string> initial_renderer);

        Spectra(const Spectra&)            = delete;
        Spectra(Spectra&&)                 = delete;
        Spectra& operator=(const Spectra&) = delete;
        Spectra& operator=(Spectra&&)      = delete;

        void run(std::optional<std::uint64_t> maximum_frame_count = std::nullopt);

        WindowPlatform platform;
        VulkanRuntime runtime;
        VulkanPresentation presentation;
        SceneDocument document;
        DynamicWorld dynamics;
        GpuScene gpu_scene;
        Renderers renderers;
        Editor editor;

    private:
        void prepare_rendering(const vk::raii::CommandBuffer& command_buffer, vk::Extent2D extent);
        void record_rendering(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        void record_editor_overlays(const vk::raii::CommandBuffer& command_buffer, bool show_axes);
        [[nodiscard]] RenderOutput current_render_output() const noexcept;
    };

    export struct RenderRequest {
        std::filesystem::path scene_path{};
        std::filesystem::path output_path{};
        std::string renderer{};
        std::optional<vk::Extent2D> resolution{};
        std::optional<std::uint32_t> samples_per_pixel{};
        std::optional<std::uint32_t> seed{};
        std::optional<std::uint64_t> simulation_step{};
        std::optional<std::filesystem::path> gbuffer_output_path{};
    };

    export void render_scene(const RenderRequest& request, const std::filesystem::path& shader_directory);
} // namespace spectra
