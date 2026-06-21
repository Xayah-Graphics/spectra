export module spectra.dynamic_scene.plugin_codec;

export import spectra.dynamic_scene.contracts;
export import spectra.dynamic_scene.plugin_abi;
import std;

namespace spectra::dynamic_scene {
    export class PluginAbiCodec final {
    public:
        struct Descriptor {
            std::string id{};
            std::string title{};
            std::string controls_panel_title{};
            std::string open_action_label{};
            std::string open_action_description{};
            std::string base_pbrt_path{};
            double frames_per_second{};
            std::vector<OptionSchema> open_options{};
            std::vector<ControlAction> control_actions{};
            std::vector<OptionSchema> control_settings{};
        };

        struct SceneSymbols {
            std::set<std::string> material_names{};
            std::set<std::string> light_names{};
        };

        [[nodiscard]] bool accepts_plugin_path(const std::filesystem::path& path) const;
        [[nodiscard]] Descriptor decode_descriptor(const SpectraPlugin& plugin) const;
        [[nodiscard]] std::string decode_last_error(const SpectraPlugin& plugin, SpectraInstance* instance, std::string_view action) const;
        [[nodiscard]] GpuBufferRequest decode_gpu_buffer_request(const SpectraGpuBufferRequest& request, std::string_view context) const;
        [[nodiscard]] SpectraGpuBufferAllocation encode_gpu_buffer_allocation(const GpuBufferAllocation& allocation) const;
        [[nodiscard]] SceneSymbols collect_scene_symbols(const scene::Scene::Document& document) const;
        void append_document(scene::Scene::Document& document, const SpectraDocumentView& view, SceneSymbols& symbols) const;
        [[nodiscard]] scene::Scene::FrameSnapshot decode_frame(const SpectraFrameView& view, const scene::Scene::FrameInfo& frame, const SceneSymbols& symbols) const;
        [[nodiscard]] ControlSnapshot decode_control_snapshot(const SpectraControlSnapshotView& view, std::span<const ControlAction> actions, std::span<const OptionSchema> setting_schemas, std::string_view context) const;
    };
} // namespace spectra::dynamic_scene
