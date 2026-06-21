module spectra.scene_session;

import std;
import spectra.scene;
import spectra.dynamic_scene.host;

namespace spectra::scene_session {
    namespace {
        void commit_scene_frame(scene::Scene& scene_instance, scene::Scene::FrameSnapshot frame) {
            scene::Scene::Edit edit{};
            edit.replace_frame(std::move(frame));
            const scene::Scene::DirtyFlags dirty = scene_instance.commit(std::move(edit));
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Frame)) throw std::runtime_error("Scene frame commit did not mark the frame dirty");
        }

        void commit_scene_timeline(scene::Scene& scene_instance, scene::Scene::Timeline timeline) {
            scene::Scene::Edit edit{};
            edit.replace_timeline(std::move(timeline));
            const scene::Scene::DirtyFlags dirty = scene_instance.commit(std::move(edit));
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Timeline)) throw std::runtime_error("Scene timeline commit did not mark the timeline dirty");
        }

        void commit_scene_timeline_and_frame(scene::Scene& scene_instance, scene::Scene::Timeline timeline, scene::Scene::FrameSnapshot frame) {
            scene::Scene::Edit edit{};
            edit.replace_timeline(std::move(timeline));
            edit.replace_frame(std::move(frame));
            const scene::Scene::DirtyFlags dirty = scene_instance.commit(std::move(edit));
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Timeline)) throw std::runtime_error("Scene timeline commit did not mark the timeline dirty");
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Frame)) throw std::runtime_error("Scene frame commit did not mark the frame dirty");
        }

        void commit_scene_document_and_frame(scene::Scene& scene_instance, scene::Scene::Document document, scene::Scene::FrameSnapshot frame) {
            scene::Scene::Edit edit{};
            edit.replace_document(std::move(document));
            edit.replace_frame(std::move(frame));
            const scene::Scene::DirtyFlags dirty = scene_instance.commit(std::move(edit));
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Document)) throw std::runtime_error("Scene document commit did not mark the document dirty");
            if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Frame)) throw std::runtime_error("Scene frame commit did not mark the frame dirty");
        }

        [[nodiscard]] bool resolved_frame_has_renderable_entity(const scene::Scene::ResolvedFrame& frame) {
            return !frame.meshes.empty() || !frame.spheres.empty() || !frame.point_clouds.empty() || !frame.volumes.empty();
        }

        void validate_dynamic_scene_renderable_entities(scene::Scene& scene_instance, const std::string_view context) {
            const scene::Scene::ResolvedFrame frame = scene_instance.resolved_frame();
            if (!resolved_frame_has_renderable_entity(frame)) throw std::runtime_error(std::format("{} must contain at least one renderable Mesh, Sphere, PointCloud, or VolumeGrid entity", context));
        }

        [[nodiscard]] dynamic_scene::ControlTimelineMode dynamic_control_timeline_mode(const scene::Scene::TimelineMode mode) {
            switch (mode) {
                case scene::Scene::TimelineMode::Live: return dynamic_scene::ControlTimelineMode::Live;
                case scene::Scene::TimelineMode::Record: return dynamic_scene::ControlTimelineMode::Record;
                case scene::Scene::TimelineMode::Playback: return dynamic_scene::ControlTimelineMode::Playback;
            }
            throw std::runtime_error("Unknown scene timeline mode");
        }
    } // namespace

    Session::Session(std::shared_ptr<scene::Scene> empty_scene, std::shared_ptr<dynamic_scene::HostServices> host) : empty_scene(std::move(empty_scene)), host(std::move(host)) {
        if (this->empty_scene == nullptr) throw std::runtime_error("Scene session requires an empty Untitled scene");
        if (this->host == nullptr) throw std::runtime_error("Scene session requires dynamic scene host services");
    }

    std::shared_ptr<scene::Scene> Session::active_scene() {
        if (this->active_scene_instance != nullptr) return this->active_scene_instance;
        return this->empty_scene;
    }

    bool Session::has_active_scene() const {
        return this->descriptor.has_value();
    }

    const SceneDescriptor& Session::active_scene_descriptor() const {
        if (!this->descriptor.has_value()) throw std::runtime_error("Scene session has no active scene");
        return *this->descriptor;
    }

    bool Session::has_activation_error() const {
        return !this->activation_error_message.empty();
    }

    const std::string& Session::activation_error() const {
        return this->activation_error_message;
    }

    bool Session::has_active_dynamic_scene_controls() const {
        return this->descriptor.has_value() && this->descriptor->kind == SceneKind::Dynamic && this->dynamic.source != nullptr;
    }

    std::shared_ptr<dynamic_scene::HostServices> Session::dynamic_host() const {
        return this->host;
    }

    bool Session::active_scene_timeline_enabled() const {
        if (this->active_scene_instance == nullptr) return false;
        return this->active_scene_instance->document()->timeline_enabled;
    }

    bool Session::active_scene_timeline_streaming_enabled() const {
        if (!this->active_scene_timeline_enabled()) return false;
        return this->active_scene_instance->timeline().mode != scene::Scene::TimelineMode::Playback;
    }

    dynamic_scene::ControlSnapshot Session::active_dynamic_scene_control_snapshot() const {
        return this->active_dynamic_scene_control_source().control_snapshot();
    }

    void Session::close_scene() {
        this->reset_dynamic_state();
        this->active_scene_instance.reset();
        this->descriptor.reset();
        this->clear_activation_error();
    }

    void Session::clear_activation_error() {
        this->activation_error_message.clear();
    }

    bool Session::open_static_scene(std::string id, std::string title, std::move_only_function<std::shared_ptr<scene::Scene>()> load_scene) {
        try {
            if (id.empty()) throw std::runtime_error("Static scene id must not be empty");
            if (!load_scene) throw std::runtime_error("Static scene load request requires a scene factory");
            std::shared_ptr<scene::Scene> loaded_scene = load_scene();
            if (loaded_scene == nullptr) throw std::runtime_error("Static scene factory returned null");
            const scene::Scene::Info scene_info = loaded_scene->info();
            if (title.empty()) title = scene_info.title;
            if (title.empty()) throw std::runtime_error("Static scene title must not be empty");
            this->reset_dynamic_state();
            this->active_scene_instance = std::move(loaded_scene);
            this->descriptor = SceneDescriptor{
                .id = std::move(id),
                .title = std::move(title),
                .kind = SceneKind::Static,
            };
            this->clear_activation_error();
            return true;
        } catch (const std::exception& error) {
            this->activation_error_message = std::format("Failed to load static scene: {}", error.what());
            return false;
        }
    }

    bool Session::open_dynamic_scene(std::string id, std::string title, std::move_only_function<std::unique_ptr<dynamic_scene::SourceInstance>()> create_source) {
        try {
            if (id.empty()) throw std::runtime_error("Dynamic scene id must not be empty");
            if (!create_source) throw std::runtime_error("Dynamic scene activation requires a source factory");
            std::unique_ptr<dynamic_scene::SourceInstance> source = create_source();
            if (source == nullptr) throw std::runtime_error("Dynamic scene source factory returned null");
            source->reset();
            scene::Scene::Document document = source->create_scene_document();
            if (!document.timeline_enabled) throw std::runtime_error("Dynamic scene source document must enable timeline");
            if (!std::isfinite(document.frames_per_second) || document.frames_per_second <= 0.0) throw std::runtime_error("Dynamic scene source document frame rate must be finite and positive");
            if (title.empty()) title = document.title;
            if (title.empty()) throw std::runtime_error("Dynamic scene title must not be empty");
            std::shared_ptr<scene::Scene> scene_instance = std::make_shared<scene::Scene>(std::move(document));
            scene::Scene::FrameSnapshot snapshot = source->create_scene_frame(scene::Scene::FrameInfo{
                .delta_seconds = 0.0,
                .time_seconds  = 0.0,
                .frame_index   = 0u,
            });
            const std::uint64_t scene_revision = source->scene_revision();
            const std::shared_ptr<const scene::Scene::Document> scene_document = scene_instance->document();
            scene::Scene::Timeline timeline{
                .mode                 = scene::Scene::TimelineMode::Live,
                .frames_per_second    = scene_document->frames_per_second,
                .playing              = true,
                .selected_frame_index = 0,
            };
            commit_scene_timeline_and_frame(*scene_instance, std::move(timeline), std::move(snapshot));
            validate_dynamic_scene_renderable_entities(*scene_instance, "Dynamic scene initial frame");
            this->reset_dynamic_state();
            this->active_scene_instance = std::move(scene_instance);
            this->dynamic.source = std::move(source);
            this->dynamic.observed_scene_revision = scene_revision;
            this->descriptor = SceneDescriptor{
                .id = std::move(id),
                .title = std::move(title),
                .kind = SceneKind::Dynamic,
            };
            this->clear_activation_error();
            return true;
        } catch (const std::exception& error) {
            this->activation_error_message = std::format("Failed to load dynamic scene: {}", error.what());
            return false;
        }
    }

    void Session::toggle_active_scene_timeline_playback() {
        if (!this->active_scene_timeline_streaming_enabled()) throw std::runtime_error("Active scene timeline playback can only be toggled in Live or Record mode");
        scene::Scene::Timeline timeline = this->active_scene_instance->timeline();
        timeline.playing = !timeline.playing;
        commit_scene_timeline(*this->active_scene_instance, std::move(timeline));
    }

    void Session::request_active_scene_timeline_reset() {
        if (!this->active_scene_timeline_enabled()) throw std::runtime_error("Active scene does not support timeline reset");
        scene::Scene::Timeline timeline = this->active_scene_instance->timeline();
        ++timeline.reset_request_serial;
        commit_scene_timeline(*this->active_scene_instance, std::move(timeline));
    }

    void Session::tick(const double delta_seconds) {
        if (!this->descriptor.has_value() || this->descriptor->kind == SceneKind::Static) return;
        if (this->dynamic.source == nullptr) throw std::runtime_error("Dynamic scene has no source instance");
        if (this->active_scene_instance == nullptr) throw std::runtime_error("Dynamic scene has no scene instance");
        const std::shared_ptr<const scene::Scene::Document> document = this->active_scene_instance->document();
        if (!document->timeline_enabled) throw std::runtime_error("Dynamic scene source must enable timeline");
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0) throw std::runtime_error("Dynamic scene delta time is invalid");
        scene::Scene::Timeline timeline = this->active_scene_instance->timeline();
        if (timeline.frames_per_second <= 0.0) throw std::runtime_error("Dynamic scene timeline frame rate must be positive");
        const double fixed_delta_seconds = 1.0 / timeline.frames_per_second;
        if (timeline.reset_request_serial != this->dynamic.observed_reset_request_serial) {
            this->reset_dynamic_scene(std::move(timeline));
            this->dynamic.observed_reset_request_serial = this->active_scene_instance->timeline().reset_request_serial;
            this->dynamic.committed_playback_frame_index.reset();
            return;
        }
        if (timeline.clear_recording_request_serial != this->dynamic.observed_clear_recording_request_serial) {
            timeline.recorded_frames.clear();
            timeline.selected_frame_index = 0;
            commit_scene_timeline(*this->active_scene_instance, std::move(timeline));
            this->dynamic.observed_clear_recording_request_serial = this->active_scene_instance->timeline().clear_recording_request_serial;
            this->dynamic.committed_playback_frame_index.reset();
            return;
        }
        if (timeline.mode == scene::Scene::TimelineMode::Playback) {
            if (timeline.recorded_frames.empty()) return;
            if (timeline.selected_frame_index >= timeline.recorded_frames.size()) throw std::runtime_error("Dynamic scene playback selected frame is out of range");
            if (this->dynamic.committed_playback_frame_index.has_value() && *this->dynamic.committed_playback_frame_index == timeline.selected_frame_index) return;
            scene::Scene::FrameSnapshot selected_frame = timeline.recorded_frames.at(timeline.selected_frame_index);
            commit_scene_frame(*this->active_scene_instance, std::move(selected_frame));
            validate_dynamic_scene_renderable_entities(*this->active_scene_instance, "Dynamic scene playback frame");
            this->dynamic.committed_playback_frame_index = timeline.selected_frame_index;
            return;
        }
        this->dynamic.committed_playback_frame_index.reset();
        const bool scene_advancing = timeline.playing && timeline.mode != scene::Scene::TimelineMode::Playback;
        this->dynamic.source->update(dynamic_scene::UpdateInfo{
            .wall_delta_seconds = delta_seconds,
            .scene_delta_seconds = scene_advancing ? delta_seconds : 0.0,
            .time_seconds = this->dynamic.stream_time_seconds,
            .frame_index = this->dynamic.stream_frame_index,
            .timeline_mode = dynamic_control_timeline_mode(timeline.mode),
            .timeline_playing = timeline.playing,
        });
        this->commit_dynamic_scene_revision("Dynamic scene update");
        if (!timeline.playing) return;
        this->dynamic.frame_accumulator_seconds += delta_seconds;
        bool advanced = false;
        scene::Scene::FrameSnapshot snapshot{};
        while (this->dynamic.frame_accumulator_seconds >= fixed_delta_seconds) {
            this->dynamic.frame_accumulator_seconds -= fixed_delta_seconds;
            ++this->dynamic.stream_frame_index;
            this->dynamic.stream_time_seconds += fixed_delta_seconds;
            snapshot = this->dynamic.source->create_scene_frame(scene::Scene::FrameInfo{
                .delta_seconds = fixed_delta_seconds,
                .time_seconds  = this->dynamic.stream_time_seconds,
                .frame_index   = this->dynamic.stream_frame_index,
            });
            advanced = true;
        }
        if (!advanced) return;
        if (timeline.mode == scene::Scene::TimelineMode::Record) {
            timeline.recorded_frames.push_back(snapshot);
            timeline.selected_frame_index = timeline.recorded_frames.size() - 1u;
            commit_scene_timeline_and_frame(*this->active_scene_instance, std::move(timeline), std::move(snapshot));
            validate_dynamic_scene_renderable_entities(*this->active_scene_instance, "Dynamic scene recorded frame");
            return;
        }
        commit_scene_frame(*this->active_scene_instance, std::move(snapshot));
        validate_dynamic_scene_renderable_entities(*this->active_scene_instance, "Dynamic scene live frame");
    }

    void Session::execute_active_dynamic_scene_control_action(const std::string_view action_id, const std::span<const dynamic_scene::Option> options) {
        if (action_id.empty()) throw std::runtime_error("Dynamic scene controls action id must not be empty");
        this->active_dynamic_scene_control_source().execute_control_action(action_id, options);
        this->commit_dynamic_scene_revision("Dynamic scene control action");
    }

    void Session::update_active_dynamic_scene_control_setting(const std::string_view key, const std::string_view value) {
        if (key.empty()) throw std::runtime_error("Dynamic scene controls setting key must not be empty");
        this->active_dynamic_scene_control_source().update_control_setting(key, value);
        this->commit_dynamic_scene_revision("Dynamic scene control setting");
    }

    void Session::commit_dynamic_scene_revision(const std::string_view context) {
        if (this->dynamic.source == nullptr) throw std::runtime_error("Dynamic scene revision commit requires a source instance");
        if (this->active_scene_instance == nullptr) throw std::runtime_error("Dynamic scene revision commit requires a scene instance");
        const std::uint64_t scene_revision = this->dynamic.source->scene_revision();
        if (scene_revision == this->dynamic.observed_scene_revision) return;
        scene::Scene::Document document = this->dynamic.source->create_scene_document();
        if (!document.timeline_enabled) throw std::runtime_error("Dynamic scene source document must enable timeline");
        if (!std::isfinite(document.frames_per_second) || document.frames_per_second <= 0.0) throw std::runtime_error("Dynamic scene source document frame rate must be finite and positive");
        scene::Scene::FrameSnapshot snapshot = this->dynamic.source->create_scene_frame(scene::Scene::FrameInfo{
            .delta_seconds = 0.0,
            .time_seconds  = this->dynamic.stream_time_seconds,
            .frame_index   = this->dynamic.stream_frame_index,
        });
        commit_scene_document_and_frame(*this->active_scene_instance, std::move(document), std::move(snapshot));
        validate_dynamic_scene_renderable_entities(*this->active_scene_instance, context);
        this->dynamic.observed_scene_revision = scene_revision;
    }

    void Session::reset_dynamic_state() {
        this->dynamic.source.reset();
        this->dynamic.frame_accumulator_seconds = 0.0;
        this->dynamic.stream_time_seconds = 0.0;
        this->dynamic.stream_frame_index = 0;
        this->dynamic.observed_reset_request_serial = 0;
        this->dynamic.observed_clear_recording_request_serial = 0;
        this->dynamic.observed_scene_revision = 0;
        this->dynamic.committed_playback_frame_index.reset();
    }

    dynamic_scene::SourceInstance& Session::active_dynamic_scene_control_source() const {
        if (!this->descriptor.has_value() || this->descriptor->kind != SceneKind::Dynamic) throw std::runtime_error("Active scene is not a dynamic scene controls");
        if (this->dynamic.source == nullptr) throw std::runtime_error("Active dynamic scene controls has no source instance");
        return *this->dynamic.source;
    }

    void Session::reset_dynamic_scene(scene::Scene::Timeline timeline) {
        this->dynamic.frame_accumulator_seconds = 0.0;
        this->dynamic.stream_time_seconds = 0.0;
        this->dynamic.stream_frame_index = 0;
        this->dynamic.source->reset();
        this->dynamic.observed_scene_revision = this->dynamic.source->scene_revision();
        scene::Scene::FrameSnapshot snapshot = this->dynamic.source->create_scene_frame(scene::Scene::FrameInfo{
            .delta_seconds = 0.0,
            .time_seconds  = 0.0,
            .frame_index   = 0u,
        });
        timeline.selected_frame_index = 0;
        commit_scene_timeline_and_frame(*this->active_scene_instance, std::move(timeline), std::move(snapshot));
        validate_dynamic_scene_renderable_entities(*this->active_scene_instance, "Dynamic scene reset frame");
    }
} // namespace spectra::scene_session
