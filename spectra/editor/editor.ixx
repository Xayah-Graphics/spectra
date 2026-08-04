module;

#include <Windows.h>

export module spectra.editor;

export import :interaction;
export import :viewport;
export import :output;
export import :ui;

import std;

namespace spectra {
    export struct Editor {
        Editor(WindowPlatform& platform, VulkanRuntime& runtime, SceneDocument& document, DynamicWorld& dynamics, GpuScene& gpu_scene, Renderers& renderers, const std::filesystem::path& shader_directory, std::filesystem::path scene_library_path, std::vector<std::filesystem::path> session_scene_roots) noexcept;
        ~Editor();

        Editor(const Editor&)            = delete;
        Editor(Editor&&)                 = delete;
        Editor& operator=(const Editor&) = delete;
        Editor& operator=(Editor&&)      = delete;

        void initialize(std::optional<std::filesystem::path> scene_path);
        void open_scene(const std::filesystem::path& path);
        void close_scene() noexcept;
        void save();
        void save_as(const std::filesystem::path& path);
        void handle_dropped_scene_paths();
        void handle_actions(const EditorActions& actions);
        void refresh_scene_library();
        void begin_frame(std::uint32_t frame_slot_index);

        struct {
            WindowPlatform& platform;
            VulkanRuntime& runtime;
            SceneDocument& document;
            DynamicWorld& dynamics;
            GpuScene& gpu_scene;
            Renderers& renderers;
        } context;

        EditorInteraction interaction;
        EditorViewport viewport;
        EditorOutput output;
        EditorUi ui;

        struct {
            std::filesystem::path configuration_path{};
            std::vector<std::filesystem::path> session_roots{};
            std::vector<SceneLibraryEntry> scenes{};
            std::vector<SceneLibraryProblem> problems{};
            bool visible{};
        } library;

        struct {
            std::uint64_t synchronized_scene_revision{};
        } rendering;

        struct {
            std::uint64_t presented_frames{};
            std::chrono::steady_clock::time_point previous_simulation_sample{};
            bool simulation_sample_valid{};
        } timing;

        struct {
            bool com_initialized{};
        } lifetime;

    private:
        [[nodiscard]] std::optional<std::filesystem::path> choose_scene_file();
        [[nodiscard]] std::optional<std::filesystem::path> choose_scene_save_path(const std::filesystem::path& current_path, bool frozen_scene = false);
        [[nodiscard]] bool confirm_scene_replacement();
        void replace_scene(const std::filesystem::path& path);
        void reload_scene();
        void destroy_rendering() noexcept;
        void rebuild_rendering(scene::Scene& source_scene);
    };
} // namespace spectra
