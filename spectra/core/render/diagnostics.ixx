export module spectra.diagnostics;

import spectra.scene;
import std;

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
        bool pivots{true};
        bool wireframe{};
        bool vertices{};
        bool normals{};
        bool tangents{};
        bool orientation{};
        bool cameras{true};
        bool camera_focal_plane{};
        bool camera_lens{};
        bool lights{true};
        bool area_emitters{true};
        bool volume_bounds{true};
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
} // namespace spectra
