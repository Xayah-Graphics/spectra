module spectra.scene_runtime.controller;

import std;
import spectra.scene;
import spectra.scene_runtime.host_services;

namespace spectra::scene_runtime {

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

        void commit_scene_document_and_frame(scene::Scene& workspace, scene::Scene::Document document, scene::Scene::FrameSnapshot frame) {
            scene::Scene::Edit edit{};
            edit.replace_document(std::move(document));
            edit.replace_frame(std::move(frame));
            const scene::Scene::DirtyFlags dirty = workspace.commit(std::move(edit));
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Document)) throw std::runtime_error("Scene document commit did not mark the document dirty");
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Frame)) throw std::runtime_error("Scene frame commit did not mark the frame dirty");
        }

        [[nodiscard]] bool resolved_frame_has_renderable_entity(const scene::Scene::ResolvedFrame& frame) {
            return !frame.meshes.empty() || !frame.spheres.empty() || !frame.point_clouds.empty() || !frame.volumes.empty();
        }

        void validate_dynamic_scene_renderable_entities(scene::Scene& workspace, const std::string_view context) {
            const scene::Scene::ResolvedFrame frame = workspace.resolved_frame();
            if (!resolved_frame_has_renderable_entity(frame)) throw std::runtime_error(std::format("{} must contain at least one renderable Mesh, Sphere, PointCloud, or VolumeGrid entity", context));
        }

        [[nodiscard]] DynamicSceneControlTimelineMode dynamic_control_timeline_mode(const scene::Scene::TimelineMode mode) {
            switch (mode) {
                case scene::Scene::TimelineMode::Live: return DynamicSceneControlTimelineMode::Live;
                case scene::Scene::TimelineMode::Record: return DynamicSceneControlTimelineMode::Record;
                case scene::Scene::TimelineMode::Playback: return DynamicSceneControlTimelineMode::Playback;
            }
            throw std::runtime_error("Unknown scene timeline mode");
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

    SceneController::SceneController(SceneRegistry registry, std::shared_ptr<scene::Scene> empty_workspace, std::shared_ptr<DynamicSceneHostServices> host_services) : registry(std::move(registry)), empty_workspace(std::move(empty_workspace)), host_services(std::move(host_services)) {
        if (this->empty_workspace == nullptr) throw std::runtime_error("Scene controller requires an empty Untitled workspace");
        if (this->host_services == nullptr) throw std::runtime_error("Scene controller requires dynamic scene host services");
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

    bool SceneController::has_active_dynamic_scene_controls() {
        if (!this->selected_entry_index.has_value()) return false;
        const SceneEntry& source = this->registry.entry(*this->selected_entry_index);
        if (source.kind != SceneEntryKind::Dynamic) return false;
        SceneSlot& slot = this->ensure_slot(*this->selected_entry_index);
        return slot.source != nullptr;
    }

    std::shared_ptr<DynamicSceneHostServices> SceneController::dynamic_host_services() const {
        return this->host_services;
    }

    bool SceneController::active_scene_timeline_enabled() {
        if (!this->selected_entry_index.has_value()) return false;
        SceneSlot& slot = this->ensure_slot(*this->selected_entry_index);
        if (slot.workspace == nullptr) return false;
        return slot.workspace->document()->timeline_enabled;
    }

    bool SceneController::active_scene_timeline_streaming_enabled() {
        if (!this->active_scene_timeline_enabled()) return false;
        SceneSlot& slot = this->ensure_slot(*this->selected_entry_index);
        return slot.workspace->timeline().mode != scene::Scene::TimelineMode::Playback;
    }

    DynamicSceneControlSnapshot SceneController::active_dynamic_scene_control_snapshot() {
        return this->active_dynamic_scene_control_source().control_snapshot();
    }

    void SceneController::activate_empty_workspace() {
        this->release_selected_dynamic_slot();
        this->selected_entry_index.reset();
        this->pending_selected_entry_index.reset();
        this->clear_activation_error();
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
            this->release_selected_dynamic_slot();
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
            const std::uint64_t scene_revision = source->scene_revision();
            const std::shared_ptr<const scene::Scene::Document> scene_document = workspace->document();
            scene::Scene::Timeline timeline{
                .mode                 = scene::Scene::TimelineMode::Live,
                .frames_per_second    = scene_document->frames_per_second,
                .playing              = true,
                .selected_frame_index = 0,
            };
            commit_scene_timeline_and_frame(*workspace, std::move(timeline), std::move(snapshot));
            validate_dynamic_scene_renderable_entities(*workspace, "Dynamic scene initial frame");
            const std::size_t index = this->registry.upsert_dynamic_source(std::move(id), std::move(title), std::move(create_source));
            this->sync_slot_count();
            this->release_selected_dynamic_slot();
            this->set_dynamic_slot(index, std::move(source), std::move(workspace));
            this->slots.at(index).observed_scene_revision = scene_revision;
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

    void SceneController::toggle_active_scene_timeline_playback() {
        if (!this->active_scene_timeline_streaming_enabled()) throw std::runtime_error("Active scene timeline playback can only be toggled in Live or Record mode");
        SceneSlot& slot = this->ensure_slot(*this->selected_entry_index);
        scene::Scene::Timeline timeline = slot.workspace->timeline();
        timeline.playing = !timeline.playing;
        commit_scene_timeline(*slot.workspace, std::move(timeline));
    }

    void SceneController::request_active_scene_timeline_reset() {
        if (!this->active_scene_timeline_enabled()) throw std::runtime_error("Active scene does not support timeline reset");
        SceneSlot& slot = this->ensure_slot(*this->selected_entry_index);
        scene::Scene::Timeline timeline = slot.workspace->timeline();
        ++timeline.reset_request_serial;
        commit_scene_timeline(*slot.workspace, std::move(timeline));
    }

    void SceneController::update_active_scene(const double delta_seconds) {
        if (!this->selected_entry_index.has_value()) return;
        const SceneEntry& source = this->registry.entry(*this->selected_entry_index);
        if (source.kind == SceneEntryKind::Static) return;
        SceneSlot& slot = this->ensure_slot(*this->selected_entry_index);
        if (slot.source == nullptr) throw std::runtime_error("Dynamic scene slot has no source instance");
        const std::shared_ptr<const scene::Scene::Document> document = slot.workspace->document();
        if (!document->timeline_enabled) throw std::runtime_error("Dynamic scene source must enable timeline");
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0) throw std::runtime_error("Dynamic scene delta time is invalid");
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
            validate_dynamic_scene_renderable_entities(*slot.workspace, "Dynamic scene playback frame");
            slot.committed_playback_frame_index = timeline.selected_frame_index;
            return;
        }
        slot.committed_playback_frame_index.reset();
        const bool scene_advancing = timeline.playing && timeline.mode != scene::Scene::TimelineMode::Playback;
        slot.source->update(DynamicSceneUpdateInfo{
            .wall_delta_seconds = delta_seconds,
            .scene_delta_seconds = scene_advancing ? delta_seconds : 0.0,
            .time_seconds = slot.stream_time_seconds,
            .frame_index = slot.stream_frame_index,
            .timeline_mode = dynamic_control_timeline_mode(timeline.mode),
            .timeline_playing = timeline.playing,
        });
        this->commit_dynamic_scene_revision(slot, "Dynamic scene update");
        if (!timeline.playing) return;
        slot.frame_accumulator_seconds += delta_seconds;
        bool advanced = false;
        scene::Scene::FrameSnapshot snapshot{};
        while (slot.frame_accumulator_seconds >= fixed_delta_seconds) {
            slot.frame_accumulator_seconds -= fixed_delta_seconds;
            ++slot.stream_frame_index;
            slot.stream_time_seconds += fixed_delta_seconds;
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
            validate_dynamic_scene_renderable_entities(*slot.workspace, "Dynamic scene recorded frame");
            return;
        }
        commit_scene_frame(*slot.workspace, std::move(snapshot));
        validate_dynamic_scene_renderable_entities(*slot.workspace, "Dynamic scene live frame");
    }

    void SceneController::execute_active_dynamic_scene_control_action(const std::string_view action_id, const std::span<const DynamicSceneOption> options) {
        if (action_id.empty()) throw std::runtime_error("Dynamic scene controls action id must not be empty");
        if (!this->selected_entry_index.has_value()) throw std::runtime_error("No active dynamic scene controls");
        const SceneEntry& source = this->registry.entry(*this->selected_entry_index);
        if (source.kind != SceneEntryKind::Dynamic) throw std::runtime_error("Active scene is not a dynamic scene controls");
        SceneSlot& slot = this->ensure_slot(*this->selected_entry_index);
        if (slot.source == nullptr) throw std::runtime_error("Active dynamic scene controls has no source instance");
        if (slot.workspace == nullptr) throw std::runtime_error("Active dynamic scene controls has no scene workspace");
        slot.source->execute_control_action(action_id, options);
        this->commit_dynamic_scene_revision(slot, "Dynamic scene control action");
    }

    void SceneController::update_active_dynamic_scene_control_setting(const std::string_view key, const std::string_view value) {
        if (key.empty()) throw std::runtime_error("Dynamic scene controls setting key must not be empty");
        if (!this->selected_entry_index.has_value()) throw std::runtime_error("No active dynamic scene controls");
        const SceneEntry& source = this->registry.entry(*this->selected_entry_index);
        if (source.kind != SceneEntryKind::Dynamic) throw std::runtime_error("Active scene is not a dynamic scene controls");
        SceneSlot& slot = this->ensure_slot(*this->selected_entry_index);
        if (slot.source == nullptr) throw std::runtime_error("Active dynamic scene controls has no source instance");
        if (slot.workspace == nullptr) throw std::runtime_error("Active dynamic scene controls has no scene workspace");
        slot.source->update_control_setting(key, value);
        this->commit_dynamic_scene_revision(slot, "Dynamic scene control setting");
    }

    void SceneController::commit_dynamic_scene_revision(SceneSlot& slot, const std::string_view context) {
        if (slot.source == nullptr) throw std::runtime_error("Dynamic scene revision commit requires a source instance");
        if (slot.workspace == nullptr) throw std::runtime_error("Dynamic scene revision commit requires a scene workspace");
        const std::uint64_t scene_revision = slot.source->scene_revision();
        if (scene_revision == slot.observed_scene_revision) return;
        scene::Scene::Document document = slot.source->create_scene_document();
        if (!document.timeline_enabled) throw std::runtime_error("Dynamic scene source document must enable timeline");
        if (!std::isfinite(document.frames_per_second) || document.frames_per_second <= 0.0) throw std::runtime_error("Dynamic scene source document frame rate must be finite and positive");
        scene::Scene::FrameSnapshot snapshot = slot.source->create_scene_frame(scene::Scene::FrameInfo{
            .delta_seconds = 0.0,
            .time_seconds  = slot.stream_time_seconds,
            .frame_index   = slot.stream_frame_index,
        });
        commit_scene_document_and_frame(*slot.workspace, std::move(document), std::move(snapshot));
        validate_dynamic_scene_renderable_entities(*slot.workspace, context);
        slot.observed_scene_revision = scene_revision;
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
        slot.observed_scene_revision = 0;
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
        slot.observed_scene_revision = 0;
        slot.committed_playback_frame_index.reset();
    }

    void SceneController::release_selected_dynamic_slot() {
        if (!this->selected_entry_index.has_value()) return;
        this->sync_slot_count();
        if (*this->selected_entry_index >= this->slots.size()) throw std::runtime_error("Selected scene slot index is out of range while releasing dynamic scene resources");
        const SceneEntry& source = this->registry.entry(*this->selected_entry_index);
        if (source.kind != SceneEntryKind::Dynamic) return;
        SceneSlot& slot = this->slots.at(*this->selected_entry_index);
        slot.source.reset();
        slot.workspace.reset();
        slot.frame_accumulator_seconds = 0.0;
        slot.stream_time_seconds = 0.0;
        slot.stream_frame_index = 0;
        slot.observed_reset_request_serial = 0;
        slot.observed_clear_recording_request_serial = 0;
        slot.observed_scene_revision = 0;
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
        slot.observed_scene_revision = slot.source->scene_revision();
        const std::shared_ptr<const scene::Scene::Document> scene_document = slot.workspace->document();
        if (!scene_document->timeline_enabled) throw std::runtime_error("Dynamic scene source must enable timeline");
        scene::Scene::Timeline timeline{
            .mode                 = scene::Scene::TimelineMode::Live,
            .frames_per_second    = scene_document->frames_per_second,
            .playing              = true,
            .selected_frame_index = 0,
        };
        commit_scene_timeline_and_frame(*slot.workspace, std::move(timeline), std::move(snapshot));
        validate_dynamic_scene_renderable_entities(*slot.workspace, "Dynamic scene initial frame");
        return slot;
    }

    DynamicSceneSourceInstance& SceneController::active_dynamic_scene_control_source() {
        if (!this->selected_entry_index.has_value()) throw std::runtime_error("No active dynamic scene controls");
        const SceneEntry& source = this->registry.entry(*this->selected_entry_index);
        if (source.kind != SceneEntryKind::Dynamic) throw std::runtime_error("Active scene is not a dynamic scene controls");
        SceneSlot& slot = this->ensure_slot(*this->selected_entry_index);
        if (slot.source == nullptr) throw std::runtime_error("Active dynamic scene controls has no source instance");
        return *slot.source;
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
        slot.observed_scene_revision = slot.source->scene_revision();
        scene::Scene::FrameSnapshot snapshot = slot.source->create_scene_frame(scene::Scene::FrameInfo{
            .delta_seconds = 0.0,
            .time_seconds  = 0.0,
            .frame_index   = 0u,
        });
        timeline.selected_frame_index = 0;
        commit_scene_timeline_and_frame(*slot.workspace, std::move(timeline), std::move(snapshot));
        validate_dynamic_scene_renderable_entities(*slot.workspace, "Dynamic scene reset frame");
    }
} // namespace spectra::scene_runtime
