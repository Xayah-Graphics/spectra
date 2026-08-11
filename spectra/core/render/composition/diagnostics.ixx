export module spectra.render.composition.diagnostics;

import spectra.render.contract;
import spectra.render.display;
import spectra.render.gpu_scene;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export enum class SceneEntityKind : std::uint32_t {
        None,
        Instance,
        Camera,
        Light,
        AreaEmitter,
        Volume,
    };

    export struct SceneEntityReference {
        SceneEntityKind kind{SceneEntityKind::None};
        std::uint64_t id{};
        std::uint64_t owner{};
        std::uint32_t subindex{};

        friend auto operator<=>(const SceneEntityReference&, const SceneEntityReference&) = default;
    };

    export struct SelectionState {
        std::vector<SceneEntityReference> selected{};
        std::optional<SceneEntityReference> active{};
        std::optional<SceneEntityReference> hovered{};
    };

    export struct SceneDiagnosticSettings {
        bool enabled{true};
        bool selected_bounds{true};
        bool all_bounds{};
        bool pivots{};
        bool geometry_edges{};
        bool vertices{};
        bool normals{};
        bool tangents{};
        bool orientation{};
        bool cameras{};
        bool camera_focal_plane{};
        bool camera_lens{};
        bool lights{};
        bool area_emitters{};
        bool volume_bounds{};
        bool volume_grid{};
        bool medium_boundaries{};
        scene::VisualizationDepthMode depth_mode{scene::VisualizationDepthMode::Tested};
        float line_width{1.5f};
        float point_size{5.0f};
        float normal_scale{0.1f};
        std::uint32_t attribute_sampling{1};
        std::uint32_t volume_grid_sampling{8};

        friend auto operator<=>(const SceneDiagnosticSettings&, const SceneDiagnosticSettings&) = default;
    };

    export struct SceneDiagnosticFrameResources {
        GpuBuffer line_buffer{};
        GpuBuffer box_buffer{};
        DescriptorLease line_descriptor{};
        DescriptorLease box_descriptor{};
        std::size_t line_capacity{};
        std::size_t box_capacity{};
    };

    export struct SceneDiagnosticRenderer {
        SceneDiagnosticRenderer(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory);
        ~SceneDiagnosticRenderer();

        SceneDiagnosticRenderer(const SceneDiagnosticRenderer&)            = delete;
        SceneDiagnosticRenderer(SceneDiagnosticRenderer&&)                 = delete;
        SceneDiagnosticRenderer& operator=(const SceneDiagnosticRenderer&) = delete;
        SceneDiagnosticRenderer& operator=(SceneDiagnosticRenderer&&)      = delete;

        void initialize();
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, DisplayPass& display, DepthBufferView depth, scene::SceneView scene, const scene::Camera& camera, std::optional<scene::CameraId> scene_camera_view, const SceneDiagnosticSettings& settings, const SelectionState& selection);
        [[nodiscard]] const GpuImage& pick_image() const noexcept;
        [[nodiscard]] std::optional<SceneEntityReference> pick_entity(std::uint32_t frame_slot_index, std::uint32_t pick_index) const noexcept;

        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            vk::raii::ShaderEXTs draw_shaders{nullptr};
            std::array<SceneDiagnosticFrameResources, VulkanFrames::frames_in_flight> frame_resources{};
            GpuImage pick_image{};
            vk::ImageLayout pick_layout{vk::ImageLayout::eUndefined};
            std::array<std::vector<SceneEntityReference>, VulkanFrames::frames_in_flight> pick_entities{};
            bool initialized{};
        } renderer;

    private:
        void ensure_buffers(SceneDiagnosticFrameResources& frame, std::size_t line_count, std::size_t box_count);
        void resize_pick_image(vk::Extent2D extent);
    };
} // namespace spectra
