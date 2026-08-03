module;

#include <Windows.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <glaze/glaze.hpp>

#undef interface

module spectra.editor;

import std;

namespace spectra {
    struct SceneLibraryConfiguration {
        std::vector<std::string> roots{};
    };

    Editor::Editor(VulkanRuntime& runtime, SceneDocument& document, DynamicWorld& dynamics, GpuScene& gpu_scene, Renderers& renderers, const std::filesystem::path& shader_directory, std::filesystem::path scene_library_path, std::vector<std::filesystem::path> session_scene_roots) noexcept : context{runtime, document, dynamics, gpu_scene, renderers}, interaction(runtime, document, dynamics, gpu_scene, renderers), viewport(runtime, gpu_scene, dynamics, renderers, interaction, shader_directory), output(runtime, gpu_scene, renderers, viewport, shader_directory), ui(runtime, document, dynamics, renderers, interaction, viewport, output, shader_directory), library{.configuration_path = std::move(scene_library_path), .session_roots = std::move(session_scene_roots)} {}

    Editor::~Editor() {
        this->output.wait_for_frozen_scene_export();
        if (this->lifetime.com_initialized) CoUninitialize();
    }

    void Editor::initialize(std::optional<std::filesystem::path> scene_path) {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (result != S_OK && result != S_FALSE) throw std::runtime_error(std::format("COM initialization failed with HRESULT 0x{:08X}", static_cast<std::uint32_t>(result)));
        this->lifetime.com_initialized = true;
        this->ui.initialize();
        this->output.initialize();
        if (scene_path)
            this->open_scene(*scene_path);
        else {
            this->library.visible = true;
            this->refresh_scene_library();
        }
    }

    void Editor::destroy_rendering() noexcept {
        this->viewport.destroy_scene();
        this->interaction.destroy_scene_rendering();
    }

    void Editor::rebuild_rendering(scene::Scene& source_scene) {
        this->interaction.rebuild_scene_rendering(source_scene);
        this->viewport.initialize(this->context.document.content.evaluated.view());
    }

    void Editor::open_scene(const std::filesystem::path& path) {
        scene::Scene next_scene = scene::load_scene(path);
        this->context.runtime.graphics.device.waitIdle();
        this->destroy_rendering();
        this->context.dynamics.destroy();
        this->context.document.content.source    = std::move(next_scene);
        this->context.document.content.evaluated = this->context.document.content.source;
        this->context.document.content.path      = path;
        if (this->context.document.content.source.dynamic_setup) this->context.dynamics.initialize(path, this->context.document.content.source);
        this->rebuild_rendering(this->context.document.content.source);
        this->interaction.initialize_from_scene();
        this->context.document.content.loaded = true;
        this->rendering.synchronized_scene_revision = 0;
    }

    void Editor::close_scene() noexcept {
        this->output.wait_for_frozen_scene_export();
        this->destroy_rendering();
        this->context.dynamics.destroy();
        this->context.document.close();
    }

    void Editor::save() {
        this->context.document.save();
        this->interaction.editing.saved_edit_serial = this->interaction.editing.current_edit_serial;
    }

    void Editor::save_as(const std::filesystem::path& path) {
        this->context.document.save_as(path);
        this->interaction.editing.saved_edit_serial = this->interaction.editing.current_edit_serial;
    }

    void Editor::begin_frame(const std::uint32_t frame_slot_index) {
        if (const std::optional<EditorOutputResult> result = this->output.begin_frame(frame_slot_index)) {
            if (result->error_message.empty())
                this->ui.notify(std::format("Written  {}", result->output_path.filename().string()));
            else
                this->ui.notify(result->error_message, true);
        }
        this->viewport.consume_pick(frame_slot_index, this->interaction);
    }

    std::optional<std::filesystem::path> Editor::choose_scene_file() {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog{};
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) throw std::runtime_error("Failed to create the Windows open dialog");
        constexpr std::array filters{COMDLG_FILTERSPEC{L"Spectra Scene", L"*.spectra"}};
        dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
        dialog->SetTitle(L"Open Spectra Scene");
        const HRESULT shown = dialog->Show(this->context.runtime.platform.native_window);
        if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return std::nullopt;
        if (FAILED(shown)) throw std::runtime_error("The Windows open dialog failed");
        Microsoft::WRL::ComPtr<IShellItem> item{};
        if (FAILED(dialog->GetResult(&item))) throw std::runtime_error("The Windows open dialog returned no item");
        PWSTR path{};
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) throw std::runtime_error("The Windows open dialog returned no filesystem path");
        const std::filesystem::path result{path};
        CoTaskMemFree(path);
        return result;
    }

    std::optional<std::filesystem::path> Editor::choose_scene_save_path(const std::filesystem::path& current_path, const bool frozen_scene) {
        Microsoft::WRL::ComPtr<IFileSaveDialog> dialog{};
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) throw std::runtime_error("Failed to create the Windows save dialog");
        constexpr std::array filters{COMDLG_FILTERSPEC{L"Spectra Scene", L"*.spectra"}};
        dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
        dialog->SetDefaultExtension(L"spectra");
        dialog->SetTitle(frozen_scene ? L"Export Frozen Spectra Scene" : L"Save Spectra Scene");
        const std::filesystem::path filename = frozen_scene ? current_path.parent_path() / std::format("{}-snapshot.spectra", current_path.stem().string()) : current_path.filename();
        dialog->SetFileName(filename.filename().c_str());
        const HRESULT shown = dialog->Show(this->context.runtime.platform.native_window);
        if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return std::nullopt;
        if (FAILED(shown)) throw std::runtime_error("The Windows save dialog failed");
        Microsoft::WRL::ComPtr<IShellItem> item{};
        if (FAILED(dialog->GetResult(&item))) throw std::runtime_error("The Windows save dialog returned no item");
        PWSTR path{};
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) throw std::runtime_error("The Windows save dialog returned no filesystem path");
        const std::filesystem::path result{path};
        CoTaskMemFree(path);
        return result;
    }

    bool Editor::confirm_scene_replacement() {
        if (!this->context.document.content.loaded || !this->interaction.scene_modified()) return true;
        this->timing.simulation_sample_valid = false;
        const int result = MessageBoxW(this->context.runtime.platform.native_window, L"The current Spectra scene has unsaved changes.\n\nSave before continuing?", L"Spectra", MB_ICONWARNING | MB_YESNOCANCEL);
        if (result == IDCANCEL) return false;
        if (result == IDYES) this->save();
        return true;
    }

    void Editor::replace_scene(const std::filesystem::path& path) {
        this->timing.simulation_sample_valid = false;
        if (path.extension() != ".spectra") throw std::runtime_error("Spectra accepts only .spectra scenes");
        if (!this->confirm_scene_replacement()) return;
        this->open_scene(path);
        this->library.visible = false;
    }

    void Editor::reload_scene() {
        this->timing.simulation_sample_valid = false;
        if (!this->confirm_scene_replacement()) return;
        this->open_scene(this->context.document.content.path);
        this->ui.notify("Scene reloaded");
    }

    void Editor::handle_dropped_scene_paths() {
        const std::vector<std::filesystem::path> paths = this->context.runtime.platform.take_dropped_paths();
        if (paths.empty()) return;
        try {
            if (paths.size() != 1u) throw std::runtime_error("Drop exactly one .spectra scene");
            this->replace_scene(paths.front());
        } catch (const std::exception& error) {
            this->ui.notify(error.what(), true);
        }
    }

    void Editor::handle_actions(const EditorActions& actions) {
        if (actions.exit_application) this->context.runtime.platform.request_close();
        try {
            if (actions.open_scene_library) {
                this->timing.simulation_sample_valid = false;
                this->refresh_scene_library();
                this->library.visible = true;
            }
            if (actions.close_scene_library) this->library.visible = false;
            if (actions.refresh_scene_library) {
                this->timing.simulation_sample_valid = false;
                this->refresh_scene_library();
            }
            if (actions.open_scene_file) {
                this->timing.simulation_sample_valid = false;
                if (const std::optional<std::filesystem::path> path = this->choose_scene_file()) this->replace_scene(*path);
            }
            if (actions.selected_scene_path) this->replace_scene(*actions.selected_scene_path);
            if (actions.reload_scene) this->reload_scene();
            if (actions.save_scene) {
                this->timing.simulation_sample_valid = false;
                this->save();
                this->ui.notify("Scene saved");
            }
            if (actions.save_scene_as) {
                this->timing.simulation_sample_valid = false;
                if (const std::optional<std::filesystem::path> path = this->choose_scene_save_path(this->context.document.content.path)) {
                    this->save_as(*path);
                    this->ui.notify("Scene saved");
                }
            }
            if (actions.export_frozen_scene) {
                this->timing.simulation_sample_valid = false;
                if (const std::optional<std::filesystem::path> path = this->choose_scene_save_path(this->context.document.content.path, true)) {
                    this->output.request_frozen_scene_export(*path);
                    this->ui.notify("Capturing Frozen Scene");
                }
            }
            if (actions.toggle_renderer) {
                this->context.runtime.graphics.device.waitIdle();
                const std::string_view renderer = *actions.toggle_renderer;
                if (this->context.renderers.enabled(renderer)) {
                    const std::span<const RendererDescriptor> enabled_renderers = this->context.renderers.enabled_renderers();
                    const RendererDescriptor replacement = *std::ranges::find_if(enabled_renderers, [renderer](const RendererDescriptor descriptor) { return descriptor.id != renderer; });
                    this->context.renderers.disable(renderer, replacement.id);
                } else
                    this->context.renderers.enable(renderer, this->context.document.content.evaluated.view());
            }
            if (actions.capture_format) this->output.request_capture(*actions.capture_format, this->context.document.content.source.film());
        } catch (const std::exception& error) {
            this->ui.notify(error.what(), true);
        }
    }

    void Editor::refresh_scene_library() {
        std::ifstream stream{this->library.configuration_path, std::ios::binary};
        if (!stream) throw std::runtime_error(std::format("Failed to open Spectra Scene Library configuration: {}", this->library.configuration_path.string()));
        const std::string json{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
        SceneLibraryConfiguration configuration{};
        constexpr glz::opts options{.error_on_unknown_keys = true, .error_on_missing_keys = true};
        const glz::error_ctx error = glz::read<options>(configuration, json);
        if (error) throw std::runtime_error(std::format("Failed to parse Spectra Scene Library configuration {}:\n{}", this->library.configuration_path.string(), glz::format_error(error, json)));

        std::vector<std::filesystem::path> roots{};
        roots.reserve(configuration.roots.size() + this->library.session_roots.size());
        for (const std::string& configured : configuration.roots) {
            const std::filesystem::path relative{configured};
            if (relative.is_absolute()) throw std::runtime_error("Spectra Scene Library roots must be relative to library.json");
            roots.emplace_back(this->library.configuration_path.parent_path() / relative);
        }
        for (const std::filesystem::path& root : this->library.session_roots) roots.emplace_back(std::filesystem::absolute(root));

        this->library.scenes.clear();
        this->library.problems.clear();
        std::vector<std::filesystem::path> discovered{};
        for (const std::filesystem::path& declared_root : roots) {
            std::error_code root_error{};
            const std::filesystem::path root = std::filesystem::weakly_canonical(declared_root, root_error);
            if (root_error || !std::filesystem::is_directory(root)) {
                this->library.problems.emplace_back(declared_root, "Scene root is unavailable");
                continue;
            }
            try {
                for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator{root}) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".spectra") continue;
                    const std::filesystem::path path = std::filesystem::weakly_canonical(entry.path());
                    if (std::ranges::contains(discovered, path)) continue;
                    discovered.emplace_back(path);
                    try {
                        this->library.scenes.emplace_back(scene::inspect_scene(path), root);
                    } catch (const std::exception& inspection_error) {
                        this->library.problems.emplace_back(path, inspection_error.what());
                    }
                }
            } catch (const std::exception& scan_error) {
                this->library.problems.emplace_back(root, scan_error.what());
            }
        }
        std::ranges::sort(this->library.scenes, [](const SceneLibraryEntry& left, const SceneLibraryEntry& right) {
            const std::string left_name  = left.summary.name.empty() ? left.summary.scene_path.string() : left.summary.name;
            const std::string right_name = right.summary.name.empty() ? right.summary.scene_path.string() : right.summary.name;
            return std::tie(left_name, left.summary.scene_path) < std::tie(right_name, right.summary.scene_path);
        });
        std::ranges::sort(this->library.problems, {}, &SceneLibraryProblem::scene_path);
    }
} // namespace spectra
