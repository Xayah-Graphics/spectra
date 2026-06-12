module spectra.rasterizer.visualization;

import std;

namespace spectra::rasterizer {
    namespace {
        void commit_scene_frame(scene::Scene& workspace, scene::Scene::FrameSnapshot frame) {
            scene::Scene::Edit edit{};
            edit.replace_frame(std::move(frame));
            const scene::Scene::DirtyFlags dirty = workspace.commit(std::move(edit));
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Frame)) throw std::runtime_error("Visualization frame commit did not mark the frame dirty");
        }

        void commit_scene_timeline(scene::Scene& workspace, scene::Scene::Timeline timeline) {
            scene::Scene::Edit edit{};
            edit.replace_timeline(std::move(timeline));
            const scene::Scene::DirtyFlags dirty = workspace.commit(std::move(edit));
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Timeline)) throw std::runtime_error("Visualization timeline commit did not mark the timeline dirty");
        }

        void commit_scene_timeline_and_frame(scene::Scene& workspace, scene::Scene::Timeline timeline, scene::Scene::FrameSnapshot frame) {
            scene::Scene::Edit edit{};
            edit.replace_timeline(std::move(timeline));
            edit.replace_frame(std::move(frame));
            const scene::Scene::DirtyFlags dirty = workspace.commit(std::move(edit));
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Timeline)) throw std::runtime_error("Visualization timeline commit did not mark the timeline dirty");
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Frame)) throw std::runtime_error("Visualization frame commit did not mark the frame dirty");
        }
    } // namespace

    VisualizationEntry::VisualizationEntry(std::string id, std::string title, const VisualizationKind kind, std::move_only_function<std::shared_ptr<scene::Scene>()> create_static_scene, std::move_only_function<std::unique_ptr<VisualizationSourceInstance>()> create_dynamic_source) : id(std::move(id)), title(std::move(title)), kind(kind), create_static_scene(std::move(create_static_scene)), create_dynamic_source(std::move(create_dynamic_source)) {}

    void VisualizationRegistry::register_static_visualization(std::string id, std::string title, std::move_only_function<std::shared_ptr<scene::Scene>()> create_scene) {
        if (!create_scene) throw std::runtime_error("Static visualization entry requires a scene factory");
        this->ensure_unique_visualization_id(id);
        this->entries.push_back(VisualizationEntry{std::move(id), std::move(title), VisualizationKind::Static, std::move(create_scene), std::move_only_function<std::unique_ptr<VisualizationSourceInstance>()>{}});
    }

    std::unique_ptr<VisualizationSourceInstance> VisualizationRegistry::create_dynamic_source(const std::size_t index) {
        if (this->entries.empty()) throw std::runtime_error("Visualization registry is empty");
        if (index >= this->entries.size()) throw std::runtime_error("Visualization registry index is out of range");
        VisualizationEntry& source = this->entries.at(index);
        if (source.kind != VisualizationKind::Dynamic) throw std::runtime_error("Visualization registry entry is not dynamic");
        if (!source.create_dynamic_source) throw std::runtime_error("Dynamic visualization entry has no source factory");
        return source.create_dynamic_source();
    }

    std::shared_ptr<scene::Scene> VisualizationRegistry::create_static_scene(const std::size_t index) {
        if (this->entries.empty()) throw std::runtime_error("Visualization registry is empty");
        if (index >= this->entries.size()) throw std::runtime_error("Visualization registry index is out of range");
        VisualizationEntry& source = this->entries.at(index);
        if (source.kind != VisualizationKind::Static) throw std::runtime_error("Visualization registry entry is not static");
        if (!source.create_static_scene) throw std::runtime_error("Static visualization entry has no scene factory");
        std::shared_ptr<scene::Scene> scene = source.create_static_scene();
        if (scene == nullptr) throw std::runtime_error("Static visualization scene factory returned null");
        return scene;
    }

    const VisualizationEntry& VisualizationRegistry::entry(const std::size_t index) const {
        if (index >= this->entries.size()) throw std::runtime_error("Visualization registry index is out of range");
        return this->entries.at(index);
    }

    std::size_t VisualizationRegistry::size() const {
        return this->entries.size();
    }

    void VisualizationRegistry::ensure_unique_visualization_id(const std::string& id) const {
        if (id.empty()) throw std::runtime_error("Visualization registry entry id must not be empty");
        for (const VisualizationEntry& source : this->entries) {
            if (source.id == id) throw std::runtime_error("Duplicate visualization registry entry id: " + id);
        }
    }

    VisualizationController::VisualizationController(VisualizationRegistry registry) : registry(std::move(registry)) {
        if (this->registry.size() == 0u) throw std::runtime_error("Visualization controller requires at least one visualization");
        this->slots.resize(this->registry.size());
        static_cast<void>(this->ensure_slot(0u));
    }

    std::shared_ptr<scene::Scene> VisualizationController::active_workspace() {
        return this->ensure_slot(this->current_active_index).workspace;
    }

    const VisualizationEntry& VisualizationController::entry(const std::size_t index) const {
        return this->registry.entry(index);
    }

    std::size_t VisualizationController::size() const {
        return this->registry.size();
    }

    std::size_t VisualizationController::selected_index() const {
        return this->pending_active_index.value_or(this->current_active_index);
    }

    bool VisualizationController::pending_switch() const {
        return this->pending_active_index.has_value() && *this->pending_active_index != this->current_active_index;
    }

    void VisualizationController::request_activate(const std::size_t index) {
        if (index >= this->slots.size()) throw std::runtime_error("Visualization activation index is out of range");
        if (index == this->current_active_index) {
            this->pending_active_index.reset();
            return;
        }
        this->pending_active_index = index;
    }

    bool VisualizationController::apply_pending_visualization() {
        if (!this->pending_active_index.has_value()) return false;
        const std::size_t next_index = *this->pending_active_index;
        this->pending_active_index.reset();
        if (next_index >= this->slots.size()) throw std::runtime_error("Pending visualization index is out of range");
        if (next_index == this->current_active_index) return false;
        static_cast<void>(this->ensure_slot(next_index));
        this->current_active_index = next_index;
        return true;
    }

    void VisualizationController::update_active_visualization(const double delta_seconds) {
        const VisualizationEntry& source = this->registry.entry(this->current_active_index);
        if (source.kind == VisualizationKind::Static) return;
        VisualizationSlot& slot = this->ensure_slot(this->current_active_index);
        if (slot.source == nullptr) throw std::runtime_error("Dynamic visualization slot has no source instance");
        const std::shared_ptr<const scene::Scene::Document> document = slot.workspace->document();
        if (!document->timeline_enabled) throw std::runtime_error("Dynamic visualization must enable timeline");
        scene::Scene::Timeline timeline = slot.workspace->timeline();
        if (timeline.frames_per_second <= 0.0) throw std::runtime_error("Visualization timeline frame rate must be positive");
        const double fixed_delta_seconds = 1.0 / timeline.frames_per_second;
        if (timeline.reset_request_serial != slot.observed_reset_request_serial) {
            this->reset_dynamic_visualization(slot, std::move(timeline));
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
            if (timeline.selected_frame_index >= timeline.recorded_frames.size()) throw std::runtime_error("Visualization playback selected frame is out of range");
            if (slot.committed_playback_frame_index.has_value() && *slot.committed_playback_frame_index == timeline.selected_frame_index) return;
            scene::Scene::FrameSnapshot selected_frame = timeline.recorded_frames.at(timeline.selected_frame_index);
            commit_scene_frame(*slot.workspace, std::move(selected_frame));
            slot.committed_playback_frame_index = timeline.selected_frame_index;
            return;
        }
        slot.committed_playback_frame_index.reset();
        if (!timeline.playing) return;
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0) throw std::runtime_error("Visualization frame delta time is invalid");
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

    VisualizationController::VisualizationSlot& VisualizationController::ensure_slot(const std::size_t index) {
        if (index >= this->slots.size()) throw std::runtime_error("Visualization slot index is out of range");
        VisualizationSlot& slot = this->slots.at(index);
        const VisualizationEntry& source = this->registry.entry(index);
        if (slot.workspace != nullptr) {
            if (source.kind == VisualizationKind::Dynamic && slot.source == nullptr) throw std::runtime_error("Dynamic visualization slot has a workspace but no source instance");
            return slot;
        }
        if (source.kind == VisualizationKind::Static) {
            slot.workspace = this->registry.create_static_scene(index);
            return slot;
        }
        scene::Scene::Document document = this->create_dynamic_slot(index, &slot);
        slot.workspace = std::make_shared<scene::Scene>(std::move(document));
        if (slot.source == nullptr) throw std::runtime_error("Dynamic visualization slot was not initialized");
        scene::Scene::FrameSnapshot snapshot = slot.source->create_scene_frame(scene::Scene::FrameInfo{
            .delta_seconds = 0.0,
            .time_seconds  = 0.0,
            .frame_index   = 0u,
        });
        const std::shared_ptr<const scene::Scene::Document> scene_document = slot.workspace->document();
        if (!scene_document->timeline_enabled) throw std::runtime_error("Dynamic visualization must enable timeline");
        scene::Scene::Timeline timeline{
            .mode                 = scene::Scene::TimelineMode::Live,
            .frames_per_second    = scene_document->frames_per_second,
            .playing              = true,
            .selected_frame_index = 0,
        };
        commit_scene_timeline_and_frame(*slot.workspace, std::move(timeline), std::move(snapshot));
        return slot;
    }

    scene::Scene::Document VisualizationController::create_dynamic_slot(const std::size_t index, VisualizationSlot* slot) {
        if (slot == nullptr) throw std::runtime_error("Dynamic visualization slot pointer must not be null");
        slot->source = this->registry.create_dynamic_source(index);
        slot->source->reset();
        scene::Scene::Document document = slot->source->create_scene_document();
        if (!document.timeline_enabled) throw std::runtime_error("Dynamic visualization document must enable timeline");
        return document;
    }

    void VisualizationController::reset_dynamic_visualization(VisualizationSlot& slot, scene::Scene::Timeline timeline) {
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
