module;

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

module spectra.rasterizer.scene_runtime;

import std;
import spectra.scene;

namespace spectra::rasterizer {
    namespace {
        void commit_scene_frame(scene::Scene& workspace, scene::Scene::FrameSnapshot frame) {
            scene::Scene::Edit edit{};
            edit.replace_frame(std::move(frame));
            const scene::Scene::DirtyFlags dirty = workspace.commit(std::move(edit));
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Frame)) throw std::runtime_error("Scene frame commit did not mark the frame dirty");
        }

        void commit_scene_timeline(scene::Scene& workspace, scene::Scene::Timeline timeline) {
            scene::Scene::Edit edit{};
            edit.replace_timeline(std::move(timeline));
            const scene::Scene::DirtyFlags dirty = workspace.commit(std::move(edit));
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Timeline)) throw std::runtime_error("Scene timeline commit did not mark the timeline dirty");
        }

        void commit_scene_timeline_and_frame(scene::Scene& workspace, scene::Scene::Timeline timeline, scene::Scene::FrameSnapshot frame) {
            scene::Scene::Edit edit{};
            edit.replace_timeline(std::move(timeline));
            edit.replace_frame(std::move(frame));
            const scene::Scene::DirtyFlags dirty = workspace.commit(std::move(edit));
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Timeline)) throw std::runtime_error("Scene timeline commit did not mark the timeline dirty");
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Frame)) throw std::runtime_error("Scene frame commit did not mark the frame dirty");
        }
    } // namespace

    SceneEntry::SceneEntry(std::string id, std::string title, const SceneEntryKind kind, std::move_only_function<std::shared_ptr<scene::Scene>()> create_static_scene, std::move_only_function<std::unique_ptr<DynamicSceneSourceInstance>()> create_dynamic_source) : id(std::move(id)), title(std::move(title)), kind(kind), create_static_scene(std::move(create_static_scene)), create_dynamic_source(std::move(create_dynamic_source)) {}

    std::size_t SceneRegistry::upsert_static_scene(std::string id, std::string title, std::move_only_function<std::shared_ptr<scene::Scene>()> create_scene) {
        if (!create_scene) throw std::runtime_error("Static scene entry requires a scene factory");
        if (id.empty()) throw std::runtime_error("Scene registry entry id must not be empty");
        if (std::optional<std::size_t> existing_index = this->find_entry_index(id)) {
            SceneEntry& entry = this->entries.at(*existing_index);
            if (entry.kind != SceneEntryKind::Static) throw std::runtime_error("Cannot replace dynamic scene source with a static scene: " + id);
            entry.title = std::move(title);
            entry.create_static_scene = std::move(create_scene);
            return *existing_index;
        }
        this->entries.push_back(SceneEntry{std::move(id), std::move(title), SceneEntryKind::Static, std::move(create_scene), std::move_only_function<std::unique_ptr<DynamicSceneSourceInstance>()>{}});
        return this->entries.size() - 1u;
    }

    std::size_t SceneRegistry::upsert_dynamic_source(std::string id, std::string title, std::move_only_function<std::unique_ptr<DynamicSceneSourceInstance>()> create_source) {
        if (!create_source) throw std::runtime_error("Dynamic scene entry requires a source factory");
        if (id.empty()) throw std::runtime_error("Scene registry entry id must not be empty");
        if (title.empty()) throw std::runtime_error("Dynamic scene entry title must not be empty");
        if (std::optional<std::size_t> existing_index = this->find_entry_index(id)) {
            SceneEntry& entry = this->entries.at(*existing_index);
            if (entry.kind != SceneEntryKind::Dynamic) throw std::runtime_error("Cannot replace static scene with a dynamic scene source: " + id);
            entry.title = std::move(title);
            entry.create_dynamic_source = std::move(create_source);
            return *existing_index;
        }
        this->entries.push_back(SceneEntry{std::move(id), std::move(title), SceneEntryKind::Dynamic, std::move_only_function<std::shared_ptr<scene::Scene>()>{}, std::move(create_source)});
        return this->entries.size() - 1u;
    }

    std::unique_ptr<DynamicSceneSourceInstance> SceneRegistry::create_dynamic_source(const std::size_t index) {
        if (this->entries.empty()) throw std::runtime_error("Scene registry is empty");
        if (index >= this->entries.size()) throw std::runtime_error("Scene registry index is out of range");
        SceneEntry& source = this->entries.at(index);
        if (source.kind != SceneEntryKind::Dynamic) throw std::runtime_error("Scene registry entry is not dynamic");
        if (!source.create_dynamic_source) throw std::runtime_error("Dynamic scene source entry has no source factory");
        return source.create_dynamic_source();
    }

    std::shared_ptr<scene::Scene> SceneRegistry::create_static_scene(const std::size_t index) {
        if (this->entries.empty()) throw std::runtime_error("Scene registry is empty");
        if (index >= this->entries.size()) throw std::runtime_error("Scene registry index is out of range");
        SceneEntry& source = this->entries.at(index);
        if (source.kind != SceneEntryKind::Static) throw std::runtime_error("Scene registry entry is not static");
        if (!source.create_static_scene) throw std::runtime_error("Static scene entry has no scene factory");
        std::shared_ptr<scene::Scene> scene = source.create_static_scene();
        if (scene == nullptr) throw std::runtime_error("Static scene factory returned null");
        return scene;
    }

    std::optional<std::size_t> SceneRegistry::find_entry_index(const std::string_view id) const {
        for (std::size_t index = 0; index < this->entries.size(); ++index)
            if (this->entries.at(index).id == id) return index;
        return std::nullopt;
    }

    const SceneEntry& SceneRegistry::entry(const std::size_t index) const {
        if (index >= this->entries.size()) throw std::runtime_error("Scene registry index is out of range");
        return this->entries.at(index);
    }

    std::size_t SceneRegistry::size() const {
        return this->entries.size();
    }

    SceneController::SceneController(SceneRegistry registry, std::shared_ptr<scene::Scene> empty_workspace) : registry(std::move(registry)), empty_workspace(std::move(empty_workspace)) {
        if (this->empty_workspace == nullptr) throw std::runtime_error("Scene controller requires an empty Untitled workspace");
        this->slots.resize(this->registry.size());
    }

    std::shared_ptr<scene::Scene> SceneController::active_workspace() {
        if (!this->selected_entry_index.has_value()) return this->empty_workspace;
        return this->ensure_slot(*this->selected_entry_index).workspace;
    }

    const SceneEntry& SceneController::entry(const std::size_t index) const {
        return this->registry.entry(index);
    }

    std::size_t SceneController::size() const {
        return this->registry.size();
    }

    bool SceneController::has_selected_entry() const {
        return this->selected_entry_index.has_value();
    }

    std::size_t SceneController::selected_index() const {
        if (this->pending_selected_entry_index.has_value()) return *this->pending_selected_entry_index;
        if (this->selected_entry_index.has_value()) return *this->selected_entry_index;
        throw std::runtime_error("Scene controller has no selected scene entry");
    }

    bool SceneController::pending_switch() const {
        return this->pending_selected_entry_index.has_value() && (!this->selected_entry_index.has_value() || *this->pending_selected_entry_index != *this->selected_entry_index);
    }

    bool SceneController::has_activation_error() const {
        return !this->activation_error_message.empty();
    }

    const std::string& SceneController::activation_error() const {
        return this->activation_error_message;
    }

    void SceneController::clear_activation_error() {
        this->activation_error_message.clear();
    }

    void SceneController::request_activate(const std::size_t index) {
        this->sync_slot_count();
        if (index >= this->slots.size()) throw std::runtime_error("Scene activation index is out of range");
        this->clear_activation_error();
        if (this->selected_entry_index.has_value() && index == *this->selected_entry_index) {
            this->pending_selected_entry_index.reset();
            return;
        }
        this->pending_selected_entry_index = index;
    }

    bool SceneController::activate_static_scene(std::string id, std::string title, std::move_only_function<std::shared_ptr<scene::Scene>()> load_scene) {
        try {
            if (!load_scene) throw std::runtime_error("Static scene load request requires a scene factory");
            std::shared_ptr<scene::Scene> scene = load_scene();
            if (scene == nullptr) throw std::runtime_error("Static scene factory returned null");
            const scene::Scene::Info scene_info = scene->info();
            if (title.empty()) title = scene_info.title;
            if (title.empty()) throw std::runtime_error("Static scene title must not be empty");
            std::shared_ptr<scene::Scene> cached_scene = scene;
            const std::size_t index = this->registry.upsert_static_scene(std::move(id), std::move(title), [cached_scene = std::move(cached_scene)] { return cached_scene; });
            this->sync_slot_count();
            this->set_static_slot(index, std::move(scene));
            this->selected_entry_index = index;
            this->pending_selected_entry_index.reset();
            this->clear_activation_error();
            return true;
        } catch (const std::exception& error) {
            this->activation_error_message = std::format("Failed to load static scene: {}", error.what());
            return false;
        }
    }

    bool SceneController::activate_dynamic_scene(std::string id, std::string title, std::move_only_function<std::unique_ptr<DynamicSceneSourceInstance>()> create_source) {
        try {
            if (!create_source) throw std::runtime_error("Dynamic scene activation requires a source factory");
            if (id.empty()) throw std::runtime_error("Dynamic scene id must not be empty");
            std::unique_ptr<DynamicSceneSourceInstance> source = create_source();
            if (source == nullptr) throw std::runtime_error("Dynamic scene source factory returned null");
            source->reset();
            scene::Scene::Document document = source->create_scene_document();
            if (!document.timeline_enabled) throw std::runtime_error("Dynamic scene source document must enable timeline");
            if (!std::isfinite(document.frames_per_second) || document.frames_per_second <= 0.0) throw std::runtime_error("Dynamic scene source document frame rate must be finite and positive");
            if (title.empty()) title = document.title;
            if (title.empty()) throw std::runtime_error("Dynamic scene title must not be empty");
            std::shared_ptr<scene::Scene> workspace = std::make_shared<scene::Scene>(std::move(document));
            scene::Scene::FrameSnapshot snapshot = source->create_scene_frame(scene::Scene::FrameInfo{
                .delta_seconds = 0.0,
                .time_seconds  = 0.0,
                .frame_index   = 0u,
            });
            const std::shared_ptr<const scene::Scene::Document> scene_document = workspace->document();
            scene::Scene::Timeline timeline{
                .mode                 = scene::Scene::TimelineMode::Live,
                .frames_per_second    = scene_document->frames_per_second,
                .playing              = true,
                .selected_frame_index = 0,
            };
            commit_scene_timeline_and_frame(*workspace, std::move(timeline), std::move(snapshot));
            static_cast<void>(workspace->resolved_frame());
            const std::size_t index = this->registry.upsert_dynamic_source(std::move(id), std::move(title), std::move(create_source));
            this->sync_slot_count();
            this->set_dynamic_slot(index, std::move(source), std::move(workspace));
            this->selected_entry_index = index;
            this->pending_selected_entry_index.reset();
            this->clear_activation_error();
            return true;
        } catch (const std::exception& error) {
            this->activation_error_message = std::format("Failed to load dynamic scene: {}", error.what());
            return false;
        }
    }

    bool SceneController::apply_pending_scene() {
        if (!this->pending_selected_entry_index.has_value()) return false;
        const std::size_t next_index = *this->pending_selected_entry_index;
        this->pending_selected_entry_index.reset();
        if (next_index >= this->slots.size()) throw std::runtime_error("Pending scene index is out of range");
        if (this->selected_entry_index.has_value() && next_index == *this->selected_entry_index) return false;
        try {
            static_cast<void>(this->ensure_slot(next_index));
        } catch (const std::exception& error) {
            const SceneEntry& source = this->registry.entry(next_index);
            this->activation_error_message = std::format("Failed to load scene \"{}\": {}", source.title, error.what());
            return false;
        }
        this->selected_entry_index = next_index;
        this->clear_activation_error();
        return true;
    }

    void SceneController::update_active_scene(const double delta_seconds) {
        if (!this->selected_entry_index.has_value()) return;
        const SceneEntry& source = this->registry.entry(*this->selected_entry_index);
        if (source.kind == SceneEntryKind::Static) return;
        SceneSlot& slot = this->ensure_slot(*this->selected_entry_index);
        if (slot.source == nullptr) throw std::runtime_error("Dynamic scene slot has no source instance");
        const std::shared_ptr<const scene::Scene::Document> document = slot.workspace->document();
        if (!document->timeline_enabled) throw std::runtime_error("Dynamic scene source must enable timeline");
        scene::Scene::Timeline timeline = slot.workspace->timeline();
        if (timeline.frames_per_second <= 0.0) throw std::runtime_error("Dynamic scene timeline frame rate must be positive");
        const double fixed_delta_seconds = 1.0 / timeline.frames_per_second;
        if (timeline.reset_request_serial != slot.observed_reset_request_serial) {
            this->reset_dynamic_scene(slot, std::move(timeline));
            slot.observed_reset_request_serial = slot.workspace->timeline().reset_request_serial;
            slot.committed_playback_frame_index.reset();
            return;
        }
        if (timeline.clear_recording_request_serial != slot.observed_clear_recording_request_serial) {
            timeline.recorded_frames.clear();
            timeline.selected_frame_index = 0;
            commit_scene_timeline(*slot.workspace, std::move(timeline));
            slot.observed_clear_recording_request_serial = slot.workspace->timeline().clear_recording_request_serial;
            slot.committed_playback_frame_index.reset();
            return;
        }
        if (timeline.mode == scene::Scene::TimelineMode::Playback) {
            if (timeline.recorded_frames.empty()) return;
            if (timeline.selected_frame_index >= timeline.recorded_frames.size()) throw std::runtime_error("Dynamic scene playback selected frame is out of range");
            if (slot.committed_playback_frame_index.has_value() && *slot.committed_playback_frame_index == timeline.selected_frame_index) return;
            scene::Scene::FrameSnapshot selected_frame = timeline.recorded_frames.at(timeline.selected_frame_index);
            commit_scene_frame(*slot.workspace, std::move(selected_frame));
            slot.committed_playback_frame_index = timeline.selected_frame_index;
            return;
        }
        slot.committed_playback_frame_index.reset();
        if (!timeline.playing) return;
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0) throw std::runtime_error("Dynamic scene frame delta time is invalid");
        slot.frame_accumulator_seconds += delta_seconds;
        bool advanced = false;
        scene::Scene::FrameSnapshot snapshot{};
        while (slot.frame_accumulator_seconds >= fixed_delta_seconds) {
            slot.frame_accumulator_seconds -= fixed_delta_seconds;
            ++slot.stream_frame_index;
            slot.stream_time_seconds += fixed_delta_seconds;
            slot.source->step(static_cast<float>(fixed_delta_seconds));
            snapshot = slot.source->create_scene_frame(scene::Scene::FrameInfo{
                .delta_seconds = fixed_delta_seconds,
                .time_seconds  = slot.stream_time_seconds,
                .frame_index   = slot.stream_frame_index,
            });
            advanced = true;
        }
        if (!advanced) return;
        if (timeline.mode == scene::Scene::TimelineMode::Record) {
            timeline.recorded_frames.push_back(snapshot);
            timeline.selected_frame_index = timeline.recorded_frames.size() - 1u;
            commit_scene_timeline_and_frame(*slot.workspace, std::move(timeline), std::move(snapshot));
            return;
        }
        commit_scene_frame(*slot.workspace, std::move(snapshot));
    }

    void SceneController::sync_slot_count() {
        if (this->slots.size() == this->registry.size()) return;
        if (this->slots.size() > this->registry.size()) throw std::runtime_error("Scene slot count cannot exceed registry size");
        this->slots.resize(this->registry.size());
    }

    void SceneController::set_static_slot(const std::size_t index, std::shared_ptr<scene::Scene> scene) {
        if (scene == nullptr) throw std::runtime_error("Static scene slot requires a scene");
        if (index >= this->slots.size()) throw std::runtime_error("Static scene slot index is out of range");
        SceneSlot& slot = this->slots.at(index);
        slot.source.reset();
        slot.workspace = std::move(scene);
        slot.frame_accumulator_seconds = 0.0;
        slot.stream_time_seconds = 0.0;
        slot.stream_frame_index = 0;
        slot.observed_reset_request_serial = 0;
        slot.observed_clear_recording_request_serial = 0;
        slot.committed_playback_frame_index.reset();
    }

    void SceneController::set_dynamic_slot(const std::size_t index, std::unique_ptr<DynamicSceneSourceInstance> source, std::shared_ptr<scene::Scene> workspace) {
        if (source == nullptr) throw std::runtime_error("Dynamic scene slot requires a source instance");
        if (workspace == nullptr) throw std::runtime_error("Dynamic scene slot requires a scene workspace");
        if (index >= this->slots.size()) throw std::runtime_error("Dynamic scene slot index is out of range");
        SceneSlot& slot = this->slots.at(index);
        slot.source = std::move(source);
        slot.workspace = std::move(workspace);
        slot.frame_accumulator_seconds = 0.0;
        slot.stream_time_seconds = 0.0;
        slot.stream_frame_index = 0;
        slot.observed_reset_request_serial = 0;
        slot.observed_clear_recording_request_serial = 0;
        slot.committed_playback_frame_index.reset();
    }

    SceneController::SceneSlot& SceneController::ensure_slot(const std::size_t index) {
        this->sync_slot_count();
        if (index >= this->slots.size()) throw std::runtime_error("Scene slot index is out of range");
        SceneSlot& slot = this->slots.at(index);
        const SceneEntry& source = this->registry.entry(index);
        if (slot.workspace != nullptr) {
            if (source.kind == SceneEntryKind::Dynamic && slot.source == nullptr) throw std::runtime_error("Dynamic scene slot has a workspace but no source instance");
            return slot;
        }
        if (source.kind == SceneEntryKind::Static) {
            slot.workspace = this->registry.create_static_scene(index);
            return slot;
        }
        scene::Scene::Document document = this->create_dynamic_slot(index, &slot);
        slot.workspace = std::make_shared<scene::Scene>(std::move(document));
        if (slot.source == nullptr) throw std::runtime_error("Dynamic scene slot was not initialized");
        scene::Scene::FrameSnapshot snapshot = slot.source->create_scene_frame(scene::Scene::FrameInfo{
            .delta_seconds = 0.0,
            .time_seconds  = 0.0,
            .frame_index   = 0u,
        });
        const std::shared_ptr<const scene::Scene::Document> scene_document = slot.workspace->document();
        if (!scene_document->timeline_enabled) throw std::runtime_error("Dynamic scene source must enable timeline");
        scene::Scene::Timeline timeline{
            .mode                 = scene::Scene::TimelineMode::Live,
            .frames_per_second    = scene_document->frames_per_second,
            .playing              = true,
            .selected_frame_index = 0,
        };
        commit_scene_timeline_and_frame(*slot.workspace, std::move(timeline), std::move(snapshot));
        return slot;
    }

    scene::Scene::Document SceneController::create_dynamic_slot(const std::size_t index, SceneSlot* slot) {
        if (slot == nullptr) throw std::runtime_error("Dynamic scene slot pointer must not be null");
        slot->source = this->registry.create_dynamic_source(index);
        slot->source->reset();
        scene::Scene::Document document = slot->source->create_scene_document();
        if (!document.timeline_enabled) throw std::runtime_error("Dynamic scene source document must enable timeline");
        return document;
    }

    void SceneController::reset_dynamic_scene(SceneSlot& slot, scene::Scene::Timeline timeline) {
        slot.frame_accumulator_seconds = 0.0;
        slot.stream_time_seconds = 0.0;
        slot.stream_frame_index = 0;
        slot.source->reset();
        scene::Scene::FrameSnapshot snapshot = slot.source->create_scene_frame(scene::Scene::FrameInfo{
            .delta_seconds = 0.0,
            .time_seconds  = 0.0,
            .frame_index   = 0u,
        });
        timeline.selected_frame_index = 0;
        commit_scene_timeline_and_frame(*slot.workspace, std::move(timeline), std::move(snapshot));
    }

    namespace {
        constexpr std::uint32_t plugin_abi_version = 1u;

        struct SpectraDynamicSceneInstance;

        enum SpectraDynamicSceneResult {
            SPECTRA_DYNAMIC_SCENE_RESULT_OK = 0,
            SPECTRA_DYNAMIC_SCENE_RESULT_ERROR = 1,
        };

        struct SpectraDynamicSceneString {
            const char* data;
            std::uint64_t size;
        };

        struct SpectraDynamicSceneStringSpan {
            const SpectraDynamicSceneString* data;
            std::uint64_t count;
        };

        struct SpectraDynamicSceneTransform {
            float position[3];
            float rotation[4];
            float scale[3];
        };

        struct SpectraDynamicSceneMaterial {
            SpectraDynamicSceneString name;
            SpectraDynamicSceneString model;
            SpectraDynamicSceneString alpha_mode;
            float base_color[4];
            float emission_color[3];
            float emission_strength;
            float roughness;
            float metallic;
            float alpha_cutoff;
            float volume_density_scale;
            float volume_temperature_scale;
        };

        struct SpectraDynamicSceneMaterialSpan {
            const SpectraDynamicSceneMaterial* data;
            std::uint64_t count;
        };

        struct SpectraDynamicSceneLight {
            SpectraDynamicSceneString name;
            SpectraDynamicSceneString kind;
            SpectraDynamicSceneTransform transform;
            float color[3];
            float intensity;
            float cone_angle_degrees;
        };

        struct SpectraDynamicSceneLightSpan {
            const SpectraDynamicSceneLight* data;
            std::uint64_t count;
        };

        struct SpectraDynamicSceneCamera {
            SpectraDynamicSceneString name;
            SpectraDynamicSceneTransform transform;
            float target[3];
            float up[3];
            float vertical_fov_degrees;
            float near_plane;
            float far_plane;
        };

        struct SpectraDynamicSceneMeshVertex {
            float position[3];
            float normal[3];
        };

        struct SpectraDynamicSceneMeshVertexSpan {
            const SpectraDynamicSceneMeshVertex* data;
            std::uint64_t count;
        };

        struct SpectraDynamicSceneUInt32Span {
            const std::uint32_t* data;
            std::uint64_t count;
        };

        struct SpectraDynamicSceneMesh {
            SpectraDynamicSceneString name;
            SpectraDynamicSceneMeshVertexSpan vertices;
            SpectraDynamicSceneUInt32Span indices;
            SpectraDynamicSceneString material_name;
            SpectraDynamicSceneTransform transform;
        };

        struct SpectraDynamicSceneMeshSpan {
            const SpectraDynamicSceneMesh* data;
            std::uint64_t count;
        };

        struct SpectraDynamicSceneSphere {
            SpectraDynamicSceneString name;
            float radius;
            SpectraDynamicSceneString material_name;
            SpectraDynamicSceneTransform transform;
        };

        struct SpectraDynamicSceneSphereSpan {
            const SpectraDynamicSceneSphere* data;
            std::uint64_t count;
        };

        struct SpectraDynamicScenePoint {
            float position[3];
            float normal[3];
            float color[4];
            float radius;
        };

        struct SpectraDynamicScenePointSpan {
            const SpectraDynamicScenePoint* data;
            std::uint64_t count;
        };

        struct SpectraDynamicScenePointCloud {
            SpectraDynamicSceneString name;
            SpectraDynamicScenePointSpan points;
            SpectraDynamicSceneString material_name;
            SpectraDynamicSceneTransform transform;
        };

        struct SpectraDynamicScenePointCloudSpan {
            const SpectraDynamicScenePointCloud* data;
            std::uint64_t count;
        };

        struct SpectraDynamicSceneFloatSpan {
            const float* data;
            std::uint64_t count;
        };

        struct SpectraDynamicSceneVolumeChannel {
            SpectraDynamicSceneString name;
            std::uint32_t dimensions[3];
            SpectraDynamicSceneFloatSpan values;
        };

        struct SpectraDynamicSceneVolumeChannelSpan {
            const SpectraDynamicSceneVolumeChannel* data;
            std::uint64_t count;
        };

        struct SpectraDynamicSceneVolume {
            SpectraDynamicSceneString name;
            std::uint32_t dimensions[3];
            float origin[3];
            float voxel_size[3];
            SpectraDynamicSceneVolumeChannelSpan channels;
            SpectraDynamicSceneString material_name;
        };

        struct SpectraDynamicSceneVolumeSpan {
            const SpectraDynamicSceneVolume* data;
            std::uint64_t count;
        };

        struct SpectraDynamicSceneDocumentView {
            std::uint64_t struct_size;
            std::uint32_t has_camera;
            SpectraDynamicSceneCamera camera;
            SpectraDynamicSceneMaterialSpan materials;
            SpectraDynamicSceneLightSpan lights;
            SpectraDynamicSceneMeshSpan meshes;
            SpectraDynamicSceneSphereSpan spheres;
            SpectraDynamicScenePointCloudSpan point_clouds;
            SpectraDynamicSceneVolumeSpan volumes;
        };

        struct SpectraDynamicSceneFrameInfo {
            double delta_seconds;
            double time_seconds;
            std::uint64_t frame_index;
        };

        struct SpectraDynamicSceneFrameView {
            std::uint64_t struct_size;
            SpectraDynamicSceneMeshSpan meshes;
            SpectraDynamicSceneSphereSpan spheres;
            SpectraDynamicScenePointCloudSpan point_clouds;
            SpectraDynamicSceneVolumeSpan volumes;
        };

        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneCreateFn)(SpectraDynamicSceneInstance** instance);
        typedef void (*SpectraDynamicSceneDestroyFn)(SpectraDynamicSceneInstance* instance);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneResetFn)(SpectraDynamicSceneInstance* instance);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneStepFn)(SpectraDynamicSceneInstance* instance, float delta_seconds);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneDocumentFn)(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneDocumentView* document);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneFrameFn)(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneFrameInfo frame, SpectraDynamicSceneFrameView* snapshot);
        typedef SpectraDynamicSceneString (*SpectraDynamicSceneLastErrorFn)(SpectraDynamicSceneInstance* instance);

        struct SpectraDynamicScenePlugin {
            std::uint32_t abi_version;
            std::uint64_t struct_size;
            SpectraDynamicSceneString id;
            SpectraDynamicSceneString title;
            SpectraDynamicSceneString pbrt_template_path;
            double frames_per_second;
            SpectraDynamicSceneCreateFn create;
            SpectraDynamicSceneDestroyFn destroy;
            SpectraDynamicSceneResetFn reset;
            SpectraDynamicSceneStepFn step;
            SpectraDynamicSceneDocumentFn document;
            SpectraDynamicSceneFrameFn frame;
            SpectraDynamicSceneLastErrorFn last_error;
        };

        typedef const SpectraDynamicScenePlugin* (*SpectraDynamicScenePluginEntryFn)(void);

        [[nodiscard]] std::string lowercase_ascii(std::string value) {
            for (char& character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return value;
        }

        [[nodiscard]] bool path_extension_is(const std::filesystem::path& path, const std::string_view extension) {
            return lowercase_ascii(path.extension().string()) == lowercase_ascii(std::string{extension});
        }

        [[nodiscard]] std::string_view abi_string_view(const SpectraDynamicSceneString value, const std::string_view context) {
            if (value.data == nullptr && value.size == 0u) return {};
            if (value.data == nullptr) throw std::runtime_error(std::format("{} string pointer is null", context));
            if (value.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) throw std::runtime_error(std::format("{} string is too large", context));
            return std::string_view{value.data, static_cast<std::size_t>(value.size)};
        }

        [[nodiscard]] std::string abi_string(const SpectraDynamicSceneString value, const std::string_view context, const bool allow_empty) {
            const std::string_view view = abi_string_view(value, context);
            if (!allow_empty && view.empty()) throw std::runtime_error(std::format("{} must not be empty", context));
            return std::string{view};
        }

        template <typename Value>
        [[nodiscard]] std::span<const Value> abi_span(const Value* data, const std::uint64_t count, const std::string_view context) {
            if (count == 0u) return {};
            if (data == nullptr) throw std::runtime_error(std::format("{} data pointer is null", context));
            if (count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) throw std::runtime_error(std::format("{} item count is too large", context));
            return std::span<const Value>{data, static_cast<std::size_t>(count)};
        }

        [[nodiscard]] std::span<const SpectraDynamicSceneMaterial> abi_span(const SpectraDynamicSceneMaterialSpan span, const std::string_view context) {
            return abi_span(span.data, span.count, context);
        }

        [[nodiscard]] std::span<const SpectraDynamicSceneLight> abi_span(const SpectraDynamicSceneLightSpan span, const std::string_view context) {
            return abi_span(span.data, span.count, context);
        }

        [[nodiscard]] std::span<const SpectraDynamicSceneMesh> abi_span(const SpectraDynamicSceneMeshSpan span, const std::string_view context) {
            return abi_span(span.data, span.count, context);
        }

        [[nodiscard]] std::span<const SpectraDynamicSceneSphere> abi_span(const SpectraDynamicSceneSphereSpan span, const std::string_view context) {
            return abi_span(span.data, span.count, context);
        }

        [[nodiscard]] std::span<const SpectraDynamicScenePointCloud> abi_span(const SpectraDynamicScenePointCloudSpan span, const std::string_view context) {
            return abi_span(span.data, span.count, context);
        }

        [[nodiscard]] std::span<const SpectraDynamicSceneVolume> abi_span(const SpectraDynamicSceneVolumeSpan span, const std::string_view context) {
            return abi_span(span.data, span.count, context);
        }

        [[nodiscard]] float finite_float(const float value, const std::string_view context) {
            if (!std::isfinite(value)) throw std::runtime_error(std::format("{} must be finite", context));
            return value;
        }

        [[nodiscard]] double finite_double(const double value, const std::string_view context) {
            if (!std::isfinite(value)) throw std::runtime_error(std::format("{} must be finite", context));
            return value;
        }

        [[nodiscard]] scene::Vector3 make_vector3(const float (&value)[3], const std::string_view context) {
            return scene::Vector3{
                finite_float(value[0], std::format("{} x", context)),
                finite_float(value[1], std::format("{} y", context)),
                finite_float(value[2], std::format("{} z", context)),
            };
        }

        [[nodiscard]] scene::Vector4 make_vector4(const float (&value)[4], const std::string_view context) {
            return scene::Vector4{
                finite_float(value[0], std::format("{} x", context)),
                finite_float(value[1], std::format("{} y", context)),
                finite_float(value[2], std::format("{} z", context)),
                finite_float(value[3], std::format("{} w", context)),
            };
        }

        [[nodiscard]] scene::Transform make_transform(const SpectraDynamicSceneTransform& transform, const std::string_view context) {
            return scene::Transform{
                .position = make_vector3(transform.position, std::format("{} position", context)),
                .rotation = scene::Quaternion{
                    finite_float(transform.rotation[0], std::format("{} rotation x", context)),
                    finite_float(transform.rotation[1], std::format("{} rotation y", context)),
                    finite_float(transform.rotation[2], std::format("{} rotation z", context)),
                    finite_float(transform.rotation[3], std::format("{} rotation w", context)),
                },
                .scale = make_vector3(transform.scale, std::format("{} scale", context)),
            };
        }

        [[nodiscard]] scene::Scene::PreviewSurfaceKind preview_surface_kind_from_string(const std::string_view value, const std::string_view material_name) {
            if (value == "lit_surface") return scene::Scene::PreviewSurfaceKind::LitSurface;
            if (value == "unlit_surface") return scene::Scene::PreviewSurfaceKind::UnlitSurface;
            if (value == "emissive_surface") return scene::Scene::PreviewSurfaceKind::EmissiveSurface;
            if (value == "volume") return scene::Scene::PreviewSurfaceKind::Volume;
            if (value == "point_sprite") return scene::Scene::PreviewSurfaceKind::PointGlyph;
            throw std::runtime_error(std::format("Dynamic scene material \"{}\" has invalid preview surface kind \"{}\"", material_name, value));
        }

        [[nodiscard]] scene::Scene::PreviewAlphaMode preview_alpha_mode_from_string(const std::string_view value, const std::string_view material_name) {
            if (value == "opaque") return scene::Scene::PreviewAlphaMode::Opaque;
            if (value == "masked") return scene::Scene::PreviewAlphaMode::Masked;
            if (value == "blend") return scene::Scene::PreviewAlphaMode::Blend;
            throw std::runtime_error(std::format("Dynamic scene material \"{}\" has invalid alpha mode \"{}\"", material_name, value));
        }

        [[nodiscard]] scene::Scene::PreviewLightKind light_kind_from_string(const std::string_view value, const std::string_view light_name) {
            if (value == "directional") return scene::Scene::PreviewLightKind::Directional;
            if (value == "point") return scene::Scene::PreviewLightKind::Point;
            if (value == "spot") return scene::Scene::PreviewLightKind::Spot;
            if (value == "area") return scene::Scene::PreviewLightKind::Area;
            if (value == "environment") return scene::Scene::PreviewLightKind::Environment;
            throw std::runtime_error(std::format("Dynamic scene light \"{}\" has invalid kind \"{}\"", light_name, value));
        }

        [[nodiscard]] scene::Scene::PreviewMaterial make_material(const SpectraDynamicSceneMaterial& material) {
            const std::string name = abi_string(material.name, "Dynamic scene material name", false);
            return scene::Scene::PreviewMaterial{
                .name = name,
                .surface_kind = preview_surface_kind_from_string(abi_string_view(material.model, std::format("Dynamic scene material \"{}\" model", name)), name),
                .alpha_mode = preview_alpha_mode_from_string(abi_string_view(material.alpha_mode, std::format("Dynamic scene material \"{}\" alpha mode", name)), name),
                .base_color = make_vector4(material.base_color, std::format("Dynamic scene material \"{}\" base color", name)),
                .emission_color = make_vector3(material.emission_color, std::format("Dynamic scene material \"{}\" emission color", name)),
                .emission_strength = finite_float(material.emission_strength, std::format("Dynamic scene material \"{}\" emission strength", name)),
                .roughness = finite_float(material.roughness, std::format("Dynamic scene material \"{}\" roughness", name)),
                .metallic = finite_float(material.metallic, std::format("Dynamic scene material \"{}\" metallic", name)),
                .alpha_cutoff = finite_float(material.alpha_cutoff, std::format("Dynamic scene material \"{}\" alpha cutoff", name)),
                .volume_density_scale = finite_float(material.volume_density_scale, std::format("Dynamic scene material \"{}\" volume density scale", name)),
                .volume_temperature_scale = finite_float(material.volume_temperature_scale, std::format("Dynamic scene material \"{}\" volume temperature scale", name)),
            };
        }

        [[nodiscard]] scene::Scene::PreviewLight make_light(const SpectraDynamicSceneLight& light) {
            const std::string name = abi_string(light.name, "Dynamic scene light name", false);
            return scene::Scene::PreviewLight{
                .name = name,
                .kind = light_kind_from_string(abi_string_view(light.kind, std::format("Dynamic scene light \"{}\" kind", name)), name),
                .transform = make_transform(light.transform, std::format("Dynamic scene light \"{}\"", name)),
                .color = make_vector3(light.color, std::format("Dynamic scene light \"{}\" color", name)),
                .intensity = finite_float(light.intensity, std::format("Dynamic scene light \"{}\" intensity", name)),
                .cone_angle_degrees = finite_float(light.cone_angle_degrees, std::format("Dynamic scene light \"{}\" cone angle", name)),
            };
        }

        [[nodiscard]] scene::Scene::Camera make_camera(const SpectraDynamicSceneCamera& camera) {
            const std::string name = abi_string(camera.name, "Dynamic scene camera name", false);
            return scene::Scene::Camera{
                .name = name,
                .transform = make_transform(camera.transform, std::format("Dynamic scene camera \"{}\"", name)),
                .target = make_vector3(camera.target, std::format("Dynamic scene camera \"{}\" target", name)),
                .up = make_vector3(camera.up, std::format("Dynamic scene camera \"{}\" up", name)),
                .vertical_fov_degrees = finite_float(camera.vertical_fov_degrees, std::format("Dynamic scene camera \"{}\" vertical fov", name)),
                .near_plane = finite_float(camera.near_plane, std::format("Dynamic scene camera \"{}\" near plane", name)),
                .far_plane = finite_float(camera.far_plane, std::format("Dynamic scene camera \"{}\" far plane", name)),
            };
        }

        [[nodiscard]] scene::Scene::Mesh make_mesh(const SpectraDynamicSceneMesh& mesh, const bool dynamic) {
            const std::string name = abi_string(mesh.name, "Dynamic scene mesh name", false);
            scene::Scene::Mesh result{
                .name = name,
                .material_name = abi_string(mesh.material_name, std::format("Dynamic scene mesh \"{}\" material name", name), false),
                .transform = make_transform(mesh.transform, std::format("Dynamic scene mesh \"{}\"", name)),
                .dynamic = dynamic,
            };
            const std::span<const SpectraDynamicSceneMeshVertex> vertices = abi_span(mesh.vertices.data, mesh.vertices.count, std::format("Dynamic scene mesh \"{}\" vertices", name));
            result.positions.reserve(vertices.size());
            result.normals.reserve(vertices.size());
            for (std::size_t index = 0u; index < vertices.size(); ++index) {
                result.positions.push_back(make_vector3(vertices[index].position, std::format("Dynamic scene mesh \"{}\" vertex #{} position", name, index)));
                result.normals.push_back(make_vector3(vertices[index].normal, std::format("Dynamic scene mesh \"{}\" vertex #{} normal", name, index)));
            }
            const std::span<const std::uint32_t> indices = abi_span(mesh.indices.data, mesh.indices.count, std::format("Dynamic scene mesh \"{}\" indices", name));
            result.indices.assign(indices.begin(), indices.end());
            if (result.positions.empty()) throw std::runtime_error(std::format("Dynamic scene mesh \"{}\" must contain vertices", name));
            if (result.indices.empty() || result.indices.size() % 3u != 0u) throw std::runtime_error(std::format("Dynamic scene mesh \"{}\" must contain triangle indices", name));
            for (const std::uint32_t index : result.indices)
                if (index >= result.positions.size()) throw std::runtime_error(std::format("Dynamic scene mesh \"{}\" contains an out-of-range vertex index", name));
            return result;
        }

        [[nodiscard]] scene::Scene::Sphere make_sphere(const SpectraDynamicSceneSphere& sphere, const bool dynamic) {
            const std::string name = abi_string(sphere.name, "Dynamic scene sphere name", false);
            const float radius = finite_float(sphere.radius, std::format("Dynamic scene sphere \"{}\" radius", name));
            if (radius <= 0.0f) throw std::runtime_error(std::format("Dynamic scene sphere \"{}\" radius must be positive", name));
            return scene::Scene::Sphere{
                .name = name,
                .radius = radius,
                .material_name = abi_string(sphere.material_name, std::format("Dynamic scene sphere \"{}\" material name", name), false),
                .transform = make_transform(sphere.transform, std::format("Dynamic scene sphere \"{}\"", name)),
                .dynamic = dynamic,
            };
        }

        [[nodiscard]] scene::Scene::PointCloud make_point_cloud(const SpectraDynamicScenePointCloud& point_cloud, const bool dynamic) {
            const std::string name = abi_string(point_cloud.name, "Dynamic scene point cloud name", false);
            scene::Scene::PointCloud result{
                .name = name,
                .material_name = abi_string(point_cloud.material_name, std::format("Dynamic scene point cloud \"{}\" material name", name), false),
                .transform = make_transform(point_cloud.transform, std::format("Dynamic scene point cloud \"{}\"", name)),
                .dynamic = dynamic,
            };
            const std::span<const SpectraDynamicScenePoint> points = abi_span(point_cloud.points.data, point_cloud.points.count, std::format("Dynamic scene point cloud \"{}\" points", name));
            result.positions.reserve(points.size());
            result.normals.reserve(points.size());
            result.colors.reserve(points.size());
            result.radii.reserve(points.size());
            for (std::size_t index = 0u; index < points.size(); ++index) {
                result.positions.push_back(make_vector3(points[index].position, std::format("Dynamic scene point cloud \"{}\" point #{} position", name, index)));
                result.normals.push_back(make_vector3(points[index].normal, std::format("Dynamic scene point cloud \"{}\" point #{} normal", name, index)));
                result.colors.push_back(make_vector4(points[index].color, std::format("Dynamic scene point cloud \"{}\" point #{} color", name, index)));
                const float radius = finite_float(points[index].radius, std::format("Dynamic scene point cloud \"{}\" point #{} radius", name, index));
                if (radius <= 0.0f) throw std::runtime_error(std::format("Dynamic scene point cloud \"{}\" point #{} radius must be positive", name, index));
                result.radii.push_back(radius);
            }
            return result;
        }

        [[nodiscard]] scene::Scene::VolumeGrid make_volume(const SpectraDynamicSceneVolume& volume, const bool dynamic) {
            const std::string name = abi_string(volume.name, "Dynamic scene volume name", false);
            scene::Scene::VolumeGrid result{
                .name = name,
                .dimensions = {volume.dimensions[0], volume.dimensions[1], volume.dimensions[2]},
                .origin = make_vector3(volume.origin, std::format("Dynamic scene volume \"{}\" origin", name)),
                .voxel_size = make_vector3(volume.voxel_size, std::format("Dynamic scene volume \"{}\" voxel size", name)),
                .material_name = abi_string(volume.material_name, std::format("Dynamic scene volume \"{}\" material name", name), false),
                .dynamic = dynamic,
            };
            if (result.dimensions[0] == 0u || result.dimensions[1] == 0u || result.dimensions[2] == 0u) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" dimensions must be positive", name));
            const std::span<const SpectraDynamicSceneVolumeChannel> channels = abi_span(volume.channels.data, volume.channels.count, std::format("Dynamic scene volume \"{}\" channels", name));
            for (const SpectraDynamicSceneVolumeChannel& channel : channels) {
                scene::Scene::VolumeChannel converted{
                    .name = abi_string(channel.name, std::format("Dynamic scene volume \"{}\" channel name", name), false),
                    .dimensions = {channel.dimensions[0], channel.dimensions[1], channel.dimensions[2]},
                };
                if (converted.dimensions != result.dimensions) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" dimensions do not match", name, converted.name));
                const std::uint64_t expected_count = static_cast<std::uint64_t>(converted.dimensions[0]) * static_cast<std::uint64_t>(converted.dimensions[1]) * static_cast<std::uint64_t>(converted.dimensions[2]);
                const std::span<const float> values = abi_span(channel.values.data, channel.values.count, std::format("Dynamic scene volume \"{}\" channel \"{}\" values", name, converted.name));
                if (expected_count != values.size()) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" value count does not match dimensions", name, converted.name));
                converted.values.assign(values.begin(), values.end());
                for (std::size_t index = 0u; index < converted.values.size(); ++index)
                    if (!std::isfinite(converted.values[index])) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" value #{} must be finite", name, converted.name, index));
                result.channels.push_back(std::move(converted));
            }
            return result;
        }

        template <typename Item>
        void require_unique_name(std::set<std::string>& names, const Item& item, const std::string_view kind) {
            if (item.name.empty()) throw std::runtime_error(std::format("Dynamic scene {} name must not be empty", kind));
            if (!names.insert(item.name).second) throw std::runtime_error(std::format("Dynamic scene {} \"{}\" is duplicated", kind, item.name));
        }

        [[nodiscard]] std::set<std::string> collect_material_names(const scene::Scene::Document& document) {
            std::set<std::string> names{};
            for (const scene::Scene::PreviewMaterial& material : document.materials) require_unique_name(names, material, "material");
            return names;
        }

        [[nodiscard]] std::set<std::string> collect_light_names(const scene::Scene::Document& document) {
            std::set<std::string> names{};
            for (const scene::Scene::PreviewLight& light : document.lights) require_unique_name(names, light, "light");
            return names;
        }

        template <typename Primitive>
        void require_material_reference(const Primitive& primitive, const std::set<std::string>& material_names, const std::string_view kind) {
            if (primitive.material_name.empty()) throw std::runtime_error(std::format("Dynamic scene {} \"{}\" material name must not be empty", kind, primitive.name));
            if (!material_names.contains(primitive.material_name)) throw std::runtime_error(std::format("Dynamic scene {} \"{}\" references unknown material \"{}\"", kind, primitive.name, primitive.material_name));
        }

        void append_document_view(scene::Scene::Document& document, const SpectraDynamicSceneDocumentView& view, std::set<std::string>& material_names, std::set<std::string>& light_names) {
            if (view.struct_size != sizeof(SpectraDynamicSceneDocumentView)) throw std::runtime_error("Dynamic scene document view ABI size mismatch");
            if (view.has_camera != 0u) document.camera = make_camera(view.camera);
            for (const SpectraDynamicSceneMaterial& material_view : abi_span(view.materials, "Dynamic scene document materials")) {
                scene::Scene::PreviewMaterial material = make_material(material_view);
                require_unique_name(material_names, material, "material");
                document.materials.push_back(std::move(material));
            }
            for (const SpectraDynamicSceneLight& light_view : abi_span(view.lights, "Dynamic scene document lights")) {
                scene::Scene::PreviewLight light = make_light(light_view);
                require_unique_name(light_names, light, "light");
                document.lights.push_back(std::move(light));
            }
            for (const SpectraDynamicSceneMesh& mesh_view : abi_span(view.meshes, "Dynamic scene document meshes")) {
                scene::Scene::Mesh mesh = make_mesh(mesh_view, false);
                require_material_reference(mesh, material_names, "mesh");
                document.meshes.push_back(std::move(mesh));
            }
            for (const SpectraDynamicSceneSphere& sphere_view : abi_span(view.spheres, "Dynamic scene document spheres")) {
                scene::Scene::Sphere sphere = make_sphere(sphere_view, false);
                require_material_reference(sphere, material_names, "sphere");
                document.spheres.push_back(std::move(sphere));
            }
            for (const SpectraDynamicScenePointCloud& point_cloud_view : abi_span(view.point_clouds, "Dynamic scene document point clouds")) {
                scene::Scene::PointCloud point_cloud = make_point_cloud(point_cloud_view, false);
                require_material_reference(point_cloud, material_names, "point cloud");
                document.point_clouds.push_back(std::move(point_cloud));
            }
            for (const SpectraDynamicSceneVolume& volume_view : abi_span(view.volumes, "Dynamic scene document volumes")) {
                scene::Scene::VolumeGrid volume = make_volume(volume_view, false);
                require_material_reference(volume, material_names, "volume");
                document.volumes.push_back(std::move(volume));
            }
        }

        [[nodiscard]] scene::Scene::FrameSnapshot make_frame_snapshot(const SpectraDynamicSceneFrameView& view, const scene::Scene::FrameInfo& frame, const std::set<std::string>& material_names) {
            if (view.struct_size != sizeof(SpectraDynamicSceneFrameView)) throw std::runtime_error("Dynamic scene frame view ABI size mismatch");
            scene::Scene::FrameSnapshot snapshot{.cursor = scene::Scene::make_frame_cursor(frame)};
            for (const SpectraDynamicSceneMesh& mesh_view : abi_span(view.meshes, "Dynamic scene frame meshes")) {
                scene::Scene::Mesh mesh = make_mesh(mesh_view, true);
                require_material_reference(mesh, material_names, "mesh");
                snapshot.meshes.push_back(std::move(mesh));
            }
            for (const SpectraDynamicSceneSphere& sphere_view : abi_span(view.spheres, "Dynamic scene frame spheres")) {
                scene::Scene::Sphere sphere = make_sphere(sphere_view, true);
                require_material_reference(sphere, material_names, "sphere");
                snapshot.spheres.push_back(std::move(sphere));
            }
            for (const SpectraDynamicScenePointCloud& point_cloud_view : abi_span(view.point_clouds, "Dynamic scene frame point clouds")) {
                scene::Scene::PointCloud point_cloud = make_point_cloud(point_cloud_view, true);
                require_material_reference(point_cloud, material_names, "point cloud");
                snapshot.point_clouds.push_back(std::move(point_cloud));
            }
            for (const SpectraDynamicSceneVolume& volume_view : abi_span(view.volumes, "Dynamic scene frame volumes")) {
                scene::Scene::VolumeGrid volume = make_volume(volume_view, true);
                require_material_reference(volume, material_names, "volume");
                snapshot.volumes.push_back(std::move(volume));
            }
            return snapshot;
        }

        class NativeLibrary final {
        public:
            explicit NativeLibrary(std::filesystem::path path) : path(std::move(path)) {
#if defined(_WIN32)
                this->handle = ::LoadLibraryW(this->path.wstring().c_str());
                if (this->handle == nullptr) throw std::runtime_error(std::format("{}: failed to load dynamic scene plugin, Win32 error {}", this->path.string(), ::GetLastError()));
#else
                this->handle = ::dlopen(this->path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
                if (this->handle == nullptr) throw std::runtime_error(std::format("{}: failed to load dynamic scene plugin: {}", this->path.string(), ::dlerror()));
#endif
            }

            NativeLibrary(const NativeLibrary& other) = delete;
            NativeLibrary(NativeLibrary&& other) = delete;
            NativeLibrary& operator=(const NativeLibrary& other) = delete;
            NativeLibrary& operator=(NativeLibrary&& other) = delete;

            ~NativeLibrary() noexcept {
#if defined(_WIN32)
                if (this->handle != nullptr) static_cast<void>(::FreeLibrary(this->handle));
#else
                if (this->handle != nullptr) static_cast<void>(::dlclose(this->handle));
#endif
            }

            [[nodiscard]] void* symbol(const char* name) const {
#if defined(_WIN32)
                void* symbol_address = reinterpret_cast<void*>(::GetProcAddress(this->handle, name));
                if (symbol_address == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin is missing export \"{}\", Win32 error {}", this->path.string(), name, ::GetLastError()));
                return symbol_address;
#else
                ::dlerror();
                void* symbol_address = ::dlsym(this->handle, name);
                const char* error = ::dlerror();
                if (error != nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin is missing export \"{}\": {}", this->path.string(), name, error));
                return symbol_address;
#endif
            }

        private:
            std::filesystem::path path{};
#if defined(_WIN32)
            HMODULE handle{};
#else
            void* handle{};
#endif
        };

        class DynamicScenePluginLibrary final {
        public:
            explicit DynamicScenePluginLibrary(std::filesystem::path path) : plugin_path(std::move(path)), plugin_directory(this->plugin_path.parent_path()), native(this->plugin_path) {
                void* entry_address = this->native.symbol("spectra_dynamic_scene_plugin");
                const SpectraDynamicScenePluginEntryFn entry = reinterpret_cast<SpectraDynamicScenePluginEntryFn>(entry_address);
                this->plugin = entry();
                if (this->plugin == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin entry returned null", this->plugin_path.string()));
                this->validate_descriptor();
            }

            DynamicScenePluginLibrary(const DynamicScenePluginLibrary& other) = delete;
            DynamicScenePluginLibrary(DynamicScenePluginLibrary&& other) = delete;
            DynamicScenePluginLibrary& operator=(const DynamicScenePluginLibrary& other) = delete;
            DynamicScenePluginLibrary& operator=(DynamicScenePluginLibrary&& other) = delete;
            ~DynamicScenePluginLibrary() noexcept = default;

            [[nodiscard]] std::string id() const {
                return abi_string(this->plugin->id, "Dynamic scene plugin id", false);
            }

            [[nodiscard]] std::string title() const {
                return abi_string(this->plugin->title, "Dynamic scene plugin title", false);
            }

            [[nodiscard]] std::string source_uri() const {
                return std::format("plugin://{}", this->plugin_path.string());
            }

            [[nodiscard]] double frames_per_second() const {
                return finite_double(this->plugin->frames_per_second, "Dynamic scene plugin frame rate");
            }

            [[nodiscard]] scene::Scene::Document make_base_document() const {
                const std::string template_path_text = abi_string(this->plugin->pbrt_template_path, "Dynamic scene plugin PBRT template path", true);
                if (template_path_text.empty()) {
                    return scene::Scene::Document{
                        .revision = scene::Scene::Revision{1},
                        .name = this->id(),
                        .title = this->title(),
                        .source = this->source_uri(),
                        .frames_per_second = this->frames_per_second(),
                        .timeline_enabled = true,
                    };
                }
                const std::filesystem::path template_relative_path{template_path_text};
                if (template_relative_path.is_absolute()) throw std::runtime_error(std::format("{}: dynamic scene PBRT template path must be relative to the plugin directory", template_path_text));
                const std::filesystem::path template_path = (this->plugin_directory / template_relative_path).lexically_normal();
                if (!std::filesystem::is_regular_file(template_path)) throw std::runtime_error(std::format("{}: dynamic scene PBRT template file does not exist", template_path.string()));
                scene::Scene template_scene = scene::Scene::parse_pbrt_file(template_path);
                scene::Scene::Document document = *template_scene.document();
                document.revision = scene::Scene::Revision{1};
                document.name = this->id();
                document.title = this->title();
                document.source = this->source_uri();
                document.frames_per_second = this->frames_per_second();
                document.timeline_enabled = true;
                return document;
            }

            void check_result(const SpectraDynamicSceneResult result, SpectraDynamicSceneInstance* instance, const std::string_view action) const {
                if (result == SPECTRA_DYNAMIC_SCENE_RESULT_OK) return;
                if (result != SPECTRA_DYNAMIC_SCENE_RESULT_ERROR) throw std::runtime_error(std::format("{} returned an unknown result code {}", action, static_cast<int>(result)));
                std::string error = abi_string(this->plugin->last_error(instance), std::format("{} error message", action), true);
                if (error.empty()) error = "unknown plugin error";
                throw std::runtime_error(std::format("{} failed: {}", action, error));
            }

            [[nodiscard]] SpectraDynamicSceneInstance* create_instance() const {
                SpectraDynamicSceneInstance* instance{};
                this->check_result(this->plugin->create(&instance), nullptr, "Dynamic scene plugin create");
                if (instance == nullptr) throw std::runtime_error("Dynamic scene plugin create returned a null instance");
                return instance;
            }

            void destroy_instance(SpectraDynamicSceneInstance* instance) const noexcept {
                if (instance != nullptr) this->plugin->destroy(instance);
            }

            void reset(SpectraDynamicSceneInstance* instance) const {
                this->check_result(this->plugin->reset(instance), instance, "Dynamic scene plugin reset");
            }

            void step(SpectraDynamicSceneInstance* instance, const float delta_seconds) const {
                this->check_result(this->plugin->step(instance, delta_seconds), instance, "Dynamic scene plugin step");
            }

            [[nodiscard]] SpectraDynamicSceneDocumentView document(SpectraDynamicSceneInstance* instance) const {
                SpectraDynamicSceneDocumentView view{};
                this->check_result(this->plugin->document(instance, &view), instance, "Dynamic scene plugin document");
                return view;
            }

            [[nodiscard]] SpectraDynamicSceneFrameView frame(SpectraDynamicSceneInstance* instance, const scene::Scene::FrameInfo& frame_info) const {
                SpectraDynamicSceneFrameView view{};
                this->check_result(this->plugin->frame(instance, SpectraDynamicSceneFrameInfo{.delta_seconds = frame_info.delta_seconds, .time_seconds = frame_info.time_seconds, .frame_index = frame_info.frame_index}, &view), instance, "Dynamic scene plugin frame");
                return view;
            }

        private:
            void validate_descriptor() const {
                if (this->plugin->abi_version != plugin_abi_version) throw std::runtime_error(std::format("{}: dynamic scene plugin ABI version {} does not match host ABI version {}", this->plugin_path.string(), this->plugin->abi_version, plugin_abi_version));
                if (this->plugin->struct_size != sizeof(SpectraDynamicScenePlugin)) throw std::runtime_error(std::format("{}: dynamic scene plugin descriptor size mismatch", this->plugin_path.string()));
                static_cast<void>(this->id());
                static_cast<void>(this->title());
                const double fps = this->frames_per_second();
                if (fps <= 0.0) throw std::runtime_error(std::format("{}: dynamic scene plugin frame rate must be positive", this->plugin_path.string()));
                if (this->plugin->create == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin create function is null", this->plugin_path.string()));
                if (this->plugin->destroy == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin destroy function is null", this->plugin_path.string()));
                if (this->plugin->reset == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin reset function is null", this->plugin_path.string()));
                if (this->plugin->step == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin step function is null", this->plugin_path.string()));
                if (this->plugin->document == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin document function is null", this->plugin_path.string()));
                if (this->plugin->frame == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin frame function is null", this->plugin_path.string()));
                if (this->plugin->last_error == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin last_error function is null", this->plugin_path.string()));
            }

            std::filesystem::path plugin_path{};
            std::filesystem::path plugin_directory{};
            NativeLibrary native;
            const SpectraDynamicScenePlugin* plugin{};
        };

        class DynamicScenePluginSourceInstance final : public DynamicSceneSourceInstance {
        public:
            explicit DynamicScenePluginSourceInstance(std::shared_ptr<DynamicScenePluginLibrary> plugin) : plugin(std::move(plugin)) {
                if (this->plugin == nullptr) throw std::runtime_error("Dynamic scene plugin source requires a plugin library");
                this->instance = this->plugin->create_instance();
            }

            DynamicScenePluginSourceInstance(const DynamicScenePluginSourceInstance& other) = delete;
            DynamicScenePluginSourceInstance(DynamicScenePluginSourceInstance&& other) = delete;
            DynamicScenePluginSourceInstance& operator=(const DynamicScenePluginSourceInstance& other) = delete;
            DynamicScenePluginSourceInstance& operator=(DynamicScenePluginSourceInstance&& other) = delete;

            ~DynamicScenePluginSourceInstance() noexcept override {
                this->plugin->destroy_instance(this->instance);
                this->instance = nullptr;
            }

            void reset() override {
                this->plugin->reset(this->instance);
            }

            void step(const float delta_seconds) override {
                this->plugin->step(this->instance, delta_seconds);
            }

            [[nodiscard]] scene::Scene::Document create_scene_document() const override {
                scene::Scene::Document document = this->plugin->make_base_document();
                std::set<std::string> material_names = collect_material_names(document);
                std::set<std::string> light_names = collect_light_names(document);
                append_document_view(document, this->plugin->document(this->instance), material_names, light_names);
                if (!document.camera.has_value()) throw std::runtime_error(std::format("Dynamic scene plugin \"{}\" did not provide a camera or PBRT template camera", this->plugin->id()));
                document.timeline_enabled = true;
                document.frames_per_second = this->plugin->frames_per_second();
                this->material_names = std::move(material_names);
                this->document_validated = true;
                return document;
            }

            [[nodiscard]] scene::Scene::FrameSnapshot create_scene_frame(const scene::Scene::FrameInfo& frame) const override {
                if (!this->document_validated) throw std::runtime_error("Dynamic scene plugin frame was requested before document material validation");
                return make_frame_snapshot(this->plugin->frame(this->instance, frame), frame, this->material_names);
            }

        private:
            std::shared_ptr<DynamicScenePluginLibrary> plugin{};
            SpectraDynamicSceneInstance* instance{};
            mutable std::set<std::string> material_names{};
            mutable bool document_validated{};
        };
    } // namespace

    bool is_dynamic_scene_plugin_file(const std::filesystem::path& path) {
#if defined(_WIN32)
        return path_extension_is(path, ".dll");
#elif defined(__APPLE__)
        return path_extension_is(path, ".dylib");
#else
        return path_extension_is(path, ".so");
#endif
    }

    DynamicScenePluginSource load_dynamic_scene_plugin(const std::filesystem::path& plugin_path) {
        if (plugin_path.empty()) throw std::runtime_error("Drop a dynamic scene plugin library into the window to load it");
        const std::filesystem::path absolute_path = std::filesystem::absolute(plugin_path).lexically_normal();
        if (std::filesystem::is_directory(absolute_path)) throw std::runtime_error("Drop a dynamic scene plugin library, not a folder");
        if (!std::filesystem::is_regular_file(absolute_path)) throw std::runtime_error(std::format("{}: dynamic scene plugin file does not exist", absolute_path.string()));
        if (!is_dynamic_scene_plugin_file(absolute_path)) throw std::runtime_error(std::format("{}: dynamic scene plugin file extension is not supported on this platform", absolute_path.string()));
        std::shared_ptr<DynamicScenePluginLibrary> plugin = std::make_shared<DynamicScenePluginLibrary>(absolute_path);
        return DynamicScenePluginSource{
            .id = plugin->id(),
            .title = plugin->title(),
            .path = absolute_path,
            .create_source = [plugin = std::move(plugin)] {
                return std::make_unique<DynamicScenePluginSourceInstance>(plugin);
            },
        };
    }
} // namespace spectra::rasterizer
