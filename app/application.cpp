module;

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <Windows.h>

module spectra.application;

import spectra.app.workspace_ui;
import spectra;
import spectra.render.output;
import spectra.scene;
import spectra.app.image_io;
import spectra.ui;
import spectra.app.presenter;
import spectra.workspace;
import std;
import vulkan;

namespace spectra {
    namespace {
        struct ComApartment {
            ComApartment() {
                const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                if (result != S_OK && result != S_FALSE) throw std::runtime_error(std::format("COM initialization failed with HRESULT 0x{:08X}", static_cast<std::uint32_t>(result)));
            }

            ~ComApartment() { CoUninitialize(); }

            ComApartment(const ComApartment&)            = delete;
            ComApartment(ComApartment&&)                 = delete;
            ComApartment& operator=(const ComApartment&) = delete;
            ComApartment& operator=(ComApartment&&)      = delete;
        };

        [[nodiscard]] std::filesystem::path known_folder(const KNOWNFOLDERID& identifier) {
            PWSTR value{};
            if (FAILED(SHGetKnownFolderPath(identifier, KF_FLAG_CREATE, nullptr, &value))) throw std::runtime_error("Failed to locate a required Windows known folder");
            const std::filesystem::path path{value};
            CoTaskMemFree(value);
            return path;
        }

        struct ImGuiPlatform {
            explicit ImGuiPlatform(GLFWwindow* window) {
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.IniFilename = nullptr;
                ImGui::StyleColorsDark();
                ImGuiStyle& style              = ImGui::GetStyle();
                style.WindowRounding           = 10.0f;
                style.ChildRounding            = 8.0f;
                style.FrameRounding            = 7.0f;
                style.PopupRounding            = 9.0f;
                style.ScrollbarRounding        = 9.0f;
                style.WindowBorderSize         = 0.0f;
                style.FrameBorderSize          = 0.0f;
                style.WindowPadding            = ImVec2{12.0f, 10.0f};
                style.FramePadding             = ImVec2{9.0f, 5.0f};
                style.ItemSpacing              = ImVec2{7.0f, 6.0f};
                ImVec4* colors                 = style.Colors;
                colors[ImGuiCol_WindowBg]      = ImVec4{0.018f, 0.023f, 0.030f, 0.94f};
                colors[ImGuiCol_PopupBg]       = ImVec4{0.025f, 0.031f, 0.040f, 0.98f};
                colors[ImGuiCol_FrameBg]       = ImVec4{0.080f, 0.092f, 0.110f, 0.90f};
                colors[ImGuiCol_FrameBgHovered] = ImVec4{0.115f, 0.135f, 0.160f, 0.95f};
                colors[ImGuiCol_FrameBgActive] = ImVec4{0.135f, 0.155f, 0.185f, 1.0f};
                colors[ImGuiCol_Header]        = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};
                colors[ImGuiCol_HeaderHovered] = ImVec4{0.72f, 0.80f, 0.90f, 0.11f};
                colors[ImGuiCol_HeaderActive]  = ImVec4{0.72f, 0.80f, 0.90f, 0.16f};
                colors[ImGuiCol_Separator]     = ImVec4{0.25f, 0.29f, 0.34f, 0.55f};
                if (!ImGui_ImplGlfw_InitForVulkan(window, true)) throw std::runtime_error("ImGui GLFW platform initialization failed");
            }

            ~ImGuiPlatform() {
                ImGui_ImplGlfw_Shutdown();
                ImGui::DestroyContext();
            }

            ImGuiPlatform(const ImGuiPlatform&)            = delete;
            ImGuiPlatform(ImGuiPlatform&&)                 = delete;
            ImGuiPlatform& operator=(const ImGuiPlatform&) = delete;
            ImGuiPlatform& operator=(ImGuiPlatform&&)      = delete;

        };

        [[nodiscard]] std::optional<std::filesystem::path> open_source_dialog(HWND owner) {
            Microsoft::WRL::ComPtr<IFileOpenDialog> dialog{};
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) throw std::runtime_error("Failed to create the Windows open dialog");
            constexpr std::array filters{
                COMDLG_FILTERSPEC{L"Spectra Scene or Plugin", L"*.spectra;*.dll"},
                COMDLG_FILTERSPEC{L"Spectra Scene", L"*.spectra"},
                COMDLG_FILTERSPEC{L"Spectra Plugin", L"*.dll"},
            };
            dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
            dialog->SetTitle(L"Open Spectra Scene or Plugin");
            const HRESULT shown = dialog->Show(owner);
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

        [[nodiscard]] std::optional<std::filesystem::path> save_scene_dialog(HWND owner, const std::filesystem::path& current) {
            Microsoft::WRL::ComPtr<IFileSaveDialog> dialog{};
            if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) throw std::runtime_error("Failed to create the Windows save dialog");
            constexpr std::array filters{
                COMDLG_FILTERSPEC{L"Spectra Scene", L"*.spectra"},
            };
            dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
            dialog->SetDefaultExtension(L"spectra");
            dialog->SetTitle(L"Save Spectra Scene");
            dialog->SetFileName(current.filename().c_str());
            const HRESULT shown = dialog->Show(owner);
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

        struct ViewportDisplay {
            explicit ViewportDisplay(GpuDevice& gpu) : gpu(&gpu), descriptor(gpu.allocate_resource_descriptor()), texture_id(static_cast<std::uint64_t>(this->descriptor.index) + 1u) {}

            ~ViewportDisplay() {
                this->gpu->release_resource_descriptor(this->descriptor);
            }

            ViewportDisplay(const ViewportDisplay&)            = delete;
            ViewportDisplay(ViewportDisplay&&)                 = delete;
            ViewportDisplay& operator=(const ViewportDisplay&) = delete;
            ViewportDisplay& operator=(ViewportDisplay&&)      = delete;

            void ensure(const vk::Extent2D extent) {
                if (*this->image.image && this->image.extent == extent) return;
                this->image = this->gpu->create_image_2d(extent, vk::Format::eB8G8R8A8Srgb, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);
                this->gpu->write_sampled_image(this->descriptor, this->image, vk::ImageLayout::eShaderReadOnlyOptimal);
                this->layout = vk::ImageLayout::eUndefined;
            }

            GpuDevice* gpu{};
            GpuImage image{};
            DescriptorHandle descriptor{};
            vk::ImageLayout layout{vk::ImageLayout::eUndefined};
            std::uint64_t texture_id{};
        };

        struct CaptureManager {
            struct Completed {
                std::filesystem::path path{};
                std::string error{};
            };

            CaptureManager(GpuDevice& gpu, const std::uint32_t frame_count) : gpu(&gpu), slots(frame_count) {}

            void request(const app::CaptureFormat format, const workspace::RenderMode mode, const scene::Film& film) {
                const std::filesystem::path directory = known_folder(FOLDERID_Pictures) / "Spectra";
                std::filesystem::create_directories(directory);
                const std::int64_t timestamp         = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                const std::string_view renderer_name = mode == workspace::RenderMode::Rasterizer ? "rasterizer" : "pathtracer";
                const std::string_view extension     = format == app::CaptureFormat::Png ? "png" : "exr";
                this->requested                      = Pending{
                    format,
                    directory / std::format("spectra-{}-{}.{}", renderer_name, timestamp, extension),
                                         {},
                    format == app::CaptureFormat::Exr && mode == workspace::RenderMode::PathTracer && film.gbuffer,
                    film.color_space,
                    film.gbuffer_camera_space,
                };
            }

            [[nodiscard]] std::optional<Completed> consume(const std::uint32_t frame_index, workspace::Workspace& workspace) noexcept {
                Slot& slot = this->slots[frame_index];
                if (!slot.pending) return std::nullopt;
                Completed completed{slot.pending->path, {}};
                try {
                    if (slot.pending->gbuffer) {
                        app::write_render_readback_exr(slot.pending->path, workspace.readback(), slot.pending->color_space, slot.pending->camera_space);
                    } else if (slot.pending->format == app::CaptureFormat::Png) {
                        const std::size_t pixel_count = static_cast<std::size_t>(slot.pending->extent.width) * slot.pending->extent.height;
                        app::write_png(slot.pending->path, std::span{static_cast<const std::uint8_t*>(slot.buffer.mapped), pixel_count * 4u}, slot.pending->extent.width, slot.pending->extent.height);
                    } else {
                        const std::size_t pixel_count = static_cast<std::size_t>(slot.pending->extent.width) * slot.pending->extent.height;
                        app::write_linear_exr(slot.pending->path, std::span{static_cast<const float*>(slot.buffer.mapped), pixel_count * 4u}, slot.pending->extent.width, slot.pending->extent.height, slot.pending->color_space);
                    }
                } catch (const std::exception& error) {
                    completed.error = error.what();
                }
                slot.pending.reset();
                return completed;
            }

            void record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_index, const render::RenderOutput source, const ViewportDisplay& display) {
                if (!this->requested) return;
                Slot& slot                 = this->slots[frame_index];
                const app::CaptureFormat format = this->requested->format;
                if (this->requested->gbuffer) {
                    slot.pending         = std::exchange(this->requested, std::nullopt);
                    slot.pending->extent = source.image.extent;
                    return;
                }
                const vk::Extent2D extent          = format == app::CaptureFormat::Png ? display.image.extent : source.image.extent;
                const vk::DeviceSize required_size = static_cast<vk::DeviceSize>(extent.width) * extent.height * (format == app::CaptureFormat::Png ? 4u : sizeof(float) * 4u);
                if (slot.buffer.size < required_size) slot.buffer = this->gpu->create_buffer(required_size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
                slot.pending                        = std::exchange(this->requested, std::nullopt);
                slot.pending->extent                = extent;
                const vk::Image image               = format == app::CaptureFormat::Png ? *display.image.image : *source.image.image;
                const vk::ImageLayout layout        = format == app::CaptureFormat::Png ? vk::ImageLayout::eShaderReadOnlyOptimal : source.layout;
                const vk::PipelineStageFlags2 stage = format == app::CaptureFormat::Png ? vk::PipelineStageFlagBits2::eFragmentShader : source.stage;
                const vk::AccessFlags2 access       = format == app::CaptureFormat::Png ? vk::AccessFlagBits2::eShaderSampledRead : source.access;
                const vk::ImageMemoryBarrier2 to_transfer{
                    stage,
                    access,
                    vk::PipelineStageFlagBits2::eCopy,
                    vk::AccessFlagBits2::eTransferRead,
                    layout,
                    vk::ImageLayout::eTransferSrcOptimal,
                    vk::QueueFamilyIgnored,
                    vk::QueueFamilyIgnored,
                    image,
                    {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
                };
                command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_transfer});
                command_buffer.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, *slot.buffer.buffer,
                    vk::BufferImageCopy{
                        0,
                        0,
                        0,
                        {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                        {0, 0, 0},
                        {extent.width, extent.height, 1},
                    });
                const std::array completion_barriers{
                    vk::ImageMemoryBarrier2{
                        vk::PipelineStageFlagBits2::eCopy,
                        vk::AccessFlagBits2::eTransferRead,
                        stage,
                        access,
                        vk::ImageLayout::eTransferSrcOptimal,
                        layout,
                        vk::QueueFamilyIgnored,
                        vk::QueueFamilyIgnored,
                        image,
                        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
                    },
                };
                const vk::MemoryBarrier2 host_barrier{
                    vk::PipelineStageFlagBits2::eCopy,
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eHost,
                    vk::AccessFlagBits2::eHostRead,
                };
                command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &host_barrier, {}, {}, static_cast<std::uint32_t>(completion_barriers.size()), completion_barriers.data()});
            }

        private:
            struct Pending {
                app::CaptureFormat format{app::CaptureFormat::None};
                std::filesystem::path path{};
                vk::Extent2D extent{};
                bool gbuffer{};
                scene::SpectrumColorSpace color_space{scene::SpectrumColorSpace::Srgb};
                bool camera_space{true};
            };

            struct Slot {
                GpuBuffer buffer{};
                std::optional<Pending> pending{};
            };

            GpuDevice* gpu{};
            std::vector<Slot> slots{};
            std::optional<Pending> requested{};
        };

        struct Application {
            Application(const std::filesystem::path& scene_path, const std::optional<std::filesystem::path>& plugin_path, const std::filesystem::path& shader_directory, const bool start_pathtracer) : com{}, runtime{}, imgui_platform(this->runtime.window), workspace(this->runtime.gpu, shader_directory, scene_path, plugin_path, Spectra::frames_in_flight), presenter(this->runtime.gpu, shader_directory), imgui_renderer(this->runtime.gpu, shader_directory, Spectra::frames_in_flight), viewport_display(this->runtime.gpu), capture(this->runtime.gpu, Spectra::frames_in_flight) {
                if (start_pathtracer) this->workspace.mode = workspace::RenderMode::PathTracer;
            }

            ~Application() {
                this->runtime.wait_idle();
            }

            Application(const Application&)            = delete;
            Application(Application&&)                 = delete;
            Application& operator=(const Application&) = delete;
            Application& operator=(Application&&)      = delete;

            void run(const std::optional<std::uint64_t> maximum_frame_count) {
                while (true) {
                    if (maximum_frame_count && this->frame_number >= *maximum_frame_count) break;
                    this->runtime.poll_events();
                    if (this->runtime.take_close_request()) break;
                    this->handle_dropped_paths();

                    const std::optional<FrameContext> frame = this->runtime.begin_frame();
                    if (!frame) {
                        this->runtime.wait_events();
                        continue;
                    }
                    if (const std::optional<CaptureManager::Completed> completed = this->capture.consume(frame->index, this->workspace)) {
                        if (completed->error.empty()) {
                            this->workspace_ui.status = std::format("Capture written  {}", completed->path.filename().string());
                            this->workspace_ui.status_error = false;
                        } else {
                            this->workspace_ui.status       = completed->error;
                            this->workspace_ui.status_error = true;
                        }
                    }
                    this->workspace.begin_frame(frame->index);
                    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
                    const double delta_seconds                      = std::chrono::duration<double>(now - this->previous_frame_time).count();
                    this->previous_frame_time                       = now;
                    this->workspace.update(delta_seconds);

                    this->resize_viewport(frame->target.extent);
                    ImGui_ImplGlfw_NewFrame();
                    ImGui::NewFrame();
                    const app::WorkspaceUiActions actions = this->workspace_ui.draw(this->workspace, this->viewport_display.texture_id);
                    this->runtime.drag_regions = actions.drag_regions;
                    this->handle_actions(actions);
                    ImGui::Render();

                    const vk::Extent2D viewport_extent = this->viewport_display.image.extent;
                    this->workspace.prepare(frame->command_buffer, viewport_extent);
                    this->workspace.record(frame->command_buffer, frame->index);
                    const render::RenderOutput output = this->workspace.output();
                    this->presenter.record(frame->command_buffer, output, *this->viewport_display.image.image, *this->viewport_display.image.view, viewport_extent, this->viewport_display.layout, vk::ImageLayout::eColorAttachmentOptimal, this->workspace.exposure);
                    this->viewport_display.layout = vk::ImageLayout::eColorAttachmentOptimal;
                    this->workspace.record_overlays(frame->command_buffer, *this->viewport_display.image.image, *this->viewport_display.image.view, viewport_extent, actions.show_axes);
                    this->viewport_display.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
                    this->capture.record(frame->command_buffer, frame->index, output, this->viewport_display);
                    this->imgui_renderer.record(*ImGui::GetDrawData(), frame->command_buffer, frame->index, frame->target.image, frame->target.view, frame->target.extent, frame->target.layout, vk::ImageLayout::ePresentSrcKHR);
                    if (this->runtime.present_frame()) ++this->frame_number;
                }
            }

            void resize_viewport(const vk::Extent2D requested) {
                if (*this->viewport_display.image.image && requested == this->viewport_display.image.extent) return;
                this->runtime.wait_idle();
                this->viewport_display.ensure(requested);
            }

            [[nodiscard]] bool confirm_scene_replacement() {
                if (!this->workspace.dirty()) return true;
                const int result = MessageBoxW(glfwGetWin32Window(this->runtime.window), L"The current Spectra scene has unsaved changes.\n\nSave before continuing?", L"Spectra", MB_ICONWARNING | MB_YESNOCANCEL);
                if (result == IDCANCEL) return false;
                if (result == IDYES) this->workspace.save();
                return true;
            }

            void open_source(const std::filesystem::path& path) {
                if (!this->confirm_scene_replacement()) return;
                if (path.extension() == ".spectra")
                    this->workspace.open_scene(path);
                else if (path.extension() == ".dll")
                    this->workspace.open_plugin(path);
                else
                    throw std::runtime_error("Spectra accepts only .spectra scenes and Plugin API libraries");
            }

            void reload_source() {
                if (!this->confirm_scene_replacement()) return;
                if (this->workspace.provider == workspace::SceneProvider::File)
                    this->workspace.open_scene(this->workspace.source_path);
                else
                    this->workspace.open_plugin(this->workspace.source_path);
                this->workspace_ui.status       = "Scene reloaded";
                this->workspace_ui.status_error = false;
            }

            void handle_dropped_paths() {
                const std::vector<std::filesystem::path> paths = this->runtime.take_dropped_paths();
                if (paths.empty()) return;
                try {
                    if (paths.size() != 1u) throw std::runtime_error("Drop exactly one .spectra scene or plugin library");
                    this->open_source(paths.front());
                } catch (const std::exception& error) {
                    this->workspace_ui.status       = error.what();
                    this->workspace_ui.status_error = true;
                }
            }

            void handle_actions(const app::WorkspaceUiActions& actions) {
                if (actions.exit_application) this->runtime.request_close();
                try {
                    if (actions.open_scene)
                        if (const std::optional<std::filesystem::path> path = open_source_dialog(glfwGetWin32Window(this->runtime.window))) this->open_source(*path);
                    if (actions.reload_scene) this->reload_source();
                    if (actions.save_scene) {
                        this->workspace.save();
                        this->workspace_ui.status       = "Scene saved";
                        this->workspace_ui.status_error = false;
                    }
                    if (actions.save_scene_as)
                        if (const std::optional<std::filesystem::path> path = save_scene_dialog(glfwGetWin32Window(this->runtime.window), this->workspace.source_path)) {
                            this->workspace.save_as(*path);
                            this->workspace_ui.status       = "Scene saved";
                            this->workspace_ui.status_error = false;
                        }
                    if (actions.capture != app::CaptureFormat::None) this->capture.request(actions.capture, this->workspace.mode, this->workspace.scene.film());
                } catch (const std::exception& error) {
                    this->workspace_ui.status       = error.what();
                    this->workspace_ui.status_error = true;
                }
            }

            ComApartment com{};
            Spectra runtime{};
            ImGuiPlatform imgui_platform;
            workspace::Workspace workspace;
            app::WorkspaceUi workspace_ui{};
            app::Presenter presenter;
            ui::ImGuiRenderer imgui_renderer;
            ViewportDisplay viewport_display;
            CaptureManager capture;
            std::uint64_t frame_number{};
            std::chrono::steady_clock::time_point previous_frame_time{std::chrono::steady_clock::now()};
        };
    } // namespace

    void run_application(const std::filesystem::path& scene_path, const std::optional<std::filesystem::path>& plugin_path, const std::filesystem::path& shader_directory, const std::optional<std::uint64_t> maximum_frame_count, const bool start_pathtracer) {
        Application application{scene_path, plugin_path, shader_directory, start_pathtracer};
        application.run(maximum_frame_count);
    }
} // namespace spectra
