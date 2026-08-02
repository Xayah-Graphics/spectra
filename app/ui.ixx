module;

#include <imgui.h>

#include <ImGuizmo.h>

export module spectra.app.ui;

import spectra;
import spectra.render;
import spectra.workspace;
import spectra.scene;
import spectra.scene.format;
import std;
import vulkan;

namespace spectra::app {
    export struct ImGuiRenderer {
        ImGuiRenderer(Spectra& runtime, const std::filesystem::path& shader_directory, std::uint32_t frames_in_flight);
        ~ImGuiRenderer();

        ImGuiRenderer(const ImGuiRenderer&)            = delete;
        ImGuiRenderer(ImGuiRenderer&&)                 = delete;
        ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;
        ImGuiRenderer& operator=(ImGuiRenderer&&)      = delete;

        void record(ImDrawData& draw_data, const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_index, vk::Image target_image, vk::ImageView target_view, vk::Extent2D extent, vk::ImageLayout target_layout, vk::ImageLayout final_layout);

    private:
        struct FrameResources {
            GpuBuffer vertex_buffer{};
            GpuBuffer index_buffer{};
            DescriptorHandle vertex_descriptor{};
            DescriptorHandle index_descriptor{};
            std::size_t vertex_capacity{};
            std::size_t index_capacity{};
        };

        void update_texture(ImTextureData& texture);
        void destroy_texture(ImTextureData& texture);
        void setup_render_state(const vk::raii::CommandBuffer& command_buffer, const ImDrawData& draw_data, const FrameResources& frame, vk::Extent2D extent);

        Spectra* runtime{};
        vk::raii::ShaderEXTs shaders{nullptr};
        std::vector<FrameResources> frames{};
        DescriptorHandle sampler_descriptor{};
    };

    export struct WorkspaceUiActions {
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
        std::optional<std::filesystem::path> selected_scene{};
        std::optional<render::ImageFileFormat> capture{};
        std::array<std::array<float, 4>, 2> drag_regions{};
    };

    export struct SceneLibraryEntry {
        scene::SceneSummary scene{};
        std::filesystem::path root{};
    };

    export struct SceneLibraryProblem {
        std::filesystem::path path{};
        std::string message{};
    };

    export struct WorkspaceUi {
        [[nodiscard]] WorkspaceUiActions draw(workspace::Workspace& workspace, std::uint64_t viewport_texture);
        [[nodiscard]] WorkspaceUiActions draw_scene_library(std::span<const SceneLibraryEntry> scenes, std::span<const SceneLibraryProblem> problems, bool active_scene);

        std::string status{};
        bool status_error{};

    private:
        template <class Operation>
        bool apply_setup_edit(Operation&& operation, std::string_view success, std::string& status, bool& status_error, bool clear_drafts = true);
        [[nodiscard]] bool path_progress_visible(const workspace::Workspace& workspace) const noexcept;
        [[nodiscard]] bool pointer_over_interface(ImVec2 position, ImVec2 size, bool show_axes) const noexcept;
        void synchronize_transform(const workspace::Workspace& workspace);
        void apply_transform(workspace::Workspace& workspace);
        void transform_field(workspace::Workspace& workspace, const char* label, std::array<float, 3>& value, float speed);
        void handle_shortcuts(workspace::Workspace& workspace, WorkspaceUiActions& actions, float aspect);
        void draw_orientation(workspace::Workspace& workspace, ImVec2 position, ImVec2 size, bool show_axes);
        void draw_gizmo(workspace::Workspace& workspace, ImVec2 minimum, ImVec2 size, bool blocked);
        void handle_viewport_input(workspace::Workspace& workspace, ImVec2 minimum, ImVec2 size, bool blocked);
        void draw_viewport(workspace::Workspace& workspace, std::uint64_t texture, ImVec2 position, ImVec2 size, bool show_axes);
        [[nodiscard]] float draw_render_status(workspace::Workspace& workspace, float end);
        void draw_top_strip(workspace::Workspace& workspace, ImVec2 position, ImVec2 size, WorkspaceUiActions& actions);
        void draw_transform_tools(ImVec2 position, ImVec2 size);
        void draw_transform_hud(workspace::Workspace& workspace, ImVec2 position, ImVec2 size);
        void draw_simulation_hud(workspace::Workspace& workspace, ImVec2 position, ImVec2 size, std::string& status, bool& status_error);
        void draw_status_toast(std::string& status, bool& status_error, ImVec2 position, ImVec2 size);

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
        std::optional<std::filesystem::path> selected_library_scene{};
    };
} // namespace spectra::app
