export module spectra.render.composition.diagnostics;

import spectra.render.contract;
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
        ParticleSet,
        Volume,
        NeuralField,
    };

    export struct SceneEntityReference {
        struct AreaEmitter {
            scene::LightId light{};
            scene::InstanceId instance{};
            std::uint32_t primitive_index{};

            friend auto operator<=>(const AreaEmitter&, const AreaEmitter&) = default;
        };

        std::variant<std::monostate, scene::InstanceId, scene::CameraId, scene::LightId, AreaEmitter, scene::ParticleSetId, scene::VolumeId, scene::NeuralFieldId> data{};

        SceneEntityReference() = default;

        template <typename Type>
        explicit SceneEntityReference(Type value) noexcept : data{value} {}

        friend auto operator<=>(const SceneEntityReference&, const SceneEntityReference&) = default;
    };

    export [[nodiscard]] SceneEntityKind scene_entity_kind(const SceneEntityReference& entity) noexcept;
    export [[nodiscard]] std::uint64_t scene_entity_id(const SceneEntityReference& entity) noexcept;

    export struct SelectionState {
        std::vector<SceneEntityReference> selected{};
        std::optional<SceneEntityReference> active{};
        std::optional<SceneEntityReference> hovered{};
    };

    export struct SceneGuideSettings {
        bool all_bounds{};
        bool cameras{};
        bool lights{};
    };

    export struct EntityDiagnostics {
        bool bounds{true};
        bool wireframe{};
        bool vertices{};
        bool normals{};
        bool tangents{};
        bool camera_frustum{true};
        bool camera_focal_plane{};
        bool camera_lens{};
        bool camera_gt_overlay{true};
        bool camera_gt_plane{};
        bool light_guide{true};
        bool area_emitter{};
        bool medium_boundary{};
        scene::VisualizationDepthMode depth_mode{scene::VisualizationDepthMode::Tested};
        float line_width{1.5f};
        float point_size{5.0f};
        float vector_scale{0.1f};
        std::uint32_t attribute_sampling{1};
    };

    export struct SceneDiagnosticRenderer {
        SceneDiagnosticRenderer(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory);
        ~SceneDiagnosticRenderer();

        SceneDiagnosticRenderer(const SceneDiagnosticRenderer&)            = delete;
        SceneDiagnosticRenderer(SceneDiagnosticRenderer&&)                 = delete;
        SceneDiagnosticRenderer& operator=(const SceneDiagnosticRenderer&) = delete;
        SceneDiagnosticRenderer& operator=(SceneDiagnosticRenderer&&)      = delete;

        void initialize();
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, ColorCompositionTarget target, DepthBufferView depth, scene::SceneView scene, const scene::Camera& camera, std::optional<scene::CameraId> scene_camera_view, const SceneGuideSettings& scene_guides, const EntityDiagnostics& entity_diagnostics, const SelectionState& selection, bool visible);
        [[nodiscard]] const GpuImage& pick_image() const noexcept;
        [[nodiscard]] std::optional<SceneEntityReference> pick_entity(std::uint32_t frame_slot_index, std::uint32_t pick_index) const noexcept;

    private:
        struct SceneDiagnosticFrameResources {
            GpuBuffer line_buffer{};
            GpuBuffer box_buffer{};
            GpuBuffer occupied_cell_buffer{};
            GpuBuffer occupancy_draw_buffer{};
            DescriptorLease line_descriptor{};
            DescriptorLease box_descriptor{};
            DescriptorLease occupied_cell_descriptor{};
            DescriptorLease occupancy_draw_descriptor{};
            std::size_t line_capacity{};
            std::size_t box_capacity{};
        };

        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            vk::raii::ShaderEXTs draw_shaders{nullptr};
            vk::raii::ShaderEXT occupancy_compaction_shader{nullptr};
            std::array<SceneDiagnosticFrameResources, VulkanFrames::frames_in_flight> frame_resources{};
            GpuImage pick_image{};
            vk::ImageLayout pick_layout{vk::ImageLayout::eUndefined};
            std::array<std::vector<SceneEntityReference>, VulkanFrames::frames_in_flight> pick_entities{};
            bool initialized{};
        } renderer;

        void ensure_buffers(SceneDiagnosticFrameResources& frame, std::size_t line_count, std::size_t box_count);
        void ensure_occupancy_buffers(SceneDiagnosticFrameResources& frame);
        void resize_pick_image(vk::Extent2D extent);
    };
} // namespace spectra
