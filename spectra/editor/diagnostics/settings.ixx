export module spectra.editor:diagnostics.settings;

import spectra.scene;
import std;

namespace spectra {
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

    export struct EditorSettings {
        explicit EditorSettings(std::filesystem::path path);

        void save() const;

        std::filesystem::path path{};
        SceneDiagnosticSettings diagnostics{};
    };
} // namespace spectra
