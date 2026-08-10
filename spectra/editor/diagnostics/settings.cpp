module spectra.editor;

import :diagnostics.settings;
import std;

namespace spectra {
    EditorSettings::EditorSettings(std::filesystem::path path) : path{std::move(path)} {
        if (!std::filesystem::exists(this->path)) return;
        std::ifstream stream{this->path};
        std::string name{};
        std::uint32_t version{};
        stream >> name >> version;
        auto read = [&stream](auto& value) {
            std::string key{};
            stream >> key >> value;
        };
        read(this->diagnostics.enabled);
        read(this->diagnostics.selected_bounds);
        read(this->diagnostics.all_bounds);
        read(this->diagnostics.pivots);
        read(this->diagnostics.wireframe);
        read(this->diagnostics.vertices);
        read(this->diagnostics.normals);
        read(this->diagnostics.tangents);
        read(this->diagnostics.orientation);
        read(this->diagnostics.cameras);
        read(this->diagnostics.camera_focal_plane);
        read(this->diagnostics.camera_lens);
        read(this->diagnostics.lights);
        read(this->diagnostics.area_emitters);
        read(this->diagnostics.volume_bounds);
        read(this->diagnostics.volume_grid);
        read(this->diagnostics.medium_boundaries);
        std::uint32_t depth_mode{};
        read(depth_mode);
        this->diagnostics.depth_mode = static_cast<scene::VisualizationDepthMode>(depth_mode);
        read(this->diagnostics.line_width);
        read(this->diagnostics.point_size);
        read(this->diagnostics.normal_scale);
        read(this->diagnostics.attribute_sampling);
        read(this->diagnostics.volume_grid_sampling);
    }

    void EditorSettings::save() const {
        std::ofstream stream{this->path, std::ios::trunc};
        stream << "spectra-editor-settings 1\n";
        const auto write = [&stream](const std::string_view key, const auto value) { stream << key << ' ' << value << '\n'; };
        write("enabled", this->diagnostics.enabled);
        write("selected-bounds", this->diagnostics.selected_bounds);
        write("all-bounds", this->diagnostics.all_bounds);
        write("pivots", this->diagnostics.pivots);
        write("wireframe", this->diagnostics.wireframe);
        write("vertices", this->diagnostics.vertices);
        write("normals", this->diagnostics.normals);
        write("tangents", this->diagnostics.tangents);
        write("orientation", this->diagnostics.orientation);
        write("cameras", this->diagnostics.cameras);
        write("camera-focal-plane", this->diagnostics.camera_focal_plane);
        write("camera-lens", this->diagnostics.camera_lens);
        write("lights", this->diagnostics.lights);
        write("area-emitters", this->diagnostics.area_emitters);
        write("volume-bounds", this->diagnostics.volume_bounds);
        write("volume-grid", this->diagnostics.volume_grid);
        write("medium-boundaries", this->diagnostics.medium_boundaries);
        write("depth-mode", std::to_underlying(this->diagnostics.depth_mode));
        write("line-width", this->diagnostics.line_width);
        write("point-size", this->diagnostics.point_size);
        write("normal-scale", this->diagnostics.normal_scale);
        write("attribute-sampling", this->diagnostics.attribute_sampling);
        write("volume-grid-sampling", this->diagnostics.volume_grid_sampling);
    }
} // namespace spectra
