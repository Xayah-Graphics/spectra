module spectra.editor;

import :diagnostics.settings;
import std;

namespace spectra {
    EditorSettings::EditorSettings(std::filesystem::path path) : path{std::move(path)} {
        if (!std::filesystem::exists(this->path)) return;
        std::ifstream stream{this->path};
        std::string key{};
        while (stream >> key) {
            if (key == "diagnostics.enabled") stream >> this->diagnostics.enabled;
            else if (key == "diagnostics.selected-bounds") stream >> this->diagnostics.selected_bounds;
            else if (key == "diagnostics.all-bounds") stream >> this->diagnostics.all_bounds;
            else if (key == "diagnostics.pivots") stream >> this->diagnostics.pivots;
            else if (key == "diagnostics.wireframe") stream >> this->diagnostics.wireframe;
            else if (key == "diagnostics.vertices") stream >> this->diagnostics.vertices;
            else if (key == "diagnostics.normals") stream >> this->diagnostics.normals;
            else if (key == "diagnostics.tangents") stream >> this->diagnostics.tangents;
            else if (key == "diagnostics.orientation") stream >> this->diagnostics.orientation;
            else if (key == "diagnostics.cameras") stream >> this->diagnostics.cameras;
            else if (key == "diagnostics.camera-focal-plane") stream >> this->diagnostics.camera_focal_plane;
            else if (key == "diagnostics.camera-lens") stream >> this->diagnostics.camera_lens;
            else if (key == "diagnostics.lights") stream >> this->diagnostics.lights;
            else if (key == "diagnostics.area-emitters") stream >> this->diagnostics.area_emitters;
            else if (key == "diagnostics.volume-bounds") stream >> this->diagnostics.volume_bounds;
            else if (key == "diagnostics.volume-grid") stream >> this->diagnostics.volume_grid;
            else if (key == "diagnostics.medium-boundaries") stream >> this->diagnostics.medium_boundaries;
            else if (key == "diagnostics.depth-mode") {
                std::uint32_t depth_mode{};
                stream >> depth_mode;
                this->diagnostics.depth_mode = static_cast<scene::VisualizationDepthMode>(depth_mode);
            } else if (key == "diagnostics.line-width") stream >> this->diagnostics.line_width;
            else if (key == "diagnostics.point-size") stream >> this->diagnostics.point_size;
            else if (key == "diagnostics.normal-scale") stream >> this->diagnostics.normal_scale;
            else if (key == "diagnostics.attribute-sampling") stream >> this->diagnostics.attribute_sampling;
            else if (key == "diagnostics.volume-grid-sampling") stream >> this->diagnostics.volume_grid_sampling;
            else throw std::runtime_error(std::format("Unknown editor setting: {}", key));
        }
    }

    void EditorSettings::save() const {
        std::ofstream stream{this->path, std::ios::trunc};
        const auto write = [&stream](const std::string_view key, const auto value) { stream << key << ' ' << value << '\n'; };
        write("diagnostics.enabled", this->diagnostics.enabled);
        write("diagnostics.selected-bounds", this->diagnostics.selected_bounds);
        write("diagnostics.all-bounds", this->diagnostics.all_bounds);
        write("diagnostics.pivots", this->diagnostics.pivots);
        write("diagnostics.wireframe", this->diagnostics.wireframe);
        write("diagnostics.vertices", this->diagnostics.vertices);
        write("diagnostics.normals", this->diagnostics.normals);
        write("diagnostics.tangents", this->diagnostics.tangents);
        write("diagnostics.orientation", this->diagnostics.orientation);
        write("diagnostics.cameras", this->diagnostics.cameras);
        write("diagnostics.camera-focal-plane", this->diagnostics.camera_focal_plane);
        write("diagnostics.camera-lens", this->diagnostics.camera_lens);
        write("diagnostics.lights", this->diagnostics.lights);
        write("diagnostics.area-emitters", this->diagnostics.area_emitters);
        write("diagnostics.volume-bounds", this->diagnostics.volume_bounds);
        write("diagnostics.volume-grid", this->diagnostics.volume_grid);
        write("diagnostics.medium-boundaries", this->diagnostics.medium_boundaries);
        write("diagnostics.depth-mode", static_cast<std::uint32_t>(std::to_underlying(this->diagnostics.depth_mode)));
        write("diagnostics.line-width", this->diagnostics.line_width);
        write("diagnostics.point-size", this->diagnostics.point_size);
        write("diagnostics.normal-scale", this->diagnostics.normal_scale);
        write("diagnostics.attribute-sampling", this->diagnostics.attribute_sampling);
        write("diagnostics.volume-grid-sampling", this->diagnostics.volume_grid_sampling);
    }
} // namespace spectra
