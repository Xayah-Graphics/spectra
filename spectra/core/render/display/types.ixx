export module spectra.render.display.types;

import spectra.simulation.frame;
import spectra.render.types;
import spectra.runtime.resources;
import spectra.scene;
import std;
import vulkan;

namespace spectra::render {
    export struct ColorTarget {
        runtime::GpuImage& image;
        vk::ImageLayout& layout;
        scene::SpectrumColorSpace color_space{scene::SpectrumColorSpace::Srgb};
        runtime::DescriptorHandle storage_descriptor{};
    };

    export enum class AxesPlane : std::uint8_t {
        Xz,
        Xy,
        Yz,
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

    export struct DiagnosticSelection {
        std::span<const scene::EntityReference> selected{};
        std::optional<scene::EntityReference> active{};
        std::optional<scene::EntityReference> hovered{};
    };

    export struct DiagnosticRequest {
        SceneGuideSettings scene_guides{};
        EntityDiagnostics entity{};
        DiagnosticSelection selection{};
        bool visible{true};
    };

    export struct CameraReferenceRequest {
        const simulation::CameraReferenceImage* reference{};
        const scene::Camera* camera{};
        math::Float4 overlay_rect{};
        bool overlay{};
        bool plane{};
    };

    export struct OverlayRequest {
        std::span<const scene::InstanceId> selected_instances{};
        std::optional<scene::InstanceId> active_instance{};
        std::optional<scene::InstanceId> hovered_instance{};
        AxesPlane axes_plane{AxesPlane::Xz};
        bool axes_visible{};
        bool outline_visible{true};
    };

    export struct DisplayFeatures {
        bool diagnostics{true};
        bool visualizations{true};
        bool neural_fields{true};
        bool overlays{true};
    };

    export struct DisplayRequest {
        RenderOutput renderer_output;
        std::optional<DepthBufferView> depth{};
        scene::ResolvedSceneView scene;
        scene::Camera camera{};
        std::optional<scene::CameraId> scene_camera_view{};
        std::span<const simulation::GpuVisualization> visualizations{};
        std::optional<DiagnosticRequest> diagnostics{};
        std::optional<CameraReferenceRequest> camera_reference{};
        std::optional<OverlayRequest> overlay{};
        std::uint32_t frame_slot_index{};
        float exposure{};
    };
} // namespace spectra::render
