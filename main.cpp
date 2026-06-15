#include <imgui.h>

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

    struct SceneCommandBarState {
        bool open_pbrt_popup{};
        bool browser_initialized{};
        std::filesystem::path browser_directory{};
        std::array<char, 4096> path_buffer{};
        std::string browser_error{};
    };

    struct SceneBrowserEntry {
        std::filesystem::path path{};
        std::string label{};
        bool directory{};
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

    void set_path_buffer(SceneCommandBarState& state, const std::filesystem::path& path) {
        const std::string value = path.string();
        std::fill(state.path_buffer.begin(), state.path_buffer.end(), '\0');
        const std::size_t count = std::min(value.size(), state.path_buffer.size() - 1u);
        std::copy_n(value.data(), count, state.path_buffer.data());
    }

    [[nodiscard]] std::filesystem::path selected_picker_path(const SceneCommandBarState& state) {
        const std::string value{state.path_buffer.data()};
        if (value.empty()) throw std::runtime_error("Choose a PBRT scene file before opening");
        std::filesystem::path path{value};
        if (!path.is_absolute()) path = state.browser_directory / path;
        return std::filesystem::absolute(path).lexically_normal();
    }

    void initialize_scene_browser(SceneCommandBarState& state) {
        if (state.browser_initialized) return;
        state.browser_directory = std::filesystem::current_path();
        state.browser_initialized = true;
        state.browser_error.clear();
    }

    [[nodiscard]] bool activate_pbrt_file(spectra::rasterizer::SceneController& controller, SceneCommandBarState& state) {
        try {
            const std::filesystem::path scene_path = selected_picker_path(state);
            const std::string id = scene_path.string();
            const std::string title = scene_file_title(scene_path);
            const bool activated = controller.activate_static_scene(id, title, [scene_path] { return std::make_shared<spectra::scene::Scene>(spectra::scene::Scene::parse_pbrt_file(scene_path)); });
            if (activated) {
                state.browser_directory = scene_path.parent_path();
                state.browser_error.clear();
            }
            return activated;
        } catch (const std::exception& error) {
            state.browser_error = error.what();
            return false;
        }
    }

    [[nodiscard]] std::vector<SceneBrowserEntry> collect_scene_browser_entries(const std::filesystem::path& directory) {
        std::vector<SceneBrowserEntry> entries{};
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory)) {
            const std::filesystem::path path = entry.path();
            if (entry.is_directory()) {
                entries.push_back(SceneBrowserEntry{.path = path, .label = path.filename().string(), .directory = true});
                continue;
            }
            if (!entry.is_regular_file() || !is_pbrt_scene_file(path)) continue;
            entries.push_back(SceneBrowserEntry{.path = path, .label = path.filename().string(), .directory = false});
        }
        std::sort(entries.begin(), entries.end(), [](const SceneBrowserEntry& left, const SceneBrowserEntry& right) {
            if (left.directory != right.directory) return left.directory > right.directory;
            return lowercase_ascii(left.label) < lowercase_ascii(right.label);
        });
        return entries;
    }

    void draw_scene_browser_entries(spectra::rasterizer::SceneController& controller, SceneCommandBarState& state) {
        try {
            std::vector<SceneBrowserEntry> entries = collect_scene_browser_entries(state.browser_directory);
            if (entries.empty()) ImGui::TextDisabled("%s", "No PBRT scenes in this folder");
            for (const SceneBrowserEntry& entry : entries) {
                ImGui::PushID(entry.path.string().c_str());
                const std::string label = entry.directory ? std::format("[{}]", entry.label) : entry.label;
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (entry.directory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        state.browser_directory = std::filesystem::absolute(entry.path).lexically_normal();
                        state.browser_error.clear();
                    } else if (!entry.directory) {
                        set_path_buffer(state, std::filesystem::absolute(entry.path).lexically_normal());
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && activate_pbrt_file(controller, state)) ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::PopID();
            }
        } catch (const std::exception& error) {
            state.browser_error = error.what();
            ImGui::TextDisabled("%s", "Unable to read this folder");
        }
    }

    void draw_open_pbrt_popup(spectra::rasterizer::SceneController& controller, SceneCommandBarState& state) {
        if (state.open_pbrt_popup) {
            initialize_scene_browser(state);
            ImGui::OpenPopup("Open PBRT Scene");
            state.open_pbrt_popup = false;
        }

        ImGui::SetNextWindowSize(ImVec2{620.0f, 460.0f}, ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Open PBRT Scene", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;

        ImGui::TextDisabled("%s", "Folder");
        ImGui::TextWrapped("%s", state.browser_directory.string().c_str());
        if (ImGui::Button("Up")) {
            const std::filesystem::path parent = state.browser_directory.parent_path();
            if (!parent.empty()) state.browser_directory = parent;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##OpenPbrtScenePath", state.path_buffer.data(), state.path_buffer.size());

        ImGui::BeginChild("SpectraOpenPbrtSceneBrowser", ImVec2{0.0f, 270.0f}, false);
        draw_scene_browser_entries(controller, state);
        ImGui::EndChild();

        if (!state.browser_error.empty()) ImGui::TextColored(ImVec4{1.0f, 0.42f, 0.36f, 1.0f}, "%s", state.browser_error.c_str());
        if (controller.has_activation_error()) ImGui::TextColored(ImVec4{1.0f, 0.42f, 0.36f, 1.0f}, "%s", controller.activation_error().c_str());

        const bool has_path = state.path_buffer.front() != '\0';
        ImGui::BeginDisabled(!has_path);
        if (ImGui::Button("Open") && activate_pbrt_file(controller, state)) ImGui::CloseCurrentPopup();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    [[nodiscard]] bool has_scene_menu_entries(spectra::rasterizer::SceneController& controller, const spectra::rasterizer::SceneEntryKind kind) {
        for (std::size_t index = 0; index < controller.size(); ++index)
            if (controller.entry(index).kind == kind) return true;
        return false;
    }

    void draw_scene_menu_entries(spectra::rasterizer::SceneController& controller, const spectra::rasterizer::SceneEntryKind kind, const std::optional<std::size_t> selected_index) {
        for (std::size_t index = 0; index < controller.size(); ++index) {
            const spectra::rasterizer::SceneEntry& entry = controller.entry(index);
            if (entry.kind != kind) continue;
            ImGui::PushID(entry.id.c_str());
            if (ImGui::MenuItem(entry.title.c_str(), nullptr, selected_index.has_value() && index == *selected_index)) controller.request_activate(index);
            ImGui::PopID();
        }
    }

    void draw_scene_command_bar_widget(spectra::rasterizer::SceneController& controller, SceneCommandBarState& state) {
        const std::optional<std::size_t> selected_index = controller.has_selected_entry() ? std::optional<std::size_t>{controller.selected_index()} : std::nullopt;
        const spectra::rasterizer::SceneEntry* selected_entry = selected_index.has_value() ? &controller.entry(*selected_index) : nullptr;
        const std::string preview = std::format("Scene: {}  v", selected_entry != nullptr ? selected_entry->title : "Untitled");
        const float preview_width = ImGui::CalcTextSize(preview.c_str()).x;
        const float chip_width = std::clamp(preview_width + ImGui::GetFrameHeight() + 26.0f, 184.0f, 320.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{12.0f, ImGui::GetStyle().FramePadding.y});
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{28.0f / 255.0f, 33.0f / 255.0f, 39.0f / 255.0f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{39.0f / 255.0f, 49.0f / 255.0f, 57.0f / 255.0f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{49.0f / 255.0f, 78.0f / 255.0f, 95.0f / 255.0f, 1.0f});
        if (ImGui::Button(preview.c_str(), ImVec2{chip_width, 0.0f})) ImGui::OpenPopup("SceneCommandMenu");
        const bool chip_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        if (ImGui::BeginPopup("SceneCommandMenu")) {
            if (controller.has_activation_error()) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 360.0f);
                ImGui::TextColored(ImVec4{1.0f, 0.42f, 0.36f, 1.0f}, "%s", controller.activation_error().c_str());
                ImGui::PopTextWrapPos();
                ImGui::Separator();
            }
            if (ImGui::MenuItem("Open PBRT...")) {
                state.open_pbrt_popup = true;
                ImGui::CloseCurrentPopup();
            }
            if (has_scene_menu_entries(controller, spectra::rasterizer::SceneEntryKind::Static)) {
                ImGui::Separator();
                ImGui::TextDisabled("%s", "Loaded Scenes");
                draw_scene_menu_entries(controller, spectra::rasterizer::SceneEntryKind::Static, selected_index);
            }
            if (has_scene_menu_entries(controller, spectra::rasterizer::SceneEntryKind::Dynamic)) {
                ImGui::Separator();
                ImGui::TextDisabled("%s", "Dynamic Sources");
                draw_scene_menu_entries(controller, spectra::rasterizer::SceneEntryKind::Dynamic, selected_index);
            }
            ImGui::EndPopup();
        }

        draw_open_pbrt_popup(controller, state);

        if (chip_hovered) {
            if (selected_entry == nullptr) {
                ImGui::SetTooltip("%s", "Empty Project\nNo scene loaded");
            } else {
                ImGui::SetTooltip(
                    "%s\n%s%s",
                    selected_entry->id.c_str(),
                    selected_entry->kind == spectra::rasterizer::SceneEntryKind::Static ? "Static" : "Dynamic",
                    controller.pending_switch() ? "\nSwitching on next frame" : "");
            }
        }
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
        std::shared_ptr<SceneCommandBarState> scene_command_state = std::make_shared<SceneCommandBarState>();
        application.register_renderer(RasterizerRendererAdapter{scene_controller, camera_workspace});
        application.register_renderer(PathtracerRendererAdapter{scene_controller, std::move(camera_workspace)});
        application.register_command_bar_widget(spectra::CommandBarWidget{
            .id    = "scene.selector",
            .title = "Scene",
            .draw  = [scene_controller = std::move(scene_controller), scene_command_state = std::move(scene_command_state)] { draw_scene_command_bar_widget(*scene_controller, *scene_command_state); },
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
