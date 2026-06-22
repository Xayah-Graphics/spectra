export module spectra.scene.plugin_codec;

import spectra.scene;
export import spectra.scene.plugin_abi;
import std;

namespace spectra::scene {
    export class PluginAbiCodec final {
    public:
        struct Descriptor {
            std::string id{};
            std::string title{};
            std::string open_action_label{};
            std::string open_action_description{};
            std::string base_pbrt_path{};
            double frames_per_second{};
            std::vector<ControlSection> sections{};
            std::vector<ControlOptionSchema> open_options{};
            std::vector<ControlAction> control_actions{};
            std::vector<ControlOptionSchema> control_settings{};
        };

        struct SceneSymbols {
            std::set<std::string> material_names{};
            std::set<std::string> light_names{};
        };

        [[nodiscard]] bool accepts_plugin_path(const std::filesystem::path& path) const;
        [[nodiscard]] Descriptor decode_descriptor(const SpectraScenePlugin& plugin) const;
        [[nodiscard]] std::string decode_last_error(const SpectraScenePlugin& plugin, SpectraSceneInstance* instance, std::string_view action) const;
        [[nodiscard]] GpuBufferRequest decode_gpu_buffer_request(const SpectraSceneGpuBufferRequest& request, std::string_view context) const;
        [[nodiscard]] SpectraSceneGpuBufferAllocation encode_gpu_buffer_allocation(const GpuBufferAllocation& allocation) const;
        [[nodiscard]] SceneSymbols collect_scene_symbols(const scene::Scene::Document& document) const;
        void append_document(scene::Scene::Document& document, const SpectraSceneDocumentView& view, SceneSymbols& symbols) const;
        [[nodiscard]] scene::Scene::FrameSnapshot decode_frame(const SpectraSceneFrameView& view, const scene::Scene::FrameInfo& frame, const SceneSymbols& symbols) const;
        [[nodiscard]] ControlState decode_control_state(const SpectraSceneControlStateView& view, std::span<const ControlSection> sections, std::span<const ControlAction> actions, std::string_view context) const;
    };
} // namespace spectra::scene
