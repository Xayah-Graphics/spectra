export module spectra.dynamic_scene.plugin_decode;

export import spectra.dynamic_scene.contracts;
export import spectra.dynamic_scene.plugin_abi;
import std;

namespace spectra::dynamic_scene {
    export [[nodiscard]] bool plugin_file_extension_supported(const std::filesystem::path& path);
    export [[nodiscard]] std::string abi_string(const char* value, std::string_view context, bool allow_empty);
    export [[nodiscard]] double finite_double(double value, std::string_view context);
    export [[nodiscard]] std::set<std::string> collect_material_names(const scene::Scene::Document& document);
    export [[nodiscard]] std::set<std::string> collect_light_names(const scene::Scene::Document& document);
    export void append_document_view(scene::Scene::Document& document, const SpectraDocumentView& view, std::set<std::string>& material_names, std::set<std::string>& light_names);
    export [[nodiscard]] scene::Scene::FrameSnapshot make_frame_snapshot(const SpectraFrameView& view, const scene::Scene::FrameInfo& frame, const std::set<std::string>& material_names);
    export [[nodiscard]] std::vector<OptionSchema> make_open_option_schemas(SpectraOptionSchemaSpan schemas, std::string_view context);
    export [[nodiscard]] std::vector<ControlAction> make_control_actions(SpectraControlActionSpan actions, std::string_view context);
    export [[nodiscard]] std::vector<OptionSchema> make_control_setting_schemas(SpectraOptionSchemaSpan schemas, std::string_view context);
    export [[nodiscard]] ControlSnapshot make_control_snapshot(const SpectraControlSnapshotView& view, std::span<const ControlAction> actions, std::span<const OptionSchema> setting_schemas, std::string_view context);
} // namespace spectra::dynamic_scene
