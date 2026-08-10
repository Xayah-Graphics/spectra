export module spectra.editor:diagnostics.renderer;

import :diagnostics.settings;
import :viewport.interaction;
import spectra.render.contract;
import spectra.render.display;
import spectra.render.scene;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export struct SceneDiagnosticFrameResources {
        GpuBuffer line_buffer{};
        GpuBuffer box_buffer{};
        GpuBuffer bounds_buffer{};
        DescriptorHandle line_descriptor{};
        DescriptorHandle box_descriptor{};
        DescriptorHandle bounds_descriptor{};
        std::size_t line_capacity{};
        std::size_t box_capacity{};
        std::size_t bounds_capacity{};
    };

    export struct SceneDiagnosticRenderer {
        SceneDiagnosticRenderer(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory);
        ~SceneDiagnosticRenderer();

        SceneDiagnosticRenderer(const SceneDiagnosticRenderer&)            = delete;
        SceneDiagnosticRenderer(SceneDiagnosticRenderer&&)                 = delete;
        SceneDiagnosticRenderer& operator=(const SceneDiagnosticRenderer&) = delete;
        SceneDiagnosticRenderer& operator=(SceneDiagnosticRenderer&&)      = delete;

        void initialize();
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, DisplayPass& display, DepthBufferView depth, scene::SceneView scene, const scene::Camera& camera, const SceneDiagnosticSettings& settings, const SelectionState& selection);
        [[nodiscard]] const GpuImage& pick_image() const noexcept;
        [[nodiscard]] std::optional<SceneEntityReference> pick_entity(std::uint32_t frame_slot_index, std::uint32_t pick_index) const noexcept;

        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            vk::raii::ShaderEXTs draw_shaders{nullptr};
            vk::raii::ShaderEXT clear_bounds_shader{nullptr};
            vk::raii::ShaderEXT accumulate_bounds_shader{nullptr};
            std::array<SceneDiagnosticFrameResources, VulkanFrames::frames_in_flight> frame_resources{};
            GpuImage pick_image{};
            vk::ImageLayout pick_layout{vk::ImageLayout::eUndefined};
            std::array<std::vector<SceneEntityReference>, VulkanFrames::frames_in_flight> pick_entities{};
            bool initialized{};
        } renderer;

    private:
        void ensure_buffers(SceneDiagnosticFrameResources& frame, std::size_t line_count, std::size_t box_count, std::size_t bounds_count);
        void resize_pick_image(vk::Extent2D extent);
        void record_bounds(const vk::raii::CommandBuffer& command_buffer, SceneDiagnosticFrameResources& frame, scene::SceneView scene, std::size_t instance_count);
    };
} // namespace spectra
