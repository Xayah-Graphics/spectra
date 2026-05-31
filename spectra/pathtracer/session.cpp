#include "session.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>
#include <utility>

namespace xayah::pathtracer {
    [[nodiscard]] std::string scene_title_text(const SceneSession& scene) {
        const std::string title = scene.scene_path.filename().string();
        if (!title.empty()) return title;
        return scene.scene_path.string();
    }

    void InteractiveSession::RollingFloatAverage::clear() {
        this->values.fill(0.0f);
        this->count = 0;
        this->cursor = 0;
        this->sum = 0.0f;
    }

    void InteractiveSession::RollingFloatAverage::add(const float value) {
        if (!std::isfinite(value) || value < 0.0f) throw std::runtime_error("Rolling statistic value must be finite and non-negative");
        if (this->count < sample_count) {
            this->values[this->cursor] = value;
            this->sum += value;
            ++this->count;
        } else {
            this->sum -= this->values[this->cursor];
            this->values[this->cursor] = value;
            this->sum += value;
        }
        this->cursor = (this->cursor + 1) % sample_count;
    }

    bool InteractiveSession::RollingFloatAverage::has_value() const {
        return this->count > 0;
    }

    float InteractiveSession::RollingFloatAverage::average() const {
        if (this->count == 0) return 0.0f;
        return this->sum / static_cast<float>(this->count);
    }

    InteractiveSession::InteractiveSession(std::filesystem::path scene_path) {
        if (scene_path.empty()) throw std::runtime_error("Spectra pathtracer plugin requires a scene path");
        this->state.scene_path = std::move(scene_path);
    }

    InteractiveSession::~InteractiveSession() noexcept = default;

    void InteractiveSession::attach(HostContext host) {
        if (this->state.attached) throw std::runtime_error("Spectra pathtracer plugin is already attached");
        this->update_host(host);
        this->state.attached = true;
        try {
            this->state.gpu_runtime = std::make_unique<RuntimeSession>();
            std::unique_ptr<SceneSession> loaded_scene = std::make_unique<SceneSession>();
            try {
                loaded_scene->load(this->state.scene_path);
                this->state.spectra_scene = std::move(loaded_scene);
            } catch (...) {
                loaded_scene->unload_noexcept();
                throw;
            }
        } catch (...) {
            this->detach();
            throw;
        }
    }

    void InteractiveSession::detach() noexcept {
        try {
            if (this->state.attached && this->state.host.device != nullptr) this->state.host.device->waitIdle();
        } catch (...) {
        }
        this->unload_pathtracer_noexcept();
        this->unload_spectra_scene_noexcept();
        this->state.gpu_runtime.reset();
        this->state.host = HostContext{};
        this->state.attached = false;
    }

    void InteractiveSession::update_host(HostContext host) {
        if (host.physical_device == nullptr) throw std::runtime_error("Spectra pathtracer host physical device is null");
        if (host.device == nullptr) throw std::runtime_error("Spectra pathtracer host logical device is null");
        if (host.frame_count == 0) throw std::runtime_error("Spectra pathtracer host frame count must be positive");
        if (host.swapchain_extent.width == 0 || host.swapchain_extent.height == 0) throw std::runtime_error("Spectra pathtracer host swapchain extent must be positive");
        this->state.host = host;
    }

    void InteractiveSession::before_imgui_shutdown() noexcept {
        if (this->state.gpu_pathtracer != nullptr) this->state.gpu_pathtracer->release_viewport_descriptors_noexcept();
    }

    void InteractiveSession::after_imgui_created() {
        if (this->state.gpu_pathtracer != nullptr) this->state.gpu_pathtracer->create_viewport_descriptors();
    }

    FrameOutput InteractiveSession::begin_frame(const FrameInput& frame) {
        if (this->state.spectra_scene == nullptr) throw std::runtime_error("Cannot update Spectra pathtracer frame without an active Spectra scene");
        FrameOutput output{};
        this->synchronize_render_resolution();
        if (this->pathtracer_ready()) {
            output.close_requested = this->process_camera_input();
            const PathtracerSession::RenderFrameResult render_result = this->state.gpu_pathtracer->render_frame(frame.frame_index, this->state.camera.moving_from_camera);
            output.completion_semaphore = this->state.gpu_pathtracer->active_cuda_complete_semaphore();
            this->update_frame_statistics(frame, render_result.rendered_sample, render_result.reset_accumulation, render_result.sample_pixels);
        } else {
            this->update_frame_statistics(frame, false, false, 0);
        }
        return output;
    }

    void InteractiveSession::record_frame(const vk::raii::CommandBuffer& command_buffer) {
        if (this->pathtracer_ready()) this->state.gpu_pathtracer->record_copy(command_buffer);
    }

    std::string InteractiveSession::window_detail() const {
        std::uint32_t width = this->state.host.swapchain_extent.width;
        std::uint32_t height = this->state.host.swapchain_extent.height;
        if (this->state.render_resolution_sync.pathtracer_created) {
            width = static_cast<std::uint32_t>(this->state.render_resolution_sync.active_resolution[0]);
            height = static_cast<std::uint32_t>(this->state.render_resolution_sync.active_resolution[1]);
        } else if (this->state.ui.viewport_known && this->state.ui.viewport_framebuffer_size[0] > 0 && this->state.ui.viewport_framebuffer_size[1] > 0) {
            width = static_cast<std::uint32_t>(this->state.ui.viewport_framebuffer_size[0]);
            height = static_cast<std::uint32_t>(this->state.ui.viewport_framebuffer_size[1]);
        }
        const std::string scene_title = this->state.spectra_scene == nullptr ? "No Scene" : scene_title_text(*this->state.spectra_scene);
        const std::array<int, 2> sample_range = this->state.spectra_scene == nullptr ? std::array<int, 2>{0, 0} : this->pathtracer_sample_range();
        return std::format("{} | Spectra Pathtracer | {}x{} | sample {}/{}", scene_title, width, height, sample_range[0], sample_range[1]);
    }

    void InteractiveSession::unload_spectra_scene_noexcept() noexcept {
        if (this->state.spectra_scene != nullptr) {
            this->state.spectra_scene->unload_noexcept();
            this->state.spectra_scene.reset();
        }
    }

    void InteractiveSession::create_pathtracer_for_resolution(const std::array<int, 2>& resolution) {
        if (this->state.spectra_scene == nullptr) throw std::runtime_error("Cannot create Spectra pathtracer without a loaded Spectra scene");
        if (this->state.gpu_pathtracer != nullptr) throw std::runtime_error("Spectra pathtracer is already loaded");
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot create Spectra pathtracer with a non-positive resolution");
        try {
            if (this->state.gpu_runtime == nullptr) throw std::runtime_error("Spectra pathtracer runtime is not initialized");
            this->state.gpu_runtime->reset_options_for_scene();
            this->state.gpu_pathtracer = std::make_unique<PathtracerSession>(*this->state.spectra_scene, resolution, *this->state.host.physical_device, *this->state.host.device, this->state.host.frame_count);
            this->state.spectra_scene->set_runtime_metadata(this->state.gpu_pathtracer->film_resolution(), this->state.gpu_pathtracer->sampler_sample_count(), this->state.gpu_pathtracer->camera_from_world_transform());
            this->state.render_resolution_sync.active_resolution = resolution;
            this->state.render_resolution_sync.pathtracer_created  = true;
        } catch (...) {
            if (this->state.gpu_runtime != nullptr) this->state.gpu_runtime->wait_gpu_noexcept();
            this->unload_pathtracer_noexcept();
            throw;
        }
    }

    void InteractiveSession::rebuild_pathtracer_for_resolution(const std::array<int, 2>& resolution) {
        if (this->state.render_resolution_sync.rebuilding) throw std::runtime_error("Spectra pathtracer resolution rebuild is already active");
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot rebuild Spectra pathtracer with a non-positive resolution");
        if (this->state.render_resolution_sync.pathtracer_created && this->state.render_resolution_sync.active_resolution == resolution) return;

        const bool preserve_camera = this->state.camera.initialized;
        const InteractiveCameraPose preserved_pose{this->state.camera.eye, this->state.camera.center, this->state.camera.up, this->state.camera.basis_handedness};
        const float preserved_speed     = this->state.camera.speed;
        const int preserved_samples     = this->state.gpu_pathtracer == nullptr ? 0 : this->state.gpu_pathtracer->target_sample_count();
        const float preserved_exposure  = this->state.gpu_pathtracer == nullptr ? 1.0f : this->state.gpu_pathtracer->current_exposure();
        this->state.render_resolution_sync.rebuilding = true;
        try {
            this->state.host.device->waitIdle();
            if (this->state.gpu_runtime != nullptr) this->state.gpu_runtime->wait_gpu_noexcept();
            this->unload_pathtracer_noexcept();
            this->create_pathtracer_for_resolution(resolution);
            if (this->state.gpu_pathtracer == nullptr) throw std::runtime_error("Spectra pathtracer was not created");
            if (preserved_samples > 0) this->state.gpu_pathtracer->set_target_sample_count(preserved_samples);
            this->state.gpu_pathtracer->set_exposure(preserved_exposure);
            if (preserve_camera) {
                this->state.camera.camera_from_world          = this->state.spectra_scene->camera_from_world;
                this->state.camera.eye                        = preserved_pose.eye;
                this->state.camera.center                     = preserved_pose.center;
                this->state.camera.up                         = preserved_pose.up;
                this->state.camera.basis_handedness           = preserved_pose.basis_handedness;
                this->state.camera.speed                      = preserved_speed;
                this->state.camera.fov_degrees                = interactive_camera_fov_degrees(*this->state.spectra_scene);
                this->state.camera.mouse_position_known       = false;
                this->state.camera.input_enabled              = false;
                this->state.camera.moving_from_camera         = moving_from_camera_from_interactive_pose(this->state.camera.camera_from_world, preserved_pose);
            } else
                this->initialize_camera_state();
            this->clear_pathtracer_throughput_statistics();
            this->state.statistics.last_frame_rendered_sample = false;
            this->state.render_resolution_sync.rebuilding     = false;
        } catch (...) {
            this->state.render_resolution_sync.rebuilding = false;
            throw;
        }
    }

    void InteractiveSession::unload_pathtracer_noexcept() noexcept {
        if (this->state.gpu_runtime != nullptr) this->state.gpu_runtime->wait_gpu_noexcept();
        this->state.gpu_pathtracer.reset();
        this->state.render_resolution_sync.pathtracer_created  = false;
        this->state.render_resolution_sync.active_resolution = {0, 0};
    }

    void InteractiveSession::observe_viewport_render_resolution(const std::array<int, 2>& resolution) {
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Viewport framebuffer resolution must be positive");
        const ImGuiIO& io = ImGui::GetIO();
        if (!std::isfinite(io.DeltaTime) || io.DeltaTime < 0.0f) throw std::runtime_error("ImGui delta time is invalid while tracking viewport resolution");
        if (!this->state.render_resolution_sync.candidate_known || this->state.render_resolution_sync.candidate_resolution != resolution) {
            this->state.render_resolution_sync.candidate_known     = true;
            this->state.render_resolution_sync.candidate_resolution = resolution;
            this->state.render_resolution_sync.stable_seconds      = 0.0f;
            return;
        }
        this->state.render_resolution_sync.stable_seconds += io.DeltaTime;
    }

    void InteractiveSession::synchronize_render_resolution() {
        constexpr float resolution_stability_seconds = 0.3f;
        if (this->state.spectra_scene == nullptr) return;
        if (!this->state.render_resolution_sync.candidate_known) return;
        if (this->state.render_resolution_sync.stable_seconds < resolution_stability_seconds) return;
        if (this->state.render_resolution_sync.pathtracer_created && this->state.render_resolution_sync.active_resolution == this->state.render_resolution_sync.candidate_resolution) return;
        this->rebuild_pathtracer_for_resolution(this->state.render_resolution_sync.candidate_resolution);
    }

    [[nodiscard]] bool InteractiveSession::pathtracer_ready() const {
        return this->state.render_resolution_sync.pathtracer_created && this->state.gpu_pathtracer != nullptr;
    }

    [[nodiscard]] std::array<int, 2> InteractiveSession::pathtracer_sample_range() const {
        if (this->state.gpu_pathtracer == nullptr) return {0, 0};
        return {this->state.gpu_pathtracer->current_sample(), this->state.gpu_pathtracer->target_sample_count()};
    }

    void InteractiveSession::request_pathtracer_accumulation_reset() {
        if (this->state.gpu_pathtracer == nullptr) throw std::runtime_error("Cannot reset Spectra pathtracer accumulation without an active Spectra pathtracer session");
        this->state.gpu_pathtracer->request_reset_accumulation();
        this->clear_pathtracer_throughput_statistics();
    }

    void InteractiveSession::initialize_camera_state() {
        if (this->state.spectra_scene == nullptr) throw std::runtime_error("Cannot initialize camera state without an active Spectra scene");
        if (this->state.gpu_pathtracer == nullptr) throw std::runtime_error("Spectra pathtracer camera focus bounds requested without an active Spectra pathtracer session");
        const float initial_move_scale = this->state.gpu_pathtracer->camera_initial_move_scale();
        if (!std::isfinite(initial_move_scale) || !(initial_move_scale > 0.0f)) throw std::runtime_error("Initial camera move scale must be finite and positive");
        this->state.camera.camera_from_world = this->state.spectra_scene->camera_from_world;
        const InteractiveCameraPose pose   = interactive_camera_pose_from_base_transform(this->state.camera.camera_from_world, this->state.gpu_pathtracer->camera_initial_focus_bounds());
        this->state.camera.initialized       = true;
        this->state.camera.input_enabled     = false;
        this->state.camera.speed             = initial_move_scale * 60.0f;
        this->state.camera.fov_degrees       = interactive_camera_fov_degrees(*this->state.spectra_scene);
        this->state.camera.basis_handedness  = pose.basis_handedness;
        this->state.camera.eye               = pose.eye;
        this->state.camera.center            = pose.center;
        this->state.camera.up                = pose.up;
        this->state.camera.mouse_position    = {0.0f, 0.0f};
        this->state.camera.mouse_position_known = false;
        this->state.camera.moving_from_camera   = spectra::Transform{};
    }

    void InteractiveSession::set_camera_speed(const float speed) {
        if (!std::isfinite(speed) || !(speed > 0.0f)) throw std::runtime_error("Camera speed must be finite and positive");
        this->state.camera.speed = speed;
    }

    void InteractiveSession::reset_camera() {
        if (!this->state.camera.initialized) throw std::runtime_error("Cannot reset camera before camera state is initialized");
        if (!this->pathtracer_ready()) throw std::runtime_error("Cannot reset camera without an active Spectra pathtracer");
        const InteractiveCameraPose pose  = interactive_camera_pose_from_base_transform(this->state.camera.camera_from_world, this->state.gpu_pathtracer->camera_initial_focus_bounds());
        this->state.camera.eye              = pose.eye;
        this->state.camera.center           = pose.center;
        this->state.camera.up               = pose.up;
        this->state.camera.basis_handedness = pose.basis_handedness;
        this->state.camera.mouse_position_known = false;
        this->state.camera.moving_from_camera   = spectra::Transform{};
        this->request_pathtracer_accumulation_reset();
    }

    void InteractiveSession::clear_pathtracer_throughput_statistics() {
        this->state.statistics.throughput_mspp.clear();
        this->state.statistics.last_valid_throughput_mspp = 0.0f;
        this->state.statistics.has_throughput             = false;
    }

    void InteractiveSession::update_frame_statistics(const FrameInput& frame, const bool rendered_sample, const bool reset_accumulation, const std::uint64_t sample_pixels) {
        const ImGuiIO& io = ImGui::GetIO();
        if (!std::isfinite(io.DeltaTime) || !(io.DeltaTime > 0.0f)) throw std::runtime_error("ImGui frame delta time must be finite and positive for statistics");
        if (!rendered_sample && sample_pixels != 0) throw std::runtime_error("Spectra pathtracer frame statistics reported sample-pixels without rendering a sample");
        if (rendered_sample && sample_pixels == 0) throw std::runtime_error("Spectra pathtracer frame statistics rendered a sample without sample-pixels");

        const float frame_milliseconds = io.DeltaTime * 1000.0f;
        ++this->state.statistics.current_frame_id;
        this->state.statistics.active_frame_index           = frame.frame_index;
        this->state.statistics.active_swapchain_image_index = frame.image_index;
        this->state.statistics.last_frame_milliseconds      = frame_milliseconds;
        this->state.statistics.last_frame_rendered_sample   = rendered_sample;
        this->state.statistics.frame_milliseconds.add(frame_milliseconds);

        if (reset_accumulation) this->clear_pathtracer_throughput_statistics();
        if (rendered_sample) {
            const float throughput = (static_cast<float>(sample_pixels) / 1000000.0f) / io.DeltaTime;
            this->state.statistics.throughput_mspp.add(throughput);
            this->state.statistics.last_valid_throughput_mspp = throughput;
            this->state.statistics.has_throughput             = true;
        }
    }

    [[nodiscard]] InteractiveSession::PathtracerStatus InteractiveSession::pathtracer_status() const {
        PathtracerStatus status{};
        const std::array<int, 2> sample_range = this->pathtracer_sample_range();
        if (this->state.render_resolution_sync.rebuilding) {
            status.state = "Rebuilding";
            return status;
        }
        if (!this->state.render_resolution_sync.pathtracer_created) {
            status.state = this->state.render_resolution_sync.candidate_known ? "Pending Resolution" : "Waiting for Viewport";
            return status;
        }
        status.uses_external_completion = this->state.gpu_pathtracer != nullptr;
        if (this->state.gpu_pathtracer == nullptr) {
            status.state = "Unavailable";
            return status;
        }
        status.state = sample_range[0] >= sample_range[1] ? "Completed" : "Sampling";
        return status;
    }

    bool InteractiveSession::process_camera_input() {
        ImGuiIO& io = ImGui::GetIO();
        const bool close_requested = !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false);

        const ImVec2 mouse_position = io.MousePos;
        const bool in_viewport_rect = this->state.ui.viewport_known && mouse_position.x >= this->state.ui.viewport_position[0] && mouse_position.x < this->state.ui.viewport_position[0] + this->state.ui.viewport_size[0] && mouse_position.y >= this->state.ui.viewport_position[1] && mouse_position.y < this->state.ui.viewport_position[1] + this->state.ui.viewport_size[1];
        this->state.camera.input_enabled  = in_viewport_rect && (this->state.ui.viewport_hovered || this->state.ui.viewport_focused) && !io.WantTextInput;
        if (!this->state.camera.input_enabled) {
            this->state.camera.mouse_position_known = false;
            return close_requested;
        }
        if (!this->state.camera.initialized) throw std::runtime_error("Cannot process camera input before camera state is initialized");

        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            this->reset_camera();
            return close_requested;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) this->set_camera_speed(this->state.camera.speed * 2.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) this->set_camera_speed(this->state.camera.speed * 0.5f);

        const bool shift = io.KeyShift;
        const bool ctrl  = io.KeyCtrl;
        const bool alt   = io.KeyAlt;
        InteractiveCameraPose pose{this->state.camera.eye, this->state.camera.center, this->state.camera.up, this->state.camera.basis_handedness};
        bool camera_changed = false;
        if (!alt) {
            if (!std::isfinite(io.DeltaTime) || io.DeltaTime < 0.0f) throw std::runtime_error("ImGui delta time is invalid");
            float key_motion_factor = io.DeltaTime;
            if (shift) key_motion_factor *= 5.0f;
            if (ctrl) key_motion_factor *= 0.1f;
            if (key_motion_factor > 0.0f) {
                if (ImGui::IsKeyDown(ImGuiKey_W)) camera_changed = interactive_camera_key_motion(pose, {key_motion_factor, 0.0f}, this->state.camera.speed, true) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_S)) camera_changed = interactive_camera_key_motion(pose, {-key_motion_factor, 0.0f}, this->state.camera.speed, true) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) camera_changed = interactive_camera_key_motion(pose, {key_motion_factor, 0.0f}, this->state.camera.speed, false) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) camera_changed = interactive_camera_key_motion(pose, {-key_motion_factor, 0.0f}, this->state.camera.speed, false) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) camera_changed = interactive_camera_key_motion(pose, {0.0f, key_motion_factor}, this->state.camera.speed, false) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) camera_changed = interactive_camera_key_motion(pose, {0.0f, -key_motion_factor}, this->state.camera.speed, false) || camera_changed;
            }
        }

        const std::array<float, 2> viewport_size = this->state.ui.viewport_size;
        if (!std::isfinite(viewport_size[0]) || !std::isfinite(viewport_size[1]) || !(viewport_size[0] > 0.0f) || !(viewport_size[1] > 0.0f)) throw std::runtime_error("Camera viewport size must be finite and positive");
        const std::array<float, 2> current_mouse_position{mouse_position.x, mouse_position.y};
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right, false)) {
            this->state.camera.mouse_position       = current_mouse_position;
            this->state.camera.mouse_position_known = true;
        }

        const bool left_dragging   = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f);
        const bool middle_dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.0f);
        const bool right_dragging  = ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f);
        if (left_dragging || middle_dragging || right_dragging) {
            if (!this->state.camera.mouse_position_known) {
                this->state.camera.mouse_position       = current_mouse_position;
                this->state.camera.mouse_position_known = true;
            }
            const std::array<float, 2> mouse_displacement{
                (current_mouse_position[0] - this->state.camera.mouse_position[0]) / viewport_size[0],
                (current_mouse_position[1] - this->state.camera.mouse_position[1]) / viewport_size[1],
            };
            if (left_dragging) {
                if ((ctrl && shift) || alt) camera_changed = interactive_camera_orbit(pose, {mouse_displacement[0], -mouse_displacement[1]}, true) || camera_changed;
                else if (shift) camera_changed = interactive_camera_dolly(pose, mouse_displacement) || camera_changed;
                else if (ctrl) camera_changed = interactive_camera_pan(pose, mouse_displacement, this->state.camera.fov_degrees, viewport_size) || camera_changed;
                else camera_changed = interactive_camera_orbit(pose, mouse_displacement, false) || camera_changed;
            } else if (middle_dragging) {
                camera_changed = interactive_camera_pan(pose, mouse_displacement, this->state.camera.fov_degrees, viewport_size) || camera_changed;
            } else if (right_dragging) {
                camera_changed = interactive_camera_dolly(pose, mouse_displacement) || camera_changed;
            }
            this->state.camera.mouse_position = current_mouse_position;
        }

        if (io.MouseWheel != 0.0f && !shift) {
            constexpr float wheel_speed = 10.0f;
            const float wheel_value     = io.MouseWheel * wheel_speed;
            const float dolly_delta     = wheel_value * std::abs(wheel_value) / viewport_size[0];
            camera_changed              = interactive_camera_dolly(pose, {dolly_delta, 0.0f}) || camera_changed;
        }

        if (camera_changed) {
            this->state.camera.eye                  = pose.eye;
            this->state.camera.center               = pose.center;
            this->state.camera.up                   = pose.up;
            this->state.camera.basis_handedness     = pose.basis_handedness;
            this->state.camera.moving_from_camera   = moving_from_camera_from_interactive_pose(this->state.camera.camera_from_world, pose);
            this->request_pathtracer_accumulation_reset();
        }
        return close_requested;
    }

} // namespace xayah::pathtracer
