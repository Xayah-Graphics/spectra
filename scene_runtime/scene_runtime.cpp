module;

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

module spectra.scene_runtime;

import std;
import spectra.scene;

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

    void DynamicSceneHostServiceRouter::set_viewport_voxel_buffer_backend(std::move_only_function<DynamicSceneViewportVoxelBufferAllocation(const DynamicSceneViewportVoxelBufferRequest&)> request_callback, std::move_only_function<void(std::uint64_t)> release_callback) {
        if (!request_callback) throw std::runtime_error("Dynamic scene viewport voxel buffer request callback must not be empty");
        if (!release_callback) throw std::runtime_error("Dynamic scene viewport voxel buffer release callback must not be empty");
        this->request_viewport_voxel_buffer_callback = std::move(request_callback);
        this->release_viewport_voxel_buffer_callback = std::move(release_callback);
        this->last_error_message.clear();
    }

    void DynamicSceneHostServiceRouter::clear_viewport_voxel_buffer_backend() noexcept {
        this->request_viewport_voxel_buffer_callback = nullptr;
        this->release_viewport_voxel_buffer_callback = nullptr;
        this->last_error_message.clear();
    }

    DynamicSceneViewportVoxelBufferAllocation DynamicSceneHostServiceRouter::request_viewport_voxel_buffer(const DynamicSceneViewportVoxelBufferRequest& request) {
        try {
            if (!this->request_viewport_voxel_buffer_callback) throw std::runtime_error("Dynamic scene viewport voxel buffer backend is not available");
            this->last_error_message.clear();
            return this->request_viewport_voxel_buffer_callback(request);
        } catch (const std::exception& error) {
            this->last_error_message = error.what();
            throw;
        }
    }

    void DynamicSceneHostServiceRouter::release_viewport_voxel_buffer(const std::uint64_t resource_id) {
        try {
            if (!this->release_viewport_voxel_buffer_callback) throw std::runtime_error("Dynamic scene viewport voxel buffer backend is not available");
            this->last_error_message.clear();
            this->release_viewport_voxel_buffer_callback(resource_id);
        } catch (const std::exception& error) {
            this->last_error_message = error.what();
            throw;
        }
    }

    std::string_view DynamicSceneHostServiceRouter::last_error() const {
        return this->last_error_message;
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

    bool SceneController::has_active_dynamic_project() {
        if (!this->selected_entry_index.has_value()) return false;
        const SceneEntry& source = this->registry.entry(*this->selected_entry_index);
        if (source.kind != SceneEntryKind::Dynamic) return false;
        SceneSlot& slot = this->ensure_slot(*this->selected_entry_index);
        return slot.source != nullptr;
    }

    std::shared_ptr<DynamicSceneHostServices> SceneController::dynamic_host_services() const {
        return this->host_services;
    }

    DynamicSceneProjectStatus SceneController::active_dynamic_project_status() {
        return this->active_dynamic_project_source().project_status();
    }

    std::vector<DynamicSceneProjectLogEntry> SceneController::active_dynamic_project_logs() {
        return this->active_dynamic_project_source().project_logs();
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
            this->release_selected_dynamic_slot();
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

    void SceneController::update_active_project(const double delta_seconds) {
        if (!this->selected_entry_index.has_value()) return;
        const SceneEntry& source = this->registry.entry(*this->selected_entry_index);
        if (source.kind != SceneEntryKind::Dynamic) return;
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0) throw std::runtime_error("Dynamic project delta time is invalid");
        this->active_dynamic_project_source().update_project(static_cast<float>(delta_seconds));
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

    void SceneController::execute_active_dynamic_project_action(const std::string_view action_id, const std::span<const DynamicSceneOpenOption> options) {
        if (action_id.empty()) throw std::runtime_error("Dynamic project action id must not be empty");
        this->active_dynamic_project_source().execute_project_action(action_id, options);
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

    DynamicSceneSourceInstance& SceneController::active_dynamic_project_source() {
        if (!this->selected_entry_index.has_value()) throw std::runtime_error("No active dynamic project");
        const SceneEntry& source = this->registry.entry(*this->selected_entry_index);
        if (source.kind != SceneEntryKind::Dynamic) throw std::runtime_error("Active scene is not a dynamic project");
        SceneSlot& slot = this->ensure_slot(*this->selected_entry_index);
        if (slot.source == nullptr) throw std::runtime_error("Active dynamic project has no source instance");
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
        scene::Scene::FrameSnapshot snapshot = slot.source->create_scene_frame(scene::Scene::FrameInfo{
            .delta_seconds = 0.0,
            .time_seconds  = 0.0,
            .frame_index   = 0u,
        });
        timeline.selected_frame_index = 0;
        commit_scene_timeline_and_frame(*slot.workspace, std::move(timeline), std::move(snapshot));
    }

    namespace {
        constexpr std::uint32_t plugin_abi_version = 14u;
        constexpr std::string_view scene_api_name = "spectra.dynamic_scene.scene";
        constexpr std::string_view project_api_name = "spectra.dynamic_scene.project";
        constexpr std::uint32_t scene_api_version = 1u;
        constexpr std::uint32_t project_api_version = 1u;

        typedef void SpectraDynamicSceneInstance;

        enum SpectraDynamicSceneResult {
            SPECTRA_DYNAMIC_SCENE_RESULT_OK = 0,
            SPECTRA_DYNAMIC_SCENE_RESULT_ERROR = 1,
        };

        struct SpectraDynamicSceneString {
            const char* data{};
            std::uint64_t size{};
        };

        struct SpectraDynamicSceneOption {
            SpectraDynamicSceneString key{};
            SpectraDynamicSceneString value{};
        };

        struct SpectraDynamicSceneOptionSpan {
            const SpectraDynamicSceneOption* data{};
            std::uint64_t count{};
        };

        enum SpectraDynamicSceneOpenOptionKind {
            SPECTRA_DYNAMIC_SCENE_OPEN_OPTION_TEXT = 0,
            SPECTRA_DYNAMIC_SCENE_OPEN_OPTION_DIRECTORY_PATH = 1,
            SPECTRA_DYNAMIC_SCENE_OPEN_OPTION_FILE_PATH = 2,
            SPECTRA_DYNAMIC_SCENE_OPEN_OPTION_CHOICE = 3,
            SPECTRA_DYNAMIC_SCENE_OPEN_OPTION_BOOL = 4,
            SPECTRA_DYNAMIC_SCENE_OPEN_OPTION_FLOAT = 5,
            SPECTRA_DYNAMIC_SCENE_OPEN_OPTION_UNSIGNED_INTEGER = 6,
        };

        struct SpectraDynamicSceneOpenOptionChoice {
            SpectraDynamicSceneString value{};
            SpectraDynamicSceneString label{};
        };

        struct SpectraDynamicSceneOpenOptionChoiceSpan {
            const SpectraDynamicSceneOpenOptionChoice* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneOpenOptionSchema {
            SpectraDynamicSceneString key{};
            SpectraDynamicSceneString label{};
            SpectraDynamicSceneString description{};
            std::uint32_t kind{};
            std::uint32_t required{};
            SpectraDynamicSceneString default_value{};
            SpectraDynamicSceneOpenOptionChoiceSpan choices{};
        };

        struct SpectraDynamicSceneOpenOptionSchemaSpan {
            const SpectraDynamicSceneOpenOptionSchema* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneStringSpan {
            const SpectraDynamicSceneString* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneProjectAction {
            SpectraDynamicSceneString id{};
            SpectraDynamicSceneString label{};
            SpectraDynamicSceneString description{};
            SpectraDynamicSceneOpenOptionSchemaSpan options{};
        };

        struct SpectraDynamicSceneProjectActionSpan {
            const SpectraDynamicSceneProjectAction* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneProjectMetric {
            SpectraDynamicSceneString key{};
            SpectraDynamicSceneString label{};
            SpectraDynamicSceneString value{};
        };

        struct SpectraDynamicSceneProjectMetricSpan {
            const SpectraDynamicSceneProjectMetric* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneProjectStatusView {
            std::uint64_t struct_size{};
            SpectraDynamicSceneString phase{};
            SpectraDynamicSceneString headline{};
            SpectraDynamicSceneString detail{};
            SpectraDynamicSceneProjectMetricSpan metrics{};
            SpectraDynamicSceneStringSpan enabled_action_ids{};
        };

        struct SpectraDynamicSceneProjectLogEntry {
            std::uint64_t sequence{};
            SpectraDynamicSceneString level{};
            SpectraDynamicSceneString message{};
        };

        struct SpectraDynamicSceneProjectLogEntrySpan {
            const SpectraDynamicSceneProjectLogEntry* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneProjectLogView {
            std::uint64_t struct_size{};
            SpectraDynamicSceneProjectLogEntrySpan entries{};
        };

        struct SpectraDynamicSceneGpuDeviceIdentity {
            std::uint32_t vendor_id{};
            std::uint32_t device_id{};
            std::uint8_t device_uuid[16]{};
            std::uint8_t device_luid[8]{};
            std::uint32_t device_node_mask{};
        };

        struct SpectraDynamicSceneViewportVoxelBufferRequest {
            std::uint64_t struct_size{};
            std::uint64_t byte_size{};
            SpectraDynamicSceneString debug_name{};
        };

        struct SpectraDynamicSceneViewportVoxelBufferAllocation {
            std::uint64_t struct_size{};
            std::uint64_t resource_id{};
            std::uint64_t byte_size{};
            std::uint32_t handle_kind{};
            std::uintptr_t handle{};
            SpectraDynamicSceneGpuDeviceIdentity device_identity{};
        };

        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneRequestViewportVoxelBufferFn)(void* user_data, const SpectraDynamicSceneViewportVoxelBufferRequest* request, SpectraDynamicSceneViewportVoxelBufferAllocation* allocation);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneReleaseViewportVoxelBufferFn)(void* user_data, std::uint64_t resource_id);
        typedef SpectraDynamicSceneString (*SpectraDynamicSceneHostLastErrorFn)(void* user_data);

        struct SpectraDynamicSceneHostServices {
            std::uint64_t struct_size{};
            void* user_data{};
            SpectraDynamicSceneRequestViewportVoxelBufferFn request_viewport_voxel_buffer{};
            SpectraDynamicSceneReleaseViewportVoxelBufferFn release_viewport_voxel_buffer{};
            SpectraDynamicSceneHostLastErrorFn last_error{};
        };

        struct SpectraDynamicSceneOpenInfo {
            std::uint64_t struct_size{};
            SpectraDynamicSceneString plugin_path{};
            SpectraDynamicSceneOptionSpan options{};
            const SpectraDynamicSceneHostServices* host_services{};
        };

        struct SpectraDynamicSceneTransform {
            float position[3]{};
            float rotation[4]{};
            float scale[3]{};
        };

        struct SpectraDynamicSceneMaterial {
            SpectraDynamicSceneString name{};
            SpectraDynamicSceneString model{};
            SpectraDynamicSceneString alpha_mode{};
            float base_color[4]{};
            float emission_color[3]{};
            float emission_strength{};
            float roughness{};
            float metallic{};
            float alpha_cutoff{};
            float volume_density_scale{};
            float volume_temperature_scale{};
        };

        struct SpectraDynamicSceneMaterialSpan {
            const SpectraDynamicSceneMaterial* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneLight {
            SpectraDynamicSceneString name{};
            SpectraDynamicSceneString kind{};
            SpectraDynamicSceneTransform transform{};
            float color[3]{};
            float intensity{};
            float cone_angle_degrees{};
        };

        struct SpectraDynamicSceneLightSpan {
            const SpectraDynamicSceneLight* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneCameraImage {
            const std::uint8_t* rgba8{};
            std::uint64_t rgba8_size{};
            std::uint64_t revision{};
            std::uint32_t width{};
            std::uint32_t height{};
            float tint[4]{};
        };

        struct SpectraDynamicSceneCameraVisualization {
            std::uint32_t enabled{};
            float color[4]{};
            float width{};
            std::uint32_t width_mode{};
            std::uint32_t depth_mode{};
            float visual_near{};
            float visual_far{};
            std::uint32_t has_image{};
            SpectraDynamicSceneCameraImage image{};
        };

        struct SpectraDynamicSceneCamera {
            SpectraDynamicSceneString name{};
            SpectraDynamicSceneString local_coordinate_system{};
            SpectraDynamicSceneTransform transform{};
            float target[3]{};
            float up[3]{};
            std::uint32_t projection{};
            float vertical_fov_degrees{};
            std::uint32_t image_width{};
            std::uint32_t image_height{};
            float fx{};
            float fy{};
            float cx{};
            float cy{};
            float near_plane{};
            float far_plane{};
            SpectraDynamicSceneCameraVisualization visualization{};
        };

        struct SpectraDynamicSceneCameraSpan {
            const SpectraDynamicSceneCamera* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneMeshVertex {
            float position[3]{};
            float normal[3]{};
        };

        struct SpectraDynamicSceneMeshVertexSpan {
            const SpectraDynamicSceneMeshVertex* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneUInt32Span {
            const std::uint32_t* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneMesh {
            SpectraDynamicSceneString name{};
            SpectraDynamicSceneMeshVertexSpan vertices{};
            SpectraDynamicSceneUInt32Span indices{};
            SpectraDynamicSceneString material_name{};
            SpectraDynamicSceneTransform transform{};
        };

        struct SpectraDynamicSceneMeshSpan {
            const SpectraDynamicSceneMesh* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneSphere {
            SpectraDynamicSceneString name{};
            float radius{};
            SpectraDynamicSceneString material_name{};
            SpectraDynamicSceneTransform transform{};
        };

        struct SpectraDynamicSceneSphereSpan {
            const SpectraDynamicSceneSphere* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicScenePoint {
            float position[3]{};
            float normal[3]{};
            float color[4]{};
            float radius{};
        };

        struct SpectraDynamicScenePointSpan {
            const SpectraDynamicScenePoint* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicScenePointCloud {
            SpectraDynamicSceneString name{};
            SpectraDynamicScenePointSpan points{};
            SpectraDynamicSceneString material_name{};
            SpectraDynamicSceneTransform transform{};
        };

        struct SpectraDynamicScenePointCloudSpan {
            const SpectraDynamicScenePointCloud* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneFloatSpan {
            const float* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneVolumeChannel {
            SpectraDynamicSceneString name{};
            std::uint32_t dimensions[3]{};
            SpectraDynamicSceneFloatSpan values{};
        };

        struct SpectraDynamicSceneVolumeChannelSpan {
            const SpectraDynamicSceneVolumeChannel* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneVolume {
            SpectraDynamicSceneString name{};
            std::uint32_t dimensions[3]{};
            float origin[3]{};
            float voxel_size[3]{};
            SpectraDynamicSceneVolumeChannelSpan channels{};
            SpectraDynamicSceneString material_name{};
        };

        struct SpectraDynamicSceneVolumeSpan {
            const SpectraDynamicSceneVolume* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneViewportSegment {
            float start[3]{};
            float end[3]{};
        };

        struct SpectraDynamicSceneViewportSegmentSpan {
            const SpectraDynamicSceneViewportSegment* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneColor {
            float value[4]{};
        };

        struct SpectraDynamicSceneColorSpan {
            const SpectraDynamicSceneColor* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneViewportSegmentSet {
            SpectraDynamicSceneString name{};
            SpectraDynamicSceneViewportSegmentSpan segments{};
            SpectraDynamicSceneColorSpan colors{};
            SpectraDynamicSceneFloatSpan widths{};
            float width{};
            std::uint32_t width_mode{};
            std::uint32_t depth_mode{};
            SpectraDynamicSceneTransform transform{};
        };

        struct SpectraDynamicSceneViewportSegmentSetSpan {
            const SpectraDynamicSceneViewportSegmentSet* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneViewportVoxelGrid {
            SpectraDynamicSceneString name{};
            std::uint32_t dimensions[3]{};
            float origin[3]{};
            float voxel_size[3]{};
            SpectraDynamicSceneTransform transform{};
            float color[4]{};
            float cell_scale{};
            std::uint32_t depth_mode{};
            std::uint32_t source_kind{};
            std::uint32_t index_encoding{};
            std::uint64_t buffer_id{};
            std::uint64_t source_byte_size{};
            std::uint64_t index_count{};
            std::uint64_t revision{};
        };

        struct SpectraDynamicSceneViewportVoxelGridSpan {
            const SpectraDynamicSceneViewportVoxelGrid* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneDocumentView {
            std::uint64_t struct_size{};
            SpectraDynamicSceneString default_coordinate_system{};
            SpectraDynamicSceneString active_camera_name{};
            SpectraDynamicSceneCameraSpan cameras{};
            SpectraDynamicSceneMaterialSpan materials{};
            SpectraDynamicSceneLightSpan lights{};
            SpectraDynamicSceneMeshSpan meshes{};
            SpectraDynamicSceneSphereSpan spheres{};
            SpectraDynamicScenePointCloudSpan point_clouds{};
            SpectraDynamicSceneVolumeSpan volumes{};
            SpectraDynamicSceneViewportSegmentSetSpan viewport_segment_sets{};
            SpectraDynamicSceneViewportVoxelGridSpan viewport_voxel_grids{};
        };

        struct SpectraDynamicSceneFrameInfo {
            double delta_seconds{};
            double time_seconds{};
            std::uint64_t frame_index{};
        };

        struct SpectraDynamicSceneFrameView {
            std::uint64_t struct_size{};
            SpectraDynamicSceneMeshSpan meshes{};
            SpectraDynamicSceneSphereSpan spheres{};
            SpectraDynamicScenePointCloudSpan point_clouds{};
            SpectraDynamicSceneVolumeSpan volumes{};
            SpectraDynamicSceneCameraSpan cameras{};
            SpectraDynamicSceneViewportSegmentSetSpan viewport_segment_sets{};
            SpectraDynamicSceneViewportVoxelGridSpan viewport_voxel_grids{};
        };

        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneCreateFn)(const SpectraDynamicSceneOpenInfo* open_info, SpectraDynamicSceneInstance** instance);
        typedef void (*SpectraDynamicSceneDestroyFn)(SpectraDynamicSceneInstance* instance);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneResetFn)(SpectraDynamicSceneInstance* instance);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneStepFn)(SpectraDynamicSceneInstance* instance, float delta_seconds);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneDocumentFn)(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneDocumentView* document);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneFrameFn)(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneFrameInfo frame, SpectraDynamicSceneFrameView* snapshot);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneProjectUpdateFn)(SpectraDynamicSceneInstance* instance, float delta_seconds);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneProjectActionFn)(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneString action_id, SpectraDynamicSceneOptionSpan options);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneProjectStatusFn)(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneProjectStatusView* status);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneProjectLogsFn)(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneProjectLogView* logs);
        typedef SpectraDynamicSceneString (*SpectraDynamicSceneLastErrorFn)(SpectraDynamicSceneInstance* instance);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneGetApiFn)(SpectraDynamicSceneString api_name, std::uint32_t api_version, const void** api);

        struct SpectraDynamicSceneSceneApi {
            std::uint64_t struct_size{};
            SpectraDynamicSceneString pbrt_template_path{};
            double frames_per_second{};
            SpectraDynamicSceneCreateFn create{};
            SpectraDynamicSceneDestroyFn destroy{};
            SpectraDynamicSceneResetFn reset{};
            SpectraDynamicSceneStepFn step{};
            SpectraDynamicSceneDocumentFn document{};
            SpectraDynamicSceneFrameFn frame{};
            SpectraDynamicSceneLastErrorFn last_error{};
        };

        struct SpectraDynamicSceneProjectApi {
            std::uint64_t struct_size{};
            SpectraDynamicSceneProjectActionSpan project_actions{};
            SpectraDynamicSceneProjectUpdateFn project_update{};
            SpectraDynamicSceneProjectActionFn project_action{};
            SpectraDynamicSceneProjectStatusFn project_status{};
            SpectraDynamicSceneProjectLogsFn project_logs{};
        };

        struct SpectraDynamicScenePlugin {
            std::uint32_t abi_version{};
            std::uint64_t struct_size{};
            SpectraDynamicSceneString id{};
            SpectraDynamicSceneString title{};
            SpectraDynamicSceneString project_panel_title{};
            SpectraDynamicSceneString open_action_label{};
            SpectraDynamicSceneString open_action_description{};
            SpectraDynamicSceneOpenOptionSchemaSpan open_options{};
            SpectraDynamicSceneGetApiFn get_api{};
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

        [[nodiscard]] std::span<const SpectraDynamicSceneCamera> abi_span(const SpectraDynamicSceneCameraSpan span, const std::string_view context) {
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

        [[nodiscard]] std::span<const SpectraDynamicSceneViewportSegment> abi_span(const SpectraDynamicSceneViewportSegmentSpan span, const std::string_view context) {
            return abi_span(span.data, span.count, context);
        }

        [[nodiscard]] std::span<const SpectraDynamicSceneColor> abi_span(const SpectraDynamicSceneColorSpan span, const std::string_view context) {
            return abi_span(span.data, span.count, context);
        }

        [[nodiscard]] std::span<const SpectraDynamicSceneViewportSegmentSet> abi_span(const SpectraDynamicSceneViewportSegmentSetSpan span, const std::string_view context) {
            return abi_span(span.data, span.count, context);
        }

        [[nodiscard]] std::span<const SpectraDynamicSceneViewportVoxelGrid> abi_span(const SpectraDynamicSceneViewportVoxelGridSpan span, const std::string_view context) {
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

        [[nodiscard]] scene::Scene::ViewportSegmentWidthMode viewport_segment_width_mode_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::ViewportSegmentWidthMode::Screen;
            case 1u: return scene::Scene::ViewportSegmentWidthMode::World;
            }
            throw std::runtime_error(std::format("{} has invalid viewport segment width mode {}", context, value));
        }

        [[nodiscard]] scene::Scene::ViewportSegmentDepthMode viewport_segment_depth_mode_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::ViewportSegmentDepthMode::DepthTested;
            case 1u: return scene::Scene::ViewportSegmentDepthMode::AlwaysVisible;
            }
            throw std::runtime_error(std::format("{} has invalid viewport segment depth mode {}", context, value));
        }

        [[nodiscard]] scene::Scene::ViewportVoxelGridSourceKind viewport_voxel_grid_source_kind_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::ViewportVoxelGridSourceKind::IndexList;
            case 1u: return scene::Scene::ViewportVoxelGridSourceKind::Bitfield;
            }
            throw std::runtime_error(std::format("{} has invalid viewport voxel grid source kind {}", context, value));
        }

        [[nodiscard]] scene::Scene::ViewportVoxelGridIndexEncoding viewport_voxel_grid_index_encoding_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::ViewportVoxelGridIndexEncoding::Linear;
            case 1u: return scene::Scene::ViewportVoxelGridIndexEncoding::Morton3D;
            }
            throw std::runtime_error(std::format("{} has invalid viewport voxel grid index encoding {}", context, value));
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

        [[nodiscard]] scene::CameraProjection camera_projection(const SpectraDynamicSceneCamera& camera, const std::string& name) {
            scene::CameraProjection projection{
                .near_plane = finite_float(camera.near_plane, std::format("Dynamic scene camera \"{}\" near plane", name)),
                .far_plane = finite_float(camera.far_plane, std::format("Dynamic scene camera \"{}\" far plane", name)),
            };
            switch (camera.projection) {
            case 0u:
                projection.kind = scene::CameraProjectionKind::Perspective;
                projection.vertical_fov_degrees = finite_float(camera.vertical_fov_degrees, std::format("Dynamic scene camera \"{}\" vertical fov", name));
                return projection;
            case 1u:
                projection.kind = scene::CameraProjectionKind::Pinhole;
                projection.image_width = camera.image_width;
                projection.image_height = camera.image_height;
                projection.fx = finite_float(camera.fx, std::format("Dynamic scene camera \"{}\" fx", name));
                projection.fy = finite_float(camera.fy, std::format("Dynamic scene camera \"{}\" fy", name));
                projection.cx = finite_float(camera.cx, std::format("Dynamic scene camera \"{}\" cx", name));
                projection.cy = finite_float(camera.cy, std::format("Dynamic scene camera \"{}\" cy", name));
                return projection;
            }
            throw std::runtime_error(std::format("Dynamic scene camera \"{}\" has invalid projection {}", name, camera.projection));
        }

        [[nodiscard]] scene::Scene::CameraImage make_camera_image(const SpectraDynamicSceneCamera& camera, const std::string& name) {
            const auto& image = camera.visualization.image;
            if (image.width == 0u || image.height == 0u) throw std::runtime_error(std::format("Dynamic scene camera \"{}\" RGBA8 image dimensions must be non-zero", name));
            const std::uint64_t expected_byte_count = static_cast<std::uint64_t>(image.width) * static_cast<std::uint64_t>(image.height) * 4u;
            if (image.rgba8_size != expected_byte_count) throw std::runtime_error(std::format("Dynamic scene camera \"{}\" RGBA8 image byte count must be width * height * 4", name));
            scene::Scene::CameraImage result{
                .width = image.width,
                .height = image.height,
                .rgba8 = image.rgba8,
                .rgba8_size = image.rgba8_size,
                .revision = image.revision,
                .tint = make_vector4(image.tint, std::format("Dynamic scene camera \"{}\" image tint", name)),
            };
            static_cast<void>(abi_span(image.rgba8, image.rgba8_size, std::format("Dynamic scene camera \"{}\" RGBA8 image", name)));
            return result;
        }

        [[nodiscard]] scene::Scene::CameraVisualization make_camera_visualization(const SpectraDynamicSceneCamera& camera, const std::string& name) {
            if (camera.visualization.enabled == 0u) return scene::Scene::CameraVisualization{};
            scene::Scene::CameraVisualization visualization{
                .enabled = true,
                .color = make_vector4(camera.visualization.color, std::format("Dynamic scene camera \"{}\" visualization color", name)),
                .width = finite_float(camera.visualization.width, std::format("Dynamic scene camera \"{}\" visualization width", name)),
                .width_mode = viewport_segment_width_mode_from_u32(camera.visualization.width_mode, std::format("Dynamic scene camera \"{}\" visualization", name)),
                .depth_mode = viewport_segment_depth_mode_from_u32(camera.visualization.depth_mode, std::format("Dynamic scene camera \"{}\" visualization", name)),
                .visual_near = finite_float(camera.visualization.visual_near, std::format("Dynamic scene camera \"{}\" visualization near", name)),
                .visual_far = finite_float(camera.visualization.visual_far, std::format("Dynamic scene camera \"{}\" visualization far", name)),
            };
            if (camera.visualization.has_image != 0u) visualization.image = make_camera_image(camera, name);
            return visualization;
        }

        [[nodiscard]] scene::Scene::Camera make_camera(const SpectraDynamicSceneCamera& camera) {
            const std::string name = abi_string(camera.name, "Dynamic scene camera name", false);
            const std::string local_coordinate_system_name = abi_string(camera.local_coordinate_system, std::format("Dynamic scene camera \"{}\" local coordinate system", name), false);
            const scene::Transform transform = make_transform(camera.transform, std::format("Dynamic scene camera \"{}\"", name));
            const scene::Vector3 target = make_vector3(camera.target, std::format("Dynamic scene camera \"{}\" target", name));
            const scene::Vector3 up = make_vector3(camera.up, std::format("Dynamic scene camera \"{}\" up", name));
            return scene::Scene::Camera{
                .name = name,
                .view = scene::CameraViewState{
                    .pose = scene::CameraPose{
                        .position = transform.position,
                        .orientation = scene::normalized_quaternion(transform.rotation, std::format("Dynamic scene camera \"{}\" orientation", name)),
                        .local_convention = scene::coordinate_system(local_coordinate_system_name).convention,
                    },
                    .focus = target,
                    .navigation_up = scene::normalize(up, std::format("Dynamic scene camera \"{}\" up", name)),
                    .projection = camera_projection(camera, name),
                },
                .visualization = make_camera_visualization(camera, name),
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

        [[nodiscard]] scene::Scene::ViewportSegmentSet make_viewport_segment_set(const SpectraDynamicSceneViewportSegmentSet& segment_set, const bool dynamic) {
            const std::string name = abi_string(segment_set.name, "Dynamic scene viewport segment set name", false);
            scene::Scene::ViewportSegmentSet result{
                .name = name,
                .width = finite_float(segment_set.width, std::format("Dynamic scene viewport segment set \"{}\" width", name)),
                .width_mode = viewport_segment_width_mode_from_u32(segment_set.width_mode, std::format("Dynamic scene viewport segment set \"{}\"", name)),
                .depth_mode = viewport_segment_depth_mode_from_u32(segment_set.depth_mode, std::format("Dynamic scene viewport segment set \"{}\"", name)),
                .transform = make_transform(segment_set.transform, std::format("Dynamic scene viewport segment set \"{}\"", name)),
                .dynamic = dynamic,
            };

            const std::span<const SpectraDynamicSceneViewportSegment> segments = abi_span(segment_set.segments, std::format("Dynamic scene viewport segment set \"{}\" segments", name));
            result.segments.reserve(segments.size());
            for (std::size_t index = 0u; index < segments.size(); ++index) {
                result.segments.push_back(scene::Scene::ViewportSegment{
                    .start = make_vector3(segments[index].start, std::format("Dynamic scene viewport segment set \"{}\" segment #{} start", name, index)),
                    .end = make_vector3(segments[index].end, std::format("Dynamic scene viewport segment set \"{}\" segment #{} end", name, index)),
                });
            }

            const std::span<const SpectraDynamicSceneColor> colors = abi_span(segment_set.colors, std::format("Dynamic scene viewport segment set \"{}\" colors", name));
            if (!colors.empty() && colors.size() != result.segments.size()) throw std::runtime_error(std::format("Dynamic scene viewport segment set \"{}\" color count does not match segment count", name));
            result.colors.reserve(colors.size());
            for (std::size_t index = 0u; index < colors.size(); ++index) result.colors.push_back(make_vector4(colors[index].value, std::format("Dynamic scene viewport segment set \"{}\" color #{}", name, index)));

            const std::span<const float> widths = abi_span(segment_set.widths.data, segment_set.widths.count, std::format("Dynamic scene viewport segment set \"{}\" widths", name));
            if (!widths.empty() && widths.size() != result.segments.size()) throw std::runtime_error(std::format("Dynamic scene viewport segment set \"{}\" width count does not match segment count", name));
            result.widths.reserve(widths.size());
            for (std::size_t index = 0u; index < widths.size(); ++index) result.widths.push_back(finite_float(widths[index], std::format("Dynamic scene viewport segment set \"{}\" width #{}", name, index)));
            return result;
        }

        [[nodiscard]] scene::Scene::ViewportVoxelGrid make_viewport_voxel_grid(const SpectraDynamicSceneViewportVoxelGrid& voxel_grid, const bool dynamic) {
            const std::string name = abi_string(voxel_grid.name, "Dynamic scene viewport voxel grid name", false);
            return scene::Scene::ViewportVoxelGrid{
                .name = name,
                .dimensions = {voxel_grid.dimensions[0], voxel_grid.dimensions[1], voxel_grid.dimensions[2]},
                .origin = make_vector3(voxel_grid.origin, std::format("Dynamic scene viewport voxel grid \"{}\" origin", name)),
                .voxel_size = make_vector3(voxel_grid.voxel_size, std::format("Dynamic scene viewport voxel grid \"{}\" voxel size", name)),
                .transform = make_transform(voxel_grid.transform, std::format("Dynamic scene viewport voxel grid \"{}\"", name)),
                .color = make_vector4(voxel_grid.color, std::format("Dynamic scene viewport voxel grid \"{}\" color", name)),
                .cell_scale = finite_float(voxel_grid.cell_scale, std::format("Dynamic scene viewport voxel grid \"{}\" cell scale", name)),
                .depth_mode = viewport_segment_depth_mode_from_u32(voxel_grid.depth_mode, std::format("Dynamic scene viewport voxel grid \"{}\"", name)),
                .source_kind = viewport_voxel_grid_source_kind_from_u32(voxel_grid.source_kind, std::format("Dynamic scene viewport voxel grid \"{}\"", name)),
                .index_encoding = viewport_voxel_grid_index_encoding_from_u32(voxel_grid.index_encoding, std::format("Dynamic scene viewport voxel grid \"{}\"", name)),
                .buffer_id = voxel_grid.buffer_id,
                .source_byte_size = voxel_grid.source_byte_size,
                .index_count = voxel_grid.index_count,
                .revision = voxel_grid.revision,
                .dynamic = dynamic,
            };
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
            const std::string coordinate_system_name = abi_string(view.default_coordinate_system, "Dynamic scene document default coordinate system", true);
            if (!coordinate_system_name.empty()) document.default_coordinate_system = scene::coordinate_system(coordinate_system_name);
            const std::string active_camera_name = abi_string(view.active_camera_name, "Dynamic scene document active camera name", true);
            if (!active_camera_name.empty()) document.active_camera_name = active_camera_name;
            for (const SpectraDynamicSceneCamera& camera_view : abi_span(view.cameras, "Dynamic scene document cameras"))
                document.cameras.push_back(make_camera(camera_view));
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
            for (const SpectraDynamicSceneViewportSegmentSet& segment_set_view : abi_span(view.viewport_segment_sets, "Dynamic scene document viewport segment sets"))
                document.viewport_segment_sets.push_back(make_viewport_segment_set(segment_set_view, false));
            for (const SpectraDynamicSceneViewportVoxelGrid& voxel_grid_view : abi_span(view.viewport_voxel_grids, "Dynamic scene document viewport voxel grids"))
                document.viewport_voxel_grids.push_back(make_viewport_voxel_grid(voxel_grid_view, false));
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
            for (const SpectraDynamicSceneCamera& camera_view : abi_span(view.cameras, "Dynamic scene frame cameras"))
                snapshot.cameras.push_back(make_camera(camera_view));
            for (const SpectraDynamicSceneViewportSegmentSet& segment_set_view : abi_span(view.viewport_segment_sets, "Dynamic scene frame viewport segment sets"))
                snapshot.viewport_segment_sets.push_back(make_viewport_segment_set(segment_set_view, true));
            for (const SpectraDynamicSceneViewportVoxelGrid& voxel_grid_view : abi_span(view.viewport_voxel_grids, "Dynamic scene frame viewport voxel grids"))
                snapshot.viewport_voxel_grids.push_back(make_viewport_voxel_grid(voxel_grid_view, true));
            return snapshot;
        }

        struct DynamicScenePluginOptionStorage {
            std::string key{};
            std::string value{};
        };

        struct DynamicScenePluginOpenRequestStorage {
            std::filesystem::path plugin_path{};
            std::vector<DynamicScenePluginOptionStorage> options{};
            std::vector<SpectraDynamicSceneOption> option_views{};
            std::shared_ptr<DynamicSceneHostServices> host_services{};
            SpectraDynamicSceneHostServices host_services_view{};
            std::string source_id{};
        };

        [[nodiscard]] SpectraDynamicSceneString abi_text(const std::string& value) {
            return SpectraDynamicSceneString{.data = value.data(), .size = static_cast<std::uint64_t>(value.size())};
        }

        [[nodiscard]] SpectraDynamicSceneString abi_text(const std::string_view value) {
            return SpectraDynamicSceneString{.data = value.data(), .size = static_cast<std::uint64_t>(value.size())};
        }

        [[nodiscard]] std::uint32_t abi_gpu_resource_handle_kind(const DynamicSceneGpuResourceHandleKind kind) {
            switch (kind) {
            case DynamicSceneGpuResourceHandleKind::OpaqueWin32: return 1u;
            case DynamicSceneGpuResourceHandleKind::OpaqueFileDescriptor: return 2u;
            }
            throw std::runtime_error("Dynamic scene GPU resource handle kind is invalid");
        }

        [[nodiscard]] SpectraDynamicSceneGpuDeviceIdentity abi_gpu_device_identity(const DynamicSceneGpuDeviceIdentity& identity) {
            SpectraDynamicSceneGpuDeviceIdentity view{
                .vendor_id = identity.vendor_id,
                .device_id = identity.device_id,
                .device_node_mask = identity.device_node_mask,
            };
            for (std::size_t index = 0u; index < identity.device_uuid.size(); ++index) view.device_uuid[index] = identity.device_uuid[index];
            for (std::size_t index = 0u; index < identity.device_luid.size(); ++index) view.device_luid[index] = identity.device_luid[index];
            return view;
        }

        thread_local std::string dynamic_scene_host_service_callback_error{};

        [[nodiscard]] SpectraDynamicSceneResult request_viewport_voxel_buffer(void* user_data, const SpectraDynamicSceneViewportVoxelBufferRequest* request, SpectraDynamicSceneViewportVoxelBufferAllocation* allocation) noexcept {
            try {
                dynamic_scene_host_service_callback_error.clear();
                if (user_data == nullptr) throw std::runtime_error("Dynamic scene host services user data pointer is null");
                if (request == nullptr) throw std::runtime_error("Dynamic scene viewport voxel buffer request pointer is null");
                if (allocation == nullptr) throw std::runtime_error("Dynamic scene viewport voxel buffer allocation pointer is null");
                if (request->struct_size != sizeof(SpectraDynamicSceneViewportVoxelBufferRequest)) throw std::runtime_error("Dynamic scene viewport voxel buffer request ABI size mismatch");
                DynamicSceneHostServices& host_services = *static_cast<DynamicSceneHostServices*>(user_data);
                const DynamicSceneViewportVoxelBufferAllocation allocated = host_services.request_viewport_voxel_buffer(DynamicSceneViewportVoxelBufferRequest{
                    .byte_size = request->byte_size,
                    .debug_name = abi_string(request->debug_name, "Dynamic scene viewport voxel buffer debug name", true),
                });
                *allocation = SpectraDynamicSceneViewportVoxelBufferAllocation{
                    .struct_size = sizeof(SpectraDynamicSceneViewportVoxelBufferAllocation),
                    .resource_id = allocated.resource_id,
                    .byte_size = allocated.byte_size,
                    .handle_kind = abi_gpu_resource_handle_kind(allocated.handle_kind),
                    .handle = allocated.handle,
                    .device_identity = abi_gpu_device_identity(allocated.device_identity),
                };
                return SPECTRA_DYNAMIC_SCENE_RESULT_OK;
            } catch (const std::exception& error) {
                dynamic_scene_host_service_callback_error = error.what();
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            } catch (...) {
                dynamic_scene_host_service_callback_error = "unknown dynamic scene host service error";
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            }
        }

        [[nodiscard]] SpectraDynamicSceneResult release_viewport_voxel_buffer(void* user_data, const std::uint64_t resource_id) noexcept {
            try {
                dynamic_scene_host_service_callback_error.clear();
                if (user_data == nullptr) throw std::runtime_error("Dynamic scene host services user data pointer is null");
                DynamicSceneHostServices& host_services = *static_cast<DynamicSceneHostServices*>(user_data);
                host_services.release_viewport_voxel_buffer(resource_id);
                return SPECTRA_DYNAMIC_SCENE_RESULT_OK;
            } catch (const std::exception& error) {
                dynamic_scene_host_service_callback_error = error.what();
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            } catch (...) {
                dynamic_scene_host_service_callback_error = "unknown dynamic scene host service error";
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            }
        }

        [[nodiscard]] SpectraDynamicSceneString dynamic_scene_host_services_last_error(void* user_data) noexcept {
            if (user_data == nullptr) return abi_text(dynamic_scene_host_service_callback_error);
            DynamicSceneHostServices& host_services = *static_cast<DynamicSceneHostServices*>(user_data);
            const std::string_view service_error = host_services.last_error();
            if (!service_error.empty()) return abi_text(service_error);
            return abi_text(dynamic_scene_host_service_callback_error);
        }

        [[nodiscard]] SpectraDynamicSceneHostServices make_host_services_view(DynamicSceneHostServices& host_services) {
            return SpectraDynamicSceneHostServices{
                .struct_size = sizeof(SpectraDynamicSceneHostServices),
                .user_data = &host_services,
                .request_viewport_voxel_buffer = request_viewport_voxel_buffer,
                .release_viewport_voxel_buffer = release_viewport_voxel_buffer,
                .last_error = dynamic_scene_host_services_last_error,
            };
        }

        [[nodiscard]] bool parse_bool_default(const std::string_view value) {
            if (value == "true") return true;
            if (value == "false") return false;
            throw std::runtime_error("Dynamic scene open option bool default must be true or false");
        }

        [[nodiscard]] float parse_float_default(const std::string_view value, const std::string_view context) {
            float parsed{};
            const char* const begin = value.data();
            const char* const end = value.data() + value.size();
            const std::from_chars_result result = std::from_chars(begin, end, parsed);
            if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(parsed)) throw std::runtime_error(std::format("{} must be a finite float", context));
            return parsed;
        }

        [[nodiscard]] std::uint64_t parse_unsigned_integer_default(const std::string_view value, const std::string_view context) {
            std::uint64_t parsed{};
            const char* const begin = value.data();
            const char* const end = value.data() + value.size();
            const std::from_chars_result result = std::from_chars(begin, end, parsed);
            if (result.ec != std::errc{} || result.ptr != end) throw std::runtime_error(std::format("{} must be an unsigned integer", context));
            return parsed;
        }

        [[nodiscard]] std::filesystem::path normalized_dynamic_scene_plugin_path(const std::filesystem::path& plugin_path) {
            if (plugin_path.empty()) throw std::runtime_error("Dynamic scene plugin path must not be empty");
            const std::string path_text = plugin_path.string();
            if (path_text.find('?') != std::string::npos) throw std::runtime_error("Dynamic scene plugin Scene URI query is not supported; open the plugin path and configure it in the Project popover");
            const std::filesystem::path absolute_path = std::filesystem::absolute(plugin_path).lexically_normal();
            if (std::filesystem::is_directory(absolute_path)) throw std::runtime_error("Drop a dynamic scene plugin library, not a folder");
            if (!std::filesystem::is_regular_file(absolute_path)) throw std::runtime_error(std::format("{}: dynamic scene plugin file does not exist", absolute_path.string()));
            if (!is_dynamic_scene_plugin_file(absolute_path)) throw std::runtime_error(std::format("{}: dynamic scene plugin file extension is not supported on this platform", absolute_path.string()));
            return absolute_path;
        }

        [[nodiscard]] std::uint64_t fnv1a64_append(std::uint64_t hash, const std::string_view value) {
            for (const char character : value) {
                hash ^= static_cast<unsigned char>(character);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        [[nodiscard]] std::string make_dynamic_scene_source_id(const std::filesystem::path& plugin_path, const std::vector<DynamicScenePluginOptionStorage>& options) {
            std::vector<DynamicScenePluginOptionStorage> sorted_options = options;
            std::ranges::sort(sorted_options, {}, &DynamicScenePluginOptionStorage::key);
            std::uint64_t hash = 14695981039346656037ull;
            hash = fnv1a64_append(hash, plugin_path.string());
            for (const DynamicScenePluginOptionStorage& option : sorted_options) {
                hash = fnv1a64_append(hash, "\n");
                hash = fnv1a64_append(hash, option.key);
                hash = fnv1a64_append(hash, "=");
                hash = fnv1a64_append(hash, option.value);
            }
            return std::format("{}#dynamic-open-{:016x}", plugin_path.string(), hash);
        }

        [[nodiscard]] DynamicScenePluginOpenRequestStorage make_plugin_open_request_storage(DynamicSceneOpenRequest request) {
            DynamicScenePluginOpenRequestStorage storage{
                .plugin_path = normalized_dynamic_scene_plugin_path(request.plugin_path),
            };
            if (request.host_services == nullptr) throw std::runtime_error("Dynamic scene open request requires host services");
            storage.host_services = std::move(request.host_services);
            storage.host_services_view = make_host_services_view(*storage.host_services);
            std::set<std::string> option_keys{};
            storage.options.reserve(request.options.size());
            for (DynamicSceneOpenOption& option : request.options) {
                if (option.key.empty()) throw std::runtime_error("Dynamic scene open option key must not be empty");
                if (!option_keys.insert(option.key).second) throw std::runtime_error(std::format("Dynamic scene open option '{}' is duplicated", option.key));
                storage.options.push_back(DynamicScenePluginOptionStorage{
                    .key = std::move(option.key),
                    .value = std::move(option.value),
                });
            }
            storage.source_id = make_dynamic_scene_source_id(storage.plugin_path, storage.options);
            storage.option_views.reserve(storage.options.size());
            for (const DynamicScenePluginOptionStorage& option : storage.options) {
                storage.option_views.push_back(SpectraDynamicSceneOption{
                    .key = abi_text(option.key),
                    .value = abi_text(option.value),
                });
            }
            return storage;
        }

        [[nodiscard]] DynamicSceneOpenOptionKind make_open_option_kind(const std::uint32_t kind, const std::string_view context) {
            switch (kind) {
                case SPECTRA_DYNAMIC_SCENE_OPEN_OPTION_TEXT: return DynamicSceneOpenOptionKind::Text;
                case SPECTRA_DYNAMIC_SCENE_OPEN_OPTION_DIRECTORY_PATH: return DynamicSceneOpenOptionKind::DirectoryPath;
                case SPECTRA_DYNAMIC_SCENE_OPEN_OPTION_FILE_PATH: return DynamicSceneOpenOptionKind::FilePath;
                case SPECTRA_DYNAMIC_SCENE_OPEN_OPTION_CHOICE: return DynamicSceneOpenOptionKind::Choice;
                case SPECTRA_DYNAMIC_SCENE_OPEN_OPTION_BOOL: return DynamicSceneOpenOptionKind::Bool;
                case SPECTRA_DYNAMIC_SCENE_OPEN_OPTION_FLOAT: return DynamicSceneOpenOptionKind::Float;
                case SPECTRA_DYNAMIC_SCENE_OPEN_OPTION_UNSIGNED_INTEGER: return DynamicSceneOpenOptionKind::UnsignedInteger;
                default: throw std::runtime_error(std::format("{} has unknown kind {}", context, kind));
            }
        }

        [[nodiscard]] DynamicSceneOpenOptionSchema make_open_option_schema(const SpectraDynamicSceneOpenOptionSchema& schema, const std::string_view context) {
            DynamicSceneOpenOptionSchema converted{
                .key = abi_string(schema.key, std::format("{} key", context), false),
                .label = abi_string(schema.label, std::format("{} label", context), false),
                .description = abi_string(schema.description, std::format("{} description", context), true),
                .kind = make_open_option_kind(schema.kind, context),
                .required = schema.required != 0u,
                .default_value = abi_string(schema.default_value, std::format("{} default value", context), true),
            };
            if (schema.required != 0u && schema.required != 1u) throw std::runtime_error(std::format("{} required flag must be 0 or 1", context));
            const std::span<const SpectraDynamicSceneOpenOptionChoice> choices = abi_span(schema.choices.data, schema.choices.count, std::format("{} choices", context));
            if (converted.kind == DynamicSceneOpenOptionKind::Choice && choices.empty()) throw std::runtime_error(std::format("{} choice option must provide at least one choice", context));
            if (converted.kind != DynamicSceneOpenOptionKind::Choice && !choices.empty()) throw std::runtime_error(std::format("{} non-choice option must not provide choices", context));
            std::set<std::string> choice_values{};
            converted.choices.reserve(choices.size());
            for (std::size_t choice_index = 0u; choice_index < choices.size(); ++choice_index) {
                const SpectraDynamicSceneOpenOptionChoice& choice = choices[choice_index];
                DynamicSceneOpenOptionChoice converted_choice{
                    .value = abi_string(choice.value, std::format("{} choice {} value", context, choice_index), false),
                    .label = abi_string(choice.label, std::format("{} choice {} label", context, choice_index), false),
                };
                if (!choice_values.insert(converted_choice.value).second) throw std::runtime_error(std::format("{} choice value '{}' is duplicated", context, converted_choice.value));
                converted.choices.push_back(std::move(converted_choice));
            }
            if (converted.kind == DynamicSceneOpenOptionKind::Choice && !converted.default_value.empty() && !choice_values.contains(converted.default_value)) throw std::runtime_error(std::format("{} default value '{}' is not one of its choices", context, converted.default_value));
            if (converted.kind == DynamicSceneOpenOptionKind::Bool && !converted.default_value.empty()) static_cast<void>(parse_bool_default(converted.default_value));
            if (converted.kind == DynamicSceneOpenOptionKind::Float && !converted.default_value.empty()) static_cast<void>(parse_float_default(converted.default_value, std::format("{} default value", context)));
            if (converted.kind == DynamicSceneOpenOptionKind::UnsignedInteger && !converted.default_value.empty()) static_cast<void>(parse_unsigned_integer_default(converted.default_value, std::format("{} default value", context)));
            return converted;
        }

        [[nodiscard]] std::vector<DynamicSceneOpenOptionSchema> make_open_option_schemas(const SpectraDynamicSceneOpenOptionSchemaSpan schemas, const std::string_view context) {
            const std::span<const SpectraDynamicSceneOpenOptionSchema> schema_span = abi_span(schemas.data, schemas.count, context);
            std::set<std::string> schema_keys{};
            std::vector<DynamicSceneOpenOptionSchema> converted{};
            converted.reserve(schema_span.size());
            for (std::size_t schema_index = 0u; schema_index < schema_span.size(); ++schema_index) {
                DynamicSceneOpenOptionSchema schema = make_open_option_schema(schema_span[schema_index], std::format("{} {}", context, schema_index));
                if (!schema_keys.insert(schema.key).second) throw std::runtime_error(std::format("{} option '{}' is duplicated", context, schema.key));
                converted.push_back(std::move(schema));
            }
            return converted;
        }

        [[nodiscard]] DynamicSceneProjectAction make_project_action(const SpectraDynamicSceneProjectAction& action, const std::string_view context) {
            return DynamicSceneProjectAction{
                .id = abi_string(action.id, std::format("{} id", context), false),
                .label = abi_string(action.label, std::format("{} label", context), false),
                .description = abi_string(action.description, std::format("{} description", context), true),
                .options = make_open_option_schemas(action.options, std::format("{} option schema", context)),
            };
        }

        [[nodiscard]] std::vector<DynamicSceneProjectAction> make_project_actions(const SpectraDynamicSceneProjectActionSpan actions, const std::string_view context) {
            const std::span<const SpectraDynamicSceneProjectAction> action_span = abi_span(actions.data, actions.count, context);
            std::set<std::string> action_ids{};
            std::vector<DynamicSceneProjectAction> converted{};
            converted.reserve(action_span.size());
            for (std::size_t action_index = 0u; action_index < action_span.size(); ++action_index) {
                DynamicSceneProjectAction action = make_project_action(action_span[action_index], std::format("{} {}", context, action_index));
                if (!action_ids.insert(action.id).second) throw std::runtime_error(std::format("{} action '{}' is duplicated", context, action.id));
                converted.push_back(std::move(action));
            }
            return converted;
        }

        [[nodiscard]] DynamicSceneProjectStatus make_project_status(const SpectraDynamicSceneProjectStatusView& view, const std::string_view context) {
            if (view.struct_size != sizeof(SpectraDynamicSceneProjectStatusView)) throw std::runtime_error(std::format("{} ABI size mismatch", context));
            DynamicSceneProjectStatus status{
                .phase = abi_string(view.phase, std::format("{} phase", context), false),
                .headline = abi_string(view.headline, std::format("{} headline", context), false),
                .detail = abi_string(view.detail, std::format("{} detail", context), true),
            };
            const std::span<const SpectraDynamicSceneProjectMetric> metrics = abi_span(view.metrics.data, view.metrics.count, std::format("{} metrics", context));
            std::set<std::string> metric_keys{};
            status.metrics.reserve(metrics.size());
            for (std::size_t metric_index = 0u; metric_index < metrics.size(); ++metric_index) {
                const SpectraDynamicSceneProjectMetric& metric = metrics[metric_index];
                DynamicSceneProjectMetric converted{
                    .key = abi_string(metric.key, std::format("{} metric {} key", context, metric_index), false),
                    .label = abi_string(metric.label, std::format("{} metric {} label", context, metric_index), false),
                    .value = abi_string(metric.value, std::format("{} metric {} value", context, metric_index), false),
                };
                if (!metric_keys.insert(converted.key).second) throw std::runtime_error(std::format("{} metric '{}' is duplicated", context, converted.key));
                status.metrics.push_back(std::move(converted));
            }
            const std::span<const SpectraDynamicSceneString> enabled_action_ids = abi_span(view.enabled_action_ids.data, view.enabled_action_ids.count, std::format("{} enabled action ids", context));
            std::set<std::string> enabled_ids{};
            status.enabled_action_ids.reserve(enabled_action_ids.size());
            for (std::size_t enabled_index = 0u; enabled_index < enabled_action_ids.size(); ++enabled_index) {
                std::string action_id = abi_string(enabled_action_ids[enabled_index], std::format("{} enabled action id {}", context, enabled_index), false);
                if (!enabled_ids.insert(action_id).second) throw std::runtime_error(std::format("{} enabled action id '{}' is duplicated", context, action_id));
                status.enabled_action_ids.push_back(std::move(action_id));
            }
            return status;
        }

        [[nodiscard]] std::vector<DynamicSceneProjectLogEntry> make_project_logs(const SpectraDynamicSceneProjectLogView& view, const std::string_view context) {
            if (view.struct_size != sizeof(SpectraDynamicSceneProjectLogView)) throw std::runtime_error(std::format("{} ABI size mismatch", context));
            const std::span<const SpectraDynamicSceneProjectLogEntry> entries = abi_span(view.entries.data, view.entries.count, std::format("{} entries", context));
            std::vector<DynamicSceneProjectLogEntry> converted{};
            converted.reserve(entries.size());
            for (std::size_t entry_index = 0u; entry_index < entries.size(); ++entry_index) {
                const SpectraDynamicSceneProjectLogEntry& entry = entries[entry_index];
                converted.push_back(DynamicSceneProjectLogEntry{
                    .sequence = entry.sequence,
                    .level = abi_string(entry.level, std::format("{} entry {} level", context, entry_index), false),
                    .message = abi_string(entry.message, std::format("{} entry {} message", context, entry_index), false),
                });
            }
            return converted;
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
            explicit DynamicScenePluginLibrary(DynamicScenePluginOpenRequestStorage open_request) : open_request(std::move(open_request)), plugin_directory(this->open_request.plugin_path.parent_path()), native(this->open_request.plugin_path) {
                void* entry_address = this->native.symbol("spectra_dynamic_scene_plugin");
                const SpectraDynamicScenePluginEntryFn entry = reinterpret_cast<SpectraDynamicScenePluginEntryFn>(entry_address);
                this->plugin = entry();
                if (this->plugin == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin entry returned null", this->open_request.plugin_path.string()));
                this->validate_plugin_descriptor();
                this->scene_api = this->required_api<SpectraDynamicSceneSceneApi>(scene_api_name, scene_api_version);
                this->project_api = this->optional_api<SpectraDynamicSceneProjectApi>(project_api_name, project_api_version);
                this->validate_scene_api();
                this->validate_project_api();
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

            [[nodiscard]] std::string project_panel_title() const {
                return abi_string(this->plugin->project_panel_title, "Dynamic scene plugin project panel title", false);
            }

            [[nodiscard]] std::string open_action_label() const {
                return abi_string(this->plugin->open_action_label, "Dynamic scene plugin open action label", false);
            }

            [[nodiscard]] std::string open_action_description() const {
                return abi_string(this->plugin->open_action_description, "Dynamic scene plugin open action description", true);
            }

            [[nodiscard]] std::string source_id() const {
                return this->open_request.source_id;
            }

            [[nodiscard]] const std::filesystem::path& path() const {
                return this->open_request.plugin_path;
            }

            [[nodiscard]] const std::filesystem::path& directory() const {
                return this->plugin_directory;
            }

            [[nodiscard]] std::vector<DynamicSceneOpenOptionSchema> open_options() const {
                return make_open_option_schemas(this->plugin->open_options, "Dynamic scene plugin open option schema");
            }

            [[nodiscard]] std::vector<DynamicSceneProjectAction> project_actions() const {
                if (this->project_api == nullptr) return {};
                return make_project_actions(this->project_api->project_actions, "Dynamic scene plugin project action");
            }

            [[nodiscard]] double frames_per_second() const {
                return finite_double(this->scene_api->frames_per_second, "Dynamic scene plugin frame rate");
            }

            [[nodiscard]] scene::Scene::Document make_base_document() const {
                const std::string template_path_text = abi_string(this->scene_api->pbrt_template_path, "Dynamic scene plugin PBRT template path", true);
                if (template_path_text.empty()) {
                    return scene::Scene::Document{
                        .revision = scene::Scene::Revision{1},
                        .name = this->id(),
                        .title = this->title(),
                        .source = this->source_id(),
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
                document.source = this->source_id();
                document.frames_per_second = this->frames_per_second();
                document.timeline_enabled = true;
                return document;
            }

            void check_result(const SpectraDynamicSceneResult result, SpectraDynamicSceneInstance* instance, const std::string_view action) const {
                if (result == SPECTRA_DYNAMIC_SCENE_RESULT_OK) return;
                if (result != SPECTRA_DYNAMIC_SCENE_RESULT_ERROR) throw std::runtime_error(std::format("{} returned an unknown result code {}", action, static_cast<int>(result)));
                std::string error = abi_string(this->scene_api->last_error(instance), std::format("{} error message", action), true);
                if (error.empty()) error = "unknown plugin error";
                throw std::runtime_error(std::format("{} failed: {}", action, error));
            }

            [[nodiscard]] SpectraDynamicSceneInstance* create_instance() const {
                if (this->open_request.host_services == nullptr) throw std::runtime_error("Dynamic scene plugin instance creation requires host services");
                SpectraDynamicSceneInstance* instance{};
                const std::string plugin_path_text = this->open_request.plugin_path.string();
                const SpectraDynamicSceneOpenInfo open_info{
                    .struct_size = sizeof(SpectraDynamicSceneOpenInfo),
                    .plugin_path = abi_text(plugin_path_text),
                    .options = SpectraDynamicSceneOptionSpan{
                        .data = this->open_request.option_views.empty() ? nullptr : this->open_request.option_views.data(),
                        .count = static_cast<std::uint64_t>(this->open_request.option_views.size()),
                    },
                    .host_services = &this->open_request.host_services_view,
                };
                this->check_result(this->scene_api->create(&open_info, &instance), nullptr, "Dynamic scene plugin create");
                if (instance == nullptr) throw std::runtime_error("Dynamic scene plugin create returned a null instance");
                return instance;
            }

            void destroy_instance(SpectraDynamicSceneInstance* instance) const noexcept {
                if (instance != nullptr) this->scene_api->destroy(instance);
            }

            void reset(SpectraDynamicSceneInstance* instance) const {
                this->check_result(this->scene_api->reset(instance), instance, "Dynamic scene plugin reset");
            }

            void step(SpectraDynamicSceneInstance* instance, const float delta_seconds) const {
                this->check_result(this->scene_api->step(instance, delta_seconds), instance, "Dynamic scene plugin step");
            }

            void update_project(SpectraDynamicSceneInstance* instance, const float delta_seconds) const {
                if (this->project_api == nullptr) return;
                this->check_result(this->project_api->project_update(instance, delta_seconds), instance, "Dynamic scene plugin project update");
            }

            void project_action(SpectraDynamicSceneInstance* instance, const std::string_view action_id, const std::span<const DynamicSceneOpenOption> options) const {
                if (this->project_api == nullptr) throw std::runtime_error("Dynamic scene plugin does not expose a project API");
                if (action_id.empty()) throw std::runtime_error("Dynamic scene plugin project action id must not be empty");
                std::vector<DynamicScenePluginOptionStorage> option_storage{};
                std::vector<SpectraDynamicSceneOption> option_views{};
                std::set<std::string> option_keys{};
                option_storage.reserve(options.size());
                option_views.reserve(options.size());
                for (const DynamicSceneOpenOption& option : options) {
                    if (option.key.empty()) throw std::runtime_error("Dynamic scene project action option key must not be empty");
                    if (!option_keys.insert(option.key).second) throw std::runtime_error(std::format("Dynamic scene project action option '{}' is duplicated", option.key));
                    option_storage.push_back(DynamicScenePluginOptionStorage{
                        .key = option.key,
                        .value = option.value,
                    });
                }
                for (const DynamicScenePluginOptionStorage& option : option_storage) {
                    option_views.push_back(SpectraDynamicSceneOption{
                        .key = abi_text(option.key),
                        .value = abi_text(option.value),
                    });
                }
                this->check_result(
                    this->project_api->project_action(
                        instance,
                        abi_text(action_id),
                        SpectraDynamicSceneOptionSpan{
                            .data = option_views.empty() ? nullptr : option_views.data(),
                            .count = static_cast<std::uint64_t>(option_views.size()),
                        }),
                    instance,
                    std::format("Dynamic scene plugin project action '{}'", action_id));
            }

            [[nodiscard]] DynamicSceneProjectStatus project_status(SpectraDynamicSceneInstance* instance) const {
                if (this->project_api == nullptr) {
                    return DynamicSceneProjectStatus{
                        .phase = "Active",
                        .headline = "Dynamic scene active",
                    };
                }
                SpectraDynamicSceneProjectStatusView view{};
                this->check_result(this->project_api->project_status(instance, &view), instance, "Dynamic scene plugin project status");
                DynamicSceneProjectStatus status = make_project_status(view, "Dynamic scene plugin project status");
                std::set<std::string> action_ids{};
                for (const DynamicSceneProjectAction& action : this->project_actions()) action_ids.insert(action.id);
                for (const std::string& enabled_action_id : status.enabled_action_ids)
                    if (!action_ids.contains(enabled_action_id)) throw std::runtime_error(std::format("Dynamic scene plugin project status enabled unknown action '{}'", enabled_action_id));
                return status;
            }

            [[nodiscard]] std::vector<DynamicSceneProjectLogEntry> project_logs(SpectraDynamicSceneInstance* instance) const {
                if (this->project_api == nullptr) return {};
                SpectraDynamicSceneProjectLogView view{};
                this->check_result(this->project_api->project_logs(instance, &view), instance, "Dynamic scene plugin project logs");
                return make_project_logs(view, "Dynamic scene plugin project logs");
            }

            [[nodiscard]] SpectraDynamicSceneDocumentView document(SpectraDynamicSceneInstance* instance) const {
                SpectraDynamicSceneDocumentView view{};
                this->check_result(this->scene_api->document(instance, &view), instance, "Dynamic scene plugin document");
                return view;
            }

            [[nodiscard]] SpectraDynamicSceneFrameView frame(SpectraDynamicSceneInstance* instance, const scene::Scene::FrameInfo& frame_info) const {
                SpectraDynamicSceneFrameView view{};
                this->check_result(this->scene_api->frame(instance, SpectraDynamicSceneFrameInfo{.delta_seconds = frame_info.delta_seconds, .time_seconds = frame_info.time_seconds, .frame_index = frame_info.frame_index}, &view), instance, "Dynamic scene plugin frame");
                return view;
            }

        private:
            template <typename Api>
            [[nodiscard]] const Api* api(std::string_view name, const std::uint32_t version) const {
                const void* api_pointer{};
                const SpectraDynamicSceneResult result = this->plugin->get_api(abi_text(name), version, &api_pointer);
                if (result == SPECTRA_DYNAMIC_SCENE_RESULT_OK) return static_cast<const Api*>(api_pointer);
                if (result != SPECTRA_DYNAMIC_SCENE_RESULT_ERROR) throw std::runtime_error(std::format("{}: dynamic scene plugin get_api returned an unknown result code {}", this->open_request.plugin_path.string(), static_cast<int>(result)));
                throw std::runtime_error(std::format("{}: dynamic scene plugin get_api({}, {}) failed", this->open_request.plugin_path.string(), name, version));
            }

            template <typename Api>
            [[nodiscard]] const Api* required_api(std::string_view name, const std::uint32_t version) const {
                const Api* loaded_api = this->api<Api>(name, version);
                if (loaded_api == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin does not expose required API {} v{}", this->open_request.plugin_path.string(), name, version));
                return loaded_api;
            }

            template <typename Api>
            [[nodiscard]] const Api* optional_api(std::string_view name, const std::uint32_t version) const {
                return this->api<Api>(name, version);
            }

            void validate_plugin_descriptor() const {
                if (this->plugin->abi_version != plugin_abi_version) throw std::runtime_error(std::format("{}: dynamic scene plugin ABI version {} does not match host ABI version {}", this->open_request.plugin_path.string(), this->plugin->abi_version, plugin_abi_version));
                if (this->plugin->struct_size != sizeof(SpectraDynamicScenePlugin)) throw std::runtime_error(std::format("{}: dynamic scene plugin descriptor size mismatch", this->open_request.plugin_path.string()));
                static_cast<void>(this->id());
                static_cast<void>(this->title());
                static_cast<void>(this->project_panel_title());
                static_cast<void>(this->open_action_label());
                static_cast<void>(this->open_action_description());
                if (this->plugin->get_api == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin get_api function is null", this->open_request.plugin_path.string()));
                static_cast<void>(this->open_options());
            }

            void validate_scene_api() const {
                if (this->scene_api->struct_size != sizeof(SpectraDynamicSceneSceneApi)) throw std::runtime_error(std::format("{}: dynamic scene scene API descriptor size mismatch", this->open_request.plugin_path.string()));
                const double fps = this->frames_per_second();
                if (fps <= 0.0) throw std::runtime_error(std::format("{}: dynamic scene plugin frame rate must be positive", this->open_request.plugin_path.string()));
                if (this->scene_api->create == nullptr) throw std::runtime_error(std::format("{}: dynamic scene scene API create function is null", this->open_request.plugin_path.string()));
                if (this->scene_api->destroy == nullptr) throw std::runtime_error(std::format("{}: dynamic scene scene API destroy function is null", this->open_request.plugin_path.string()));
                if (this->scene_api->reset == nullptr) throw std::runtime_error(std::format("{}: dynamic scene scene API reset function is null", this->open_request.plugin_path.string()));
                if (this->scene_api->step == nullptr) throw std::runtime_error(std::format("{}: dynamic scene scene API step function is null", this->open_request.plugin_path.string()));
                if (this->scene_api->document == nullptr) throw std::runtime_error(std::format("{}: dynamic scene scene API document function is null", this->open_request.plugin_path.string()));
                if (this->scene_api->frame == nullptr) throw std::runtime_error(std::format("{}: dynamic scene scene API frame function is null", this->open_request.plugin_path.string()));
                if (this->scene_api->last_error == nullptr) throw std::runtime_error(std::format("{}: dynamic scene scene API last_error function is null", this->open_request.plugin_path.string()));
            }

            void validate_project_api() const {
                if (this->project_api == nullptr) return;
                if (this->project_api->struct_size != sizeof(SpectraDynamicSceneProjectApi)) throw std::runtime_error(std::format("{}: dynamic scene project API descriptor size mismatch", this->open_request.plugin_path.string()));
                if (this->project_api->project_update == nullptr) throw std::runtime_error(std::format("{}: dynamic scene project API project_update function is null", this->open_request.plugin_path.string()));
                if (this->project_api->project_action == nullptr) throw std::runtime_error(std::format("{}: dynamic scene project API project_action function is null", this->open_request.plugin_path.string()));
                if (this->project_api->project_status == nullptr) throw std::runtime_error(std::format("{}: dynamic scene project API project_status function is null", this->open_request.plugin_path.string()));
                if (this->project_api->project_logs == nullptr) throw std::runtime_error(std::format("{}: dynamic scene project API project_logs function is null", this->open_request.plugin_path.string()));
                static_cast<void>(this->project_actions());
            }

            DynamicScenePluginOpenRequestStorage open_request{};
            std::filesystem::path plugin_directory{};
            NativeLibrary native;
            const SpectraDynamicScenePlugin* plugin{};
            const SpectraDynamicSceneSceneApi* scene_api{};
            const SpectraDynamicSceneProjectApi* project_api{};
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

            void update_project(const float delta_seconds) override {
                this->plugin->update_project(this->instance, delta_seconds);
            }

            void execute_project_action(const std::string_view action_id, const std::span<const DynamicSceneOpenOption> options) override {
                this->plugin->project_action(this->instance, action_id, options);
            }

            [[nodiscard]] DynamicSceneProjectStatus project_status() const override {
                return this->plugin->project_status(this->instance);
            }

            [[nodiscard]] std::vector<DynamicSceneProjectLogEntry> project_logs() const override {
                return this->plugin->project_logs(this->instance);
            }

            [[nodiscard]] scene::Scene::Document create_scene_document() const override {
                scene::Scene::Document document = this->plugin->make_base_document();
                std::set<std::string> material_names = collect_material_names(document);
                std::set<std::string> light_names = collect_light_names(document);
                append_document_view(document, this->plugin->document(this->instance), material_names, light_names);
                if (document.active_camera_name.empty()) throw std::runtime_error(std::format("Dynamic scene plugin \"{}\" did not provide an active camera name", this->plugin->id()));
                if (document.cameras.empty()) throw std::runtime_error(std::format("Dynamic scene plugin \"{}\" did not provide a camera or PBRT template camera", this->plugin->id()));
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

    DynamicScenePluginInfo inspect_dynamic_scene_plugin(const std::filesystem::path& plugin_path) {
        DynamicScenePluginOpenRequestStorage request{
            .plugin_path = normalized_dynamic_scene_plugin_path(plugin_path),
        };
        request.source_id = make_dynamic_scene_source_id(request.plugin_path, request.options);
        DynamicScenePluginLibrary plugin{std::move(request)};
        return DynamicScenePluginInfo{
            .id = plugin.id(),
            .title = plugin.title(),
            .project_panel_title = plugin.project_panel_title(),
            .open_action_label = plugin.open_action_label(),
            .open_action_description = plugin.open_action_description(),
            .path = plugin.path(),
            .open_options = plugin.open_options(),
            .project_actions = plugin.project_actions(),
        };
    }

    DynamicScenePluginSource load_dynamic_scene_plugin(DynamicSceneOpenRequest request) {
        DynamicScenePluginOpenRequestStorage open_request = make_plugin_open_request_storage(std::move(request));
        const std::filesystem::path absolute_path = open_request.plugin_path;
        const std::string source_id = open_request.source_id;
        std::shared_ptr<DynamicScenePluginLibrary> plugin = std::make_shared<DynamicScenePluginLibrary>(std::move(open_request));
        return DynamicScenePluginSource{
            .id = source_id,
            .title = plugin->title(),
            .path = absolute_path,
            .create_source = [plugin = std::move(plugin)] {
                return std::make_unique<DynamicScenePluginSourceInstance>(plugin);
            },
        };
    }
} // namespace spectra::scene_runtime
