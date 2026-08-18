export module spectra.render.display.diagnostics;

import spectra.render.display.types;
import spectra.render.types;
import spectra.render.gpu_scene;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

export namespace spectra::render {
    struct DiagnosticsPass {
        DiagnosticsPass(runtime::VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory);
        ~DiagnosticsPass();

        DiagnosticsPass(const DiagnosticsPass&)            = delete;
        DiagnosticsPass(DiagnosticsPass&&)                 = delete;
        DiagnosticsPass& operator=(const DiagnosticsPass&) = delete;
        DiagnosticsPass& operator=(DiagnosticsPass&&)      = delete;

        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, ColorTarget target, DepthBufferView depth, scene::ResolvedSceneView scene, const scene::Camera& camera, std::optional<scene::CameraId> scene_camera_view, const DiagnosticRequest& request);
        [[nodiscard]] const runtime::GpuImage& pick_image() const noexcept;
        [[nodiscard]] std::optional<scene::EntityReference> pick_entity(std::uint32_t frame_slot_index, std::uint32_t pick_index) const noexcept;

    private:
        struct SceneDiagnosticFrameResources {
            runtime::GpuBuffer line_buffer{};
            runtime::GpuBuffer box_buffer{};
            runtime::GpuBuffer occupied_cell_buffer{};
            runtime::GpuBuffer occupancy_draw_buffer{};
            runtime::DescriptorLease line_descriptor{};
            runtime::DescriptorLease box_descriptor{};
            runtime::DescriptorLease occupied_cell_descriptor{};
            runtime::DescriptorLease occupancy_draw_descriptor{};
            std::size_t line_capacity{};
            std::size_t box_capacity{};
        };

        struct {
            runtime::VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            vk::raii::ShaderEXTs draw_shaders{nullptr};
            vk::raii::ShaderEXT occupancy_compaction_shader{nullptr};
            std::array<SceneDiagnosticFrameResources, runtime::VulkanFrames::frames_in_flight> frame_resources{};
            runtime::GpuImage pick_image{};
            vk::ImageLayout pick_layout{vk::ImageLayout::eUndefined};
            std::array<std::vector<scene::EntityReference>, runtime::VulkanFrames::frames_in_flight> pick_entities{};
        } renderer;

        void ensure_buffers(SceneDiagnosticFrameResources& frame, std::size_t line_count, std::size_t box_count);
        void ensure_occupancy_buffers(SceneDiagnosticFrameResources& frame);
        void resize_pick_image(vk::Extent2D extent);
    };
} // namespace spectra::render
