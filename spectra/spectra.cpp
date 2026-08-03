module;

#include <imgui.h>
#include <imgui_impl_glfw.h>

module spectra;

import std;
import vulkan;

namespace spectra {
    Spectra::Spectra(std::optional<std::filesystem::path> scene_path, const std::filesystem::path& scene_library_path, std::vector<std::filesystem::path> session_scene_roots, const std::filesystem::path& shader_directory, std::optional<std::string> initial_renderer) : runtime("Spectra", {1920, 1080}), dynamics(runtime, document), gpu_scene(runtime, document, dynamics, shader_directory), renderers(runtime, gpu_scene, shader_directory, std::move(initial_renderer)), editor(runtime, document, dynamics, gpu_scene, renderers, shader_directory, scene_library_path, std::move(session_scene_roots)) {
        this->editor.initialize(std::move(scene_path));
    }

    void Spectra::prepare_rendering(const vk::raii::CommandBuffer& command_buffer, const vk::Extent2D extent) {
        scene::SceneChange binding_changes{scene::SceneChange::None};
        bool gpu_scene_synchronized{};
        if (this->dynamics.configuration.initialized)
            if (const dynamics::DynamicFrame* frame = this->dynamics.pending_frame()) {
                binding_changes        = this->gpu_scene.apply(*frame, command_buffer);
                gpu_scene_synchronized = true;
            }

        const scene::SceneRevision revision = this->document.content.evaluated.revision();
        const bool scene_changed            = revision.number != this->editor.rendering.synchronized_scene_revision;
        if (scene_changed) {
            if (!gpu_scene_synchronized) binding_changes = this->gpu_scene.synchronize(command_buffer);
            scene::SceneView synchronized_scene = this->document.content.evaluated.view();
            synchronized_scene.revision.changes = synchronized_scene.revision.changes | binding_changes;
            this->editor.viewport.synchronize(synchronized_scene, command_buffer);
            this->renderers.invalidate(synchronized_scene.revision.changes);
            this->editor.rendering.synchronized_scene_revision = revision.number;
            this->editor.interaction.prune_selection();
        }

        const float aspect              = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const bool aspect_changed       = aspect != this->editor.interaction.view.aspect;
        const bool scene_camera_changed = scene_changed && (revision.changes & scene::SceneChange::Camera) != scene::SceneChange::None;
        this->editor.interaction.view.aspect = aspect;
        const bool camera_changed = aspect_changed || this->editor.interaction.view.source != this->editor.interaction.view.synchronized_source || (this->editor.interaction.view.source == CameraSource::Viewport && (scene_camera_changed || this->editor.interaction.view.camera_revision != this->editor.interaction.view.synchronized_camera_revision)) || (this->editor.interaction.view.source == CameraSource::Scene && this->document.content.source.camera().revision != this->editor.interaction.view.synchronized_scene_camera_revision);
        if (camera_changed) this->editor.interaction.camera_changed();

        this->editor.output.record_frozen_scene_snapshot(command_buffer, this->runtime.frames.frame.current_slot_index, this->editor.interaction.view.render_camera, extent, this->document.content.path);

        if (scene_changed) this->document.content.evaluated.acknowledge_changes();
        if (this->document.content.source.revision().changes != scene::SceneChange::None) this->document.content.source.acknowledge_changes();
        this->editor.viewport.set_camera(this->editor.interaction.view.render_camera);
        this->renderers.prepare(this->document.content.evaluated.view(), RenderView{this->editor.interaction.view.render_camera, extent, this->editor.interaction.view.render_camera_revision}, command_buffer);
        if (this->dynamics.configuration.initialized) this->dynamics.consume_frame();
    }

    void Spectra::record_rendering(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index) {
        this->renderers.record(command_buffer, frame_slot_index);
        this->editor.viewport.record_picker(command_buffer, frame_slot_index);
    }

    void Spectra::record_editor_overlays(const vk::raii::CommandBuffer& command_buffer, const bool show_axes) {
        this->editor.viewport.record_overlay(command_buffer, show_axes, this->editor.interaction);
    }

    RenderOutput Spectra::current_render_output() const noexcept {
        return this->renderers.output();
    }

    void Spectra::run(const std::optional<std::uint64_t> maximum_frame_count) {
        while (true) {
            if (maximum_frame_count && this->editor.timing.presented_frames >= *maximum_frame_count) break;
            this->runtime.platform.poll_events();
            if (this->runtime.platform.take_close_request()) break;
            this->editor.handle_dropped_scene_paths();

            const std::optional<FrameContext> frame = this->runtime.frames.begin_frame();
            if (!frame) {
                this->runtime.platform.wait_events();
                this->editor.timing.simulation_sample_valid = false;
                continue;
            }

            const std::chrono::steady_clock::time_point current_clock_sample = std::chrono::steady_clock::now();
            const bool simulation_clock_active                               = this->document.content.loaded && !this->editor.library.visible;
            if (this->document.content.loaded) {
                this->editor.begin_frame(frame->slot_index);
                if (simulation_clock_active && this->editor.timing.simulation_sample_valid && this->dynamics.configuration.initialized) this->dynamics.advance(current_clock_sample - this->editor.timing.previous_simulation_sample);
                this->editor.viewport.resize(frame->presentation_target.extent);
            }
            this->editor.timing.previous_simulation_sample = current_clock_sample;
            this->editor.timing.simulation_sample_valid    = simulation_clock_active;

            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            const EditorActions actions = this->editor.library.visible ? this->editor.ui.draw_scene_library(this->editor.library.scenes, this->editor.library.problems, this->document.content.loaded) : this->editor.ui.draw_editor_ui();
            this->runtime.platform.window_drag_regions = actions.window_drag_regions;
            this->editor.handle_actions(actions);
            if (!this->editor.timing.simulation_sample_valid && this->document.content.loaded && !this->editor.library.visible) {
                this->editor.timing.previous_simulation_sample = std::chrono::steady_clock::now();
                this->editor.timing.simulation_sample_valid    = true;
            }
            ImGui::Render();

            if (this->document.content.loaded) {
                this->editor.viewport.resize(frame->presentation_target.extent);
                const vk::Extent2D viewport_extent = this->editor.viewport.target.image.extent;
                this->prepare_rendering(frame->command_buffer, viewport_extent);
                this->record_rendering(frame->command_buffer, frame->slot_index);
                const RenderOutput output = this->current_render_output();
                this->editor.output.record_presenter(frame->command_buffer, output);
                this->record_editor_overlays(frame->command_buffer, actions.show_axes);
                this->editor.output.record_capture(frame->command_buffer, frame->slot_index, output);
            }

            this->editor.ui.record_imgui(*ImGui::GetDrawData(), frame->command_buffer, frame->slot_index, frame->presentation_target.image, frame->presentation_target.view, frame->presentation_target.extent, frame->presentation_target.image_layout, vk::ImageLayout::ePresentSrcKHR);
            if (this->runtime.frames.present_frame()) ++this->editor.timing.presented_frames;
        }
    }
} // namespace spectra
