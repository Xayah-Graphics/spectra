module spectra.rasterizer.visualization;

import std;

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

    SceneEntry::SceneEntry(std::string id, std::string title, const SceneEntryKind kind, std::move_only_function<std::shared_ptr<scene::Scene>()> create_static_scene, std::move_only_function<std::unique_ptr<VisualizationSourceInstance>()> create_dynamic_source) : id(std::move(id)), title(std::move(title)), kind(kind), create_static_scene(std::move(create_static_scene)), create_dynamic_source(std::move(create_dynamic_source)) {}

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
        this->entries.push_back(SceneEntry{std::move(id), std::move(title), SceneEntryKind::Static, std::move(create_scene), std::move_only_function<std::unique_ptr<VisualizationSourceInstance>()>{}});
        return this->entries.size() - 1u;
    }

    std::unique_ptr<VisualizationSourceInstance> SceneRegistry::create_dynamic_source(const std::size_t index) {
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

    void SceneRegistry::ensure_unique_entry_id(const std::string& id) const {
        if (id.empty()) throw std::runtime_error("Scene registry entry id must not be empty");
        for (const SceneEntry& source : this->entries) {
            if (source.id == id) throw std::runtime_error("Duplicate scene registry entry id: " + id);
        }
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
} // namespace spectra::rasterizer
