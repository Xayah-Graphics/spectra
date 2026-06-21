export module spectra.scene_runtime.plugin_conversion;

export import spectra.scene_runtime.contracts;
export import spectra.scene_runtime.plugin_c_abi;
import std;

namespace spectra::scene_runtime {
    export [[nodiscard]] std::string lowercase_ascii(std::string value);
    export [[nodiscard]] bool path_extension_is(const std::filesystem::path& path, std::string_view extension);
    export [[nodiscard]] std::string abi_string(const char* value, std::string_view context, bool allow_empty);
    export [[nodiscard]] double finite_double(double value, std::string_view context);
    export [[nodiscard]] std::set<std::string> collect_material_names(const scene::Scene::Document& document);
    export [[nodiscard]] std::set<std::string> collect_light_names(const scene::Scene::Document& document);
    export void append_document_view(scene::Scene::Document& document, const SpectraDynamicSceneDocumentView& view, std::set<std::string>& material_names, std::set<std::string>& light_names);
    export [[nodiscard]] scene::Scene::FrameSnapshot make_frame_snapshot(const SpectraDynamicSceneFrameView& view, const scene::Scene::FrameInfo& frame, const std::set<std::string>& material_names);
    export [[nodiscard]] std::vector<DynamicSceneOptionSchema> make_open_option_schemas(SpectraDynamicSceneOptionSchemaSpan schemas, std::string_view context);
    export [[nodiscard]] std::vector<DynamicSceneControlAction> make_control_actions(SpectraDynamicSceneControlActionSpan actions, std::string_view context);
    export [[nodiscard]] std::vector<DynamicSceneOptionSchema> make_control_setting_schemas(SpectraDynamicSceneOptionSchemaSpan schemas, std::string_view context);
    export [[nodiscard]] DynamicSceneControlSnapshot make_control_snapshot(const SpectraDynamicSceneControlSnapshotView& view, std::span<const DynamicSceneControlAction> actions, std::span<const DynamicSceneOptionSchema> setting_schemas, std::string_view context);
} // namespace spectra::scene_runtime
