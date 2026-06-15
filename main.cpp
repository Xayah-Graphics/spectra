#include <vulkan/vulkan_raii.hpp>

import std;
import spectra;
import spectra.pathtracer.renderer;
import spectra.rasterizer.renderer;
import spectra.rasterizer.visualization;
import spectra.scene;

namespace {
    struct CliOptions {
        std::optional<std::string> scene_id{};
    };

    static_assert(spectra::pathtracer::Host<spectra::Spectra>);
    static_assert(spectra::rasterizer::Host<spectra::Spectra>);

    struct SceneWorkspaceStatusState {
        std::string status_text{};
        bool status_error{};
        std::chrono::steady_clock::time_point status_expires{};
    };

    [[nodiscard]] std::string lowercase_ascii(std::string value) {
        for (char& character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        return value;
    }

    [[nodiscard]] bool path_extension_is(const std::filesystem::path& path, const std::string_view extension) {
        return lowercase_ascii(path.extension().string()) == lowercase_ascii(std::string{extension});
    }

    [[nodiscard]] bool is_pbrt_scene_file(const std::filesystem::path& path) {
        if (path_extension_is(path, ".pbrt")) return true;
        if (!path_extension_is(path, ".gz")) return false;
        return path_extension_is(path.stem(), ".pbrt");
    }

    [[nodiscard]] std::string scene_file_title(const std::filesystem::path& path) {
        std::filesystem::path filename = path.filename();
        if (path_extension_is(filename, ".gz")) filename = filename.stem();
        if (path_extension_is(filename, ".pbrt")) filename = filename.stem();
        if (filename.empty()) throw std::runtime_error("PBRT scene path has an empty filename");
        return filename.string();
    }

    void set_scene_status(SceneWorkspaceStatusState& state, std::string text, const bool error) {
        state.status_text = std::move(text);
        state.status_error = error;
        state.status_expires = std::chrono::steady_clock::now() + std::chrono::seconds{4};
    }

    [[nodiscard]] bool scene_status_visible(SceneWorkspaceStatusState& state) {
        if (state.status_text.empty()) return false;
        if (std::chrono::steady_clock::now() < state.status_expires) return true;
        state.status_text.clear();
        state.status_error = false;
        return false;
    }

    [[nodiscard]] bool activate_pbrt_scene_path(spectra::rasterizer::SceneController& controller, SceneWorkspaceStatusState& state, const std::filesystem::path& scene_path) {
        try {
            if (scene_path.empty()) throw std::runtime_error("Drop a PBRT scene file into the window to load it");
            const std::filesystem::path absolute_path = std::filesystem::absolute(scene_path).lexically_normal();
            if (std::filesystem::is_directory(absolute_path)) throw std::runtime_error("Drop a PBRT scene file, not a folder");
            if (!std::filesystem::is_regular_file(absolute_path)) throw std::runtime_error(std::format("{}: PBRT scene file does not exist", absolute_path.string()));
            if (!is_pbrt_scene_file(absolute_path)) throw std::runtime_error(std::format("{}: scene file must use .pbrt or .pbrt.gz", absolute_path.string()));
            const std::string id = absolute_path.string();
            const std::string title = scene_file_title(absolute_path);
            const bool activated = controller.activate_static_scene(id, title, [absolute_path] { return std::make_shared<spectra::scene::Scene>(spectra::scene::Scene::parse_pbrt_file(absolute_path)); });
            if (!activated) throw std::runtime_error(controller.activation_error().empty() ? "Failed to load static scene" : controller.activation_error());
            set_scene_status(state, std::format("Loaded {}", title), false);
            return true;
        } catch (const std::exception& error) {
            set_scene_status(state, error.what(), true);
            return false;
        }
    }

    [[nodiscard]] bool handle_scene_file_drop(spectra::rasterizer::SceneController& controller, SceneWorkspaceStatusState& state, const std::span<const std::filesystem::path> paths) {
        if (paths.empty()) {
            set_scene_status(state, "Drop a PBRT scene file to load it", true);
            return true;
        }
        if (paths.size() != 1u) {
            set_scene_status(state, "Drop exactly one PBRT scene file", true);
            return true;
        }
        static_cast<void>(activate_pbrt_scene_path(controller, state, paths.front()));
        return true;
    }

    [[nodiscard]] std::string scene_workspace_tooltip(const spectra::rasterizer::SceneEntry* selected_entry, const bool pending_switch) {
        if (selected_entry == nullptr) return "Empty Project\nDrop a PBRT scene file into the window to load it";
        return std::format(
            "{}\n{}{}\nDrop another PBRT scene file into the window to replace it",
            selected_entry->id,
            selected_entry->kind == spectra::rasterizer::SceneEntryKind::Static ? "Static" : "Dynamic",
            pending_switch ? "\nSwitching on next frame" : "");
    }

    [[nodiscard]] spectra::WorkspaceTitle make_scene_workspace_title(spectra::rasterizer::SceneController& controller, SceneWorkspaceStatusState& state) {
        const std::optional<std::size_t> selected_index = controller.has_selected_entry() ? std::optional<std::size_t>{controller.selected_index()} : std::nullopt;
        const spectra::rasterizer::SceneEntry* selected_entry = selected_index.has_value() ? &controller.entry(*selected_index) : nullptr;
        spectra::WorkspaceTitle title{
            .detail  = selected_entry != nullptr ? selected_entry->title : "Untitled",
            .tooltip = scene_workspace_tooltip(selected_entry, controller.pending_switch()),
        };
        if (scene_status_visible(state)) {
            title.status_text = state.status_text;
            title.status_error = state.status_error;
        }
        return title;
    }

    class PathtracerRendererAdapter final {
    public:
        PathtracerRendererAdapter(std::shared_ptr<spectra::rasterizer::SceneController> scene_controller, std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace) : scene_controller(std::move(scene_controller)), camera_workspace(std::move(camera_workspace)) {
            if (this->scene_controller == nullptr) throw std::runtime_error("Pathtracer adapter requires a scene controller");
            if (this->camera_workspace == nullptr) throw std::runtime_error("Pathtracer adapter requires a scene camera workspace");
            this->active_workspace = this->scene_controller->active_workspace();
            this->renderer = std::make_unique<spectra::pathtracer::Renderer>(this->active_workspace, this->camera_workspace);
        }

        PathtracerRendererAdapter(const PathtracerRendererAdapter& other) = delete;
        PathtracerRendererAdapter(PathtracerRendererAdapter&& other) noexcept = default;
        PathtracerRendererAdapter& operator=(const PathtracerRendererAdapter& other) = delete;
        PathtracerRendererAdapter& operator=(PathtracerRendererAdapter&& other) noexcept = default;
        ~PathtracerRendererAdapter() noexcept = default;

        [[nodiscard]] static std::string_view name() {
            return spectra::pathtracer::Renderer::name();
        }

        void attach(spectra::Spectra& host) {
            this->renderer->attach(spectra::pathtracer::HostView{host});
        }

        void detach() noexcept {
            this->renderer->detach();
        }

        void before_imgui_shutdown() noexcept {
            this->renderer->before_imgui_shutdown();
        }

        void after_imgui_created() {
            this->renderer->after_imgui_created();
        }

        [[nodiscard]] spectra::FrameResult begin_frame(spectra::Spectra& host, const spectra::FrameContext& frame) {
            static_cast<void>(this->scene_controller->apply_pending_scene());
            this->sync_scene_workspace();
            this->scene_controller->update_active_scene(frame.delta_seconds);
            const spectra::pathtracer::FrameContext frame_context{
                .frame_index = frame.frame_slot_index,
                .image_index = frame.image_index,
            };
            spectra::pathtracer::FrameResult result = this->renderer->begin_frame(spectra::pathtracer::HostView{host}, frame_context);
            return spectra::FrameResult{
                .completion_semaphore = std::move(result.completion_semaphore),
                .close_requested      = result.close_requested,
                .window_detail        = std::move(result.window_detail),
            };
        }

        void record_frame(const vk::raii::CommandBuffer& command_buffer) {
            this->renderer->record_frame(command_buffer);
        }

    private:
        void sync_scene_workspace() {
            std::shared_ptr<spectra::scene::Scene> current_workspace = this->scene_controller->active_workspace();
            if (this->active_workspace == current_workspace) return;
            this->renderer->set_scene_workspace(current_workspace, this->camera_workspace);
            this->active_workspace = std::move(current_workspace);
        }

        std::shared_ptr<spectra::rasterizer::SceneController> scene_controller{};
        std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace{};
        std::shared_ptr<spectra::scene::Scene> active_workspace{};
        std::unique_ptr<spectra::pathtracer::Renderer> renderer{};
    };

    class RasterizerRendererAdapter final {
    public:
        RasterizerRendererAdapter(std::shared_ptr<spectra::rasterizer::SceneController> scene_controller, std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace) : scene_controller(std::move(scene_controller)), camera_workspace(std::move(camera_workspace)) {
            if (this->scene_controller == nullptr) throw std::runtime_error("Rasterizer adapter requires a scene controller");
            if (this->camera_workspace == nullptr) throw std::runtime_error("Rasterizer adapter requires a scene camera workspace");
            this->active_workspace = this->scene_controller->active_workspace();
            this->renderer = std::make_unique<spectra::rasterizer::Renderer>(this->active_workspace, this->camera_workspace);
        }

        RasterizerRendererAdapter(const RasterizerRendererAdapter& other) = delete;
        RasterizerRendererAdapter(RasterizerRendererAdapter&& other) noexcept = default;
        RasterizerRendererAdapter& operator=(const RasterizerRendererAdapter& other) = delete;
        RasterizerRendererAdapter& operator=(RasterizerRendererAdapter&& other) noexcept = default;
        ~RasterizerRendererAdapter() noexcept = default;

        [[nodiscard]] static std::string_view name() {
            return spectra::rasterizer::Renderer::name();
        }

        void attach(spectra::Spectra& host) {
            this->renderer->attach(spectra::rasterizer::HostView{host});
        }

        void detach() noexcept {
            this->renderer->detach();
        }

        void before_imgui_shutdown() noexcept {
            this->renderer->before_imgui_shutdown();
        }

        void after_imgui_created() {
            this->renderer->after_imgui_created();
        }

        [[nodiscard]] spectra::FrameResult begin_frame(spectra::Spectra& host, const spectra::FrameContext& frame) {
            static_cast<void>(this->scene_controller->apply_pending_scene());
            this->sync_scene_workspace();
            this->scene_controller->update_active_scene(frame.delta_seconds);
            const spectra::rasterizer::FrameContext rasterizer_frame{
                .frame_index   = frame.frame_slot_index,
                .image_index   = frame.image_index,
                .frame_number  = frame.frame_number,
                .delta_seconds = frame.delta_seconds,
            };
            spectra::rasterizer::FrameResult result = this->renderer->begin_frame(spectra::rasterizer::HostView{host}, rasterizer_frame);
            return spectra::FrameResult{
                .completion_semaphore = std::move(result.completion_semaphore),
                .close_requested      = result.close_requested,
                .window_detail        = std::move(result.window_detail),
            };
        }

        void record_frame(const vk::raii::CommandBuffer& command_buffer) {
            this->renderer->record_frame(command_buffer);
        }

    private:
        void sync_scene_workspace() {
            std::shared_ptr<spectra::scene::Scene> current_workspace = this->scene_controller->active_workspace();
            if (this->active_workspace == current_workspace) return;
            this->renderer->set_scene_workspace(current_workspace, this->camera_workspace);
            this->active_workspace = std::move(current_workspace);
        }

        std::shared_ptr<spectra::rasterizer::SceneController> scene_controller{};
        std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace{};
        std::shared_ptr<spectra::scene::Scene> active_workspace{};
        std::unique_ptr<spectra::rasterizer::Renderer> renderer{};
    };

    static_assert(spectra::RendererFor<PathtracerRendererAdapter, spectra::Spectra>);
    static_assert(spectra::RendererFor<RasterizerRendererAdapter, spectra::Spectra>);

    [[nodiscard]] CliOptions parse_cli(const int argc, char** argv) {
        CliOptions options{};
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument{argv[index]};
            if (argument == "--scene") {
                ++index;
                if (index >= argc) throw std::runtime_error("usage: spectra_gui [--scene <scene-id-or-path>]");
                options.scene_id = argv[index];
                if (options.scene_id->empty()) throw std::runtime_error("spectra_gui --scene requires a non-empty scene id or path");
            } else if (argument == "--help" || argument == "-h") {
                throw std::runtime_error("usage: spectra_gui [--scene <scene-id-or-path>]");
            } else {
                throw std::runtime_error("usage: spectra_gui [--scene <scene-id-or-path>]");
            }
        }
        return options;
    }

    struct InitialSceneLoad {
        std::shared_ptr<spectra::scene::Scene> scene{};
        std::string id{};
    };

    [[nodiscard]] std::shared_ptr<spectra::scene::Scene> make_empty_project_scene() {
        spectra::scene::Scene::Document document{
            .revision = spectra::scene::Scene::Revision{1},
            .name = "untitled",
            .title = "Untitled",
            .source = "scene://untitled",
            .timeline_enabled = false,
            .camera = spectra::scene::Scene::Camera{
                .name = "Camera",
                .transform = spectra::scene::Transform{.position = spectra::scene::Vector3{0.0f, 1.0f, 5.0f}},
                .target = spectra::scene::Vector3{0.0f, 0.0f, 0.0f},
                .up = spectra::scene::Vector3{0.0f, 1.0f, 0.0f},
                .vertical_fov_degrees = 45.0f,
            },
        };
        return std::make_shared<spectra::scene::Scene>(std::move(document));
    }

    [[nodiscard]] InitialSceneLoad load_initial_scene(const std::string& scene_id) {
        const std::filesystem::path requested_path{scene_id};
        if (requested_path.is_absolute() && is_pbrt_scene_file(requested_path) && !std::filesystem::is_regular_file(requested_path)) throw std::runtime_error(std::format("{}: initial scene file does not exist", requested_path.string()));
        if (std::filesystem::is_regular_file(requested_path)) {
            const std::filesystem::path absolute_path = std::filesystem::absolute(requested_path).lexically_normal();
            if (!is_pbrt_scene_file(absolute_path)) throw std::runtime_error(std::format("{}: initial scene file must use .pbrt or .pbrt.gz", absolute_path.string()));
            return InitialSceneLoad{
                .scene = std::make_shared<spectra::scene::Scene>(spectra::scene::Scene::parse_pbrt_file(absolute_path)),
                .id    = absolute_path.string(),
            };
        }
        return InitialSceneLoad{
            .scene = std::make_shared<spectra::scene::Scene>(spectra::scene::Scene::parse_pbrt(scene_id)),
            .id    = scene_id,
        };
    }

    void load_cli_scene(spectra::rasterizer::SceneController& controller, const std::string& scene_id) {
        InitialSceneLoad initial_scene = load_initial_scene(scene_id);
        if (initial_scene.scene == nullptr) throw std::runtime_error("Initial scene loader returned null");
        const std::string title = initial_scene.scene->info().title;
        std::shared_ptr<spectra::scene::Scene> scene = std::move(initial_scene.scene);
        if (!controller.activate_static_scene(std::move(initial_scene.id), title, [scene = std::move(scene)] { return scene; })) throw std::runtime_error(controller.activation_error());
    }

    void register_renderers(spectra::Spectra& application, std::shared_ptr<spectra::rasterizer::SceneController> scene_controller, std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace) {
        if (scene_controller == nullptr) throw std::runtime_error("Renderer registration requires a scene controller");
        if (camera_workspace == nullptr) throw std::runtime_error("Renderer registration requires a scene camera workspace");
        std::shared_ptr<SceneWorkspaceStatusState> scene_status_state = std::make_shared<SceneWorkspaceStatusState>();
        application.register_renderer(RasterizerRendererAdapter{scene_controller, camera_workspace});
        application.register_renderer(PathtracerRendererAdapter{scene_controller, std::move(camera_workspace)});
        std::shared_ptr<spectra::rasterizer::SceneController> drop_scene_controller = scene_controller;
        std::shared_ptr<SceneWorkspaceStatusState> drop_scene_status_state = scene_status_state;
        application.set_workspace_title_provider([scene_controller = std::move(scene_controller), scene_status_state = std::move(scene_status_state)] { return make_scene_workspace_title(*scene_controller, *scene_status_state); });
        application.register_file_drop_handler(spectra::FileDropHandler{
            .id             = "scene.file-drop",
            .title          = "Scene File Drop",
            .owner_renderer = {},
            .handle         = [scene_controller = std::move(drop_scene_controller), scene_status_state = std::move(drop_scene_status_state)](const std::span<const std::filesystem::path> paths) { return handle_scene_file_drop(*scene_controller, *scene_status_state, paths); },
        });
    }
} // namespace

int main(const int argc, char** argv) {
    try {
        const CliOptions options = parse_cli(argc, argv);
        spectra::rasterizer::SceneRegistry scene_registry{};
        std::shared_ptr<spectra::rasterizer::SceneController> scene_controller = std::make_shared<spectra::rasterizer::SceneController>(std::move(scene_registry), make_empty_project_scene());
        if (options.scene_id.has_value()) load_cli_scene(*scene_controller, *options.scene_id);
        std::shared_ptr<spectra::scene::Scene::CameraWorkspace> camera_workspace = std::make_shared<spectra::scene::Scene::CameraWorkspace>();

        spectra::Spectra app{"Spectra"};
        register_renderers(app, std::move(scene_controller), std::move(camera_workspace));
        app.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
