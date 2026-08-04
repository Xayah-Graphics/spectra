module;

#include <imgui.h>
#include <ImGuizmo.h>

export module spectra.editor:ui;

import :interaction;
import :viewport;
import :output;
import spectra.runtime;
import spectra.scene;
import spectra.scene.dynamics;
import spectra.render;
import std;
import vulkan;

namespace spectra {
    export struct EditorActions {
        bool exit_application{};
        bool open_scene_library{};
        bool close_scene_library{};
        bool refresh_scene_library{};
        bool open_scene_file{};
        bool reload_scene{};
        bool save_scene{};
        bool save_scene_as{};
        bool export_frozen_scene{};
        bool show_axes{};
        std::optional<std::filesystem::path> selected_scene_path{};
        std::optional<CaptureFormat> capture_format{};
        std::optional<std::string> toggle_renderer{};
        std::array<std::array<float, 4>, 2> window_drag_regions{};
    };

    export struct SceneLibraryEntry {
        scene::SceneSummary summary{};
        std::filesystem::path root_path{};
    };

    export struct SceneLibraryProblem {
        std::filesystem::path scene_path{};
        std::string error_message{};
    };

    export struct EditorUi {
        struct ImGuiFrameResources {
            GpuBuffer vertex_buffer{};
            GpuBuffer index_buffer{};
            DescriptorHandle vertex_descriptor{};
            DescriptorHandle index_descriptor{};
            std::size_t vertex_capacity{};
            std::size_t index_capacity{};
        };

        EditorUi(WindowPlatform& platform, VulkanRuntime& runtime, SceneDocument& document, DynamicWorld& dynamics, Renderers& renderers, EditorInteraction& interaction, EditorViewport& viewport, EditorOutput& output, std::filesystem::path shader_directory) noexcept;
        ~EditorUi();

        EditorUi(const EditorUi&)            = delete;
        EditorUi(EditorUi&&)                 = delete;
        EditorUi& operator=(const EditorUi&) = delete;
        EditorUi& operator=(EditorUi&&)      = delete;

        void initialize();
        void update_imgui_texture(ImTextureData& texture);
        void destroy_imgui_texture(ImTextureData& texture);
        void setup_imgui_render_state(const vk::raii::CommandBuffer& command_buffer, const ImDrawData& draw_data, const ImGuiFrameResources& frame, vk::Extent2D extent);
        void record_imgui(ImDrawData& draw_data, const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, vk::Image target_image, vk::ImageView target_view, vk::Extent2D extent, vk::ImageLayout target_layout, vk::ImageLayout final_layout);
        [[nodiscard]] EditorActions draw_editor_ui();
        [[nodiscard]] EditorActions draw_scene_library(std::span<const SceneLibraryEntry> scenes, std::span<const SceneLibraryProblem> problems, bool active_scene);
        void notify(std::string message, bool error = false);

        struct {
            WindowPlatform& platform;
            VulkanRuntime& runtime;
            SceneDocument& document;
            DynamicWorld& dynamics;
            Renderers& renderers;
            EditorInteraction& interaction;
            EditorViewport& viewport;
            EditorOutput& output;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            vk::raii::ShaderEXTs shaders{nullptr};
            std::vector<ImGuiFrameResources> frames{};
            DescriptorHandle sampler_descriptor{};
        } renderer;

        struct {
            std::string status{};
            bool status_error{};
            bool expanded{};
            ImGuizmo::OPERATION gizmo_operation{ImGuizmo::TRANSLATE};
            bool gizmo_using{};
            bool transform_interaction{};
            std::optional<scene::InstanceId> transform_instance{};
            std::uint64_t transform_revision{};
            std::array<float, 3> translation{};
            std::array<float, 3> rotation{};
            std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
            std::string observed_status{};
            bool observed_status_error{};
            double status_since{};
            std::size_t selected_dynamic_system{};
            std::uint64_t observed_dynamic_revision{};
            std::map<std::string, scene::DynamicParameterValue> parameter_drafts{};
            std::set<std::string> pending_reset_systems{};
            std::optional<std::uint64_t> simulation_step_edit{};
            std::optional<scene::DynamicClock> clock_draft{};
            std::optional<std::uint64_t> seed_draft{};
            std::string pending_system_provider{};
            std::vector<scene::DynamicPortBinding> pending_system_bindings{};
            std::optional<std::size_t> pending_replacement_system{};
            bool open_system_configuration{};
            std::optional<std::filesystem::path> selected_scene{};
        } controls;

        struct {
            bool initialized{};
        } lifetime;

    private:
        template <class Operation>
        bool apply_setup_edit(Operation&& operation, std::string_view success_message, bool clear_drafts = true);
        void initialize_renderer();
        [[nodiscard]] bool render_progress_visible() const noexcept;
        [[nodiscard]] bool pointer_over_interface(ImVec2 position, ImVec2 size, bool show_axes) const noexcept;
        void synchronize_transform();
        void apply_transform();
        void transform_field(const char* label, std::array<float, 3>& value, float speed);
        void handle_shortcuts(EditorActions& actions, float aspect);
        void draw_orientation(ImVec2 position, ImVec2 size, bool show_axes);
        void draw_gizmo(ImVec2 minimum, ImVec2 size, bool blocked);
        void handle_viewport_input(ImVec2 minimum, ImVec2 size, bool blocked);
        void draw_viewport(ImVec2 position, ImVec2 size, bool show_axes);
        [[nodiscard]] float draw_render_status(float right_edge);
        void draw_top_strip(ImVec2 position, ImVec2 size, EditorActions& actions);
        void draw_transform_tools(ImVec2 position, ImVec2 size);
        void draw_transform_hud(ImVec2 position, ImVec2 size);
        void draw_simulation_hud(ImVec2 position, ImVec2 size);
        void draw_status_toast(ImVec2 position, ImVec2 size);
    };
} // namespace spectra
