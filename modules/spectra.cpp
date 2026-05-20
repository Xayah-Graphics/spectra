module;
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
#define GLFW_INCLUDE_VULKAN
#include <cuda_runtime_api.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <material_symbols/material_symbols_rounded_regular.h>
#include <roboto/roboto_mono.h>
#include <roboto/roboto_regular.h>

#include <pbrt/base/film.h>
#include <pbrt/base/sampler.h>
#include <pbrt/gpu/memory.h>
#include <pbrt/gpu/util.h>
#include <pbrt/options.h>
#include <pbrt/parser.h>
#include <pbrt/scene.h>
#include <pbrt/util/color.h>
#include <pbrt/util/print.h>
#include <pbrt/util/transform.h>
#include <pbrt/wavefront/integrator.h>

#include <vulkan/vulkan_raii.hpp>
module spectra;
import std;

namespace {
    void transition_image_layout(const vk::raii::CommandBuffer& command_buffer, const vk::Image image, const vk::ImageLayout old_layout, const vk::ImageLayout new_layout, const vk::ImageAspectFlags aspect, const vk::PipelineStageFlags2 src_stage, const vk::AccessFlags2 src_access, const vk::PipelineStageFlags2 dst_stage, const vk::AccessFlags2 dst_access) {
        const vk::ImageMemoryBarrier2 image_memory_barrier{
            src_stage,
            src_access,
            dst_stage,
            dst_access,
            old_layout,
            new_layout,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            image,
            {aspect, 0, 1, 0, 1},
        };
        const vk::DependencyInfo dependency_info{{}, 0, nullptr, 0, nullptr, 1, &image_memory_barrier};
        command_buffer.pipelineBarrier2(dependency_info);
    }

    VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(const vk::DebugUtilsMessageSeverityFlagBitsEXT severity, const vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* callback_data, void*) {
        if (vk::DebugUtilsMessageSeverityFlagsEXT{severity} & (vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)) std::cerr << "validation layer: type " << vk::to_string(type) << " msg: " << callback_data->pMessage << std::endl;
        return VK_FALSE;
    }

    [[nodiscard]] std::uint32_t find_memory_type_index(const vk::raii::PhysicalDevice& physical_device, const std::uint32_t memory_type_bits, const vk::MemoryPropertyFlags required_properties) {
        const vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            const bool supported = (memory_type_bits & (1u << index)) != 0;
            const bool matching  = (memory_properties.memoryTypes[index].propertyFlags & required_properties) == required_properties;
            if (supported && matching) return index;
        }
        throw std::runtime_error("No matching Vulkan memory type");
    }

    [[nodiscard]] vk::ExternalMemoryHandleTypeFlagBits pbrt_external_memory_handle_type() {
#if defined(_WIN32)
        return vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
#else
        return vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
#endif
    }

    [[nodiscard]] vk::ExternalSemaphoreHandleTypeFlagBits pbrt_external_semaphore_handle_type() {
#if defined(_WIN32)
        return vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32;
#else
        return vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd;
#endif
    }

    [[nodiscard]] cudaExternalMemoryHandleType pbrt_cuda_external_memory_handle_type() {
#if defined(_WIN32)
        return cudaExternalMemoryHandleTypeOpaqueWin32;
#else
        return cudaExternalMemoryHandleTypeOpaqueFd;
#endif
    }

    [[nodiscard]] cudaExternalSemaphoreHandleType pbrt_cuda_external_semaphore_handle_type() {
#if defined(_WIN32)
        return cudaExternalSemaphoreHandleTypeOpaqueWin32;
#else
        return cudaExternalSemaphoreHandleTypeOpaqueFd;
#endif
    }

    void draw_statistics_row(const char* label, const char* value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value);
    }

    void draw_statistics_row(const char* label, const std::string& value) {
        draw_statistics_row(label, value.c_str());
    }

    [[nodiscard]] std::string format_device_bytes(const vk::DeviceSize bytes) {
        const double megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
        return std::format("{} bytes ({:.2f} MiB)", static_cast<unsigned long long>(bytes), megabytes);
    }

    [[nodiscard]] std::array<float, 16> identity_matrix_array() {
        return {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
    }

    [[nodiscard]] std::array<float, 16> matrix_array_from_transform(const pbrt::Transform& transform) {
        std::array<float, 16> values{};
        const pbrt::SquareMatrix<4>& matrix = transform.GetMatrix();
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                const float value = static_cast<float>(matrix[row][column]);
                if (!std::isfinite(value)) throw std::runtime_error("Camera transform matrix contains a non-finite value");
                values[row * 4 + column] = value;
            }
        }
        return values;
    }

    [[nodiscard]] pbrt::Transform transform_from_matrix_array(const std::array<float, 16>& values) {
        pbrt::Float matrix[4][4]{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                const float value = values[row * 4 + column];
                if (!std::isfinite(value)) throw std::runtime_error("Camera transform matrix contains a non-finite value");
                matrix[row][column] = static_cast<pbrt::Float>(value);
            }
        }
        return pbrt::Transform{matrix};
    }

    [[nodiscard]] std::string pbrt_transform_text(const pbrt::Transform& transform) {
        const pbrt::SquareMatrix<4>& matrix = transform.GetMatrix();
        std::string text{"Transform [ "};
        for (int index = 0; index < 16; ++index) {
            const float value = static_cast<float>(matrix[index % 4][index / 4]);
            if (!std::isfinite(value)) throw std::runtime_error("Camera transform text contains a non-finite value");
            text += std::format("{:.9g} ", value);
        }
        text += "]";
        return text;
    }

    [[nodiscard]] ImVec4 imgui_srgb(const float red, const float green, const float blue, const float alpha) {
        return ImVec4{red, green, blue, alpha};
    }

    void load_imgui_fonts() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.Fonts == nullptr) throw std::runtime_error("ImGui font atlas is unavailable");

        ImFontConfig font_config{};
        font_config.OversampleH   = 3;
        font_config.OversampleV   = 3;
        constexpr float font_size = 15.0f;
        ImFont* default_font      = io.Fonts->AddFontFromMemoryCompressedTTF(g_roboto_regular_compressed_data, g_roboto_regular_compressed_size, font_size, &font_config);
        if (default_font == nullptr) throw std::runtime_error("Failed to load Roboto regular font");

        ImFontConfig icon_config{};
        icon_config.MergeMode     = true;
        icon_config.PixelSnapH    = true;
        icon_config.OversampleH   = 3;
        icon_config.OversampleV   = 3;
        constexpr float icon_size = 1.28571429f * font_size;
        icon_config.GlyphOffset.x = icon_size * 0.01f;
        icon_config.GlyphOffset.y = icon_size * 0.2f;
        constexpr std::array<ImWchar, 3> icon_ranges{ICON_MIN_MS, ICON_MAX_MS, 0};
        if (io.Fonts->AddFontFromMemoryCompressedTTF(g_materialSymbolsRounded_compressed_data, g_materialSymbolsRounded_compressed_size, icon_size, &icon_config, icon_ranges.data()) == nullptr) throw std::runtime_error("Failed to load Material Symbols icon font");

        ImFontConfig mono_config{};
        mono_config.OversampleH = 3;
        mono_config.OversampleV = 3;
        if (io.Fonts->AddFontFromMemoryCompressedTTF(g_roboto_mono_compressed_data, g_roboto_mono_compressed_size, font_size, &mono_config) == nullptr) throw std::runtime_error("Failed to load Roboto mono font");
        io.FontDefault = default_font;
    }

    void apply_imgui_style(const bool viewports) {
        ImGui::StyleColorsDark();
        ImGuiStyle& style                  = ImGui::GetStyle();
        style.WindowRounding               = 0.0f;
        style.WindowBorderSize             = 0.0f;
        style.ColorButtonPosition          = ImGuiDir_Right;
        style.FrameRounding                = 2.0f;
        style.FrameBorderSize              = 1.0f;
        style.GrabRounding                 = 4.0f;
        style.IndentSpacing                = 12.0f;
        style.Colors[ImGuiCol_WindowBg]    = imgui_srgb(0.2f, 0.2f, 0.2f, 1.0f);
        style.Colors[ImGuiCol_MenuBarBg]   = imgui_srgb(0.2f, 0.2f, 0.2f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarBg] = imgui_srgb(0.2f, 0.2f, 0.2f, 1.0f);
        style.Colors[ImGuiCol_PopupBg]     = imgui_srgb(0.135f, 0.135f, 0.135f, 1.0f);
        style.Colors[ImGuiCol_Border]      = imgui_srgb(0.4f, 0.4f, 0.4f, 0.5f);
        style.Colors[ImGuiCol_FrameBg]     = imgui_srgb(0.05f, 0.05f, 0.05f, 0.5f);

        const ImVec4 normal_color = imgui_srgb(0.465f, 0.465f, 0.525f, 1.0f);
        constexpr std::array normal_colors{
            ImGuiCol_Header,
            ImGuiCol_SliderGrab,
            ImGuiCol_Button,
            ImGuiCol_CheckMark,
            ImGuiCol_ResizeGrip,
            ImGuiCol_TextSelectedBg,
            ImGuiCol_Separator,
            ImGuiCol_FrameBgActive,
        };
        for (const ImGuiCol color_id : normal_colors) style.Colors[color_id] = normal_color;

        const ImVec4 active_color = imgui_srgb(0.365f, 0.365f, 0.425f, 1.0f);
        constexpr std::array active_colors{
            ImGuiCol_HeaderActive,
            ImGuiCol_SliderGrabActive,
            ImGuiCol_ButtonActive,
            ImGuiCol_ResizeGripActive,
            ImGuiCol_SeparatorActive,
        };
        for (const ImGuiCol color_id : active_colors) style.Colors[color_id] = active_color;

        const ImVec4 hovered_color = imgui_srgb(0.565f, 0.565f, 0.625f, 1.0f);
        constexpr std::array hovered_colors{
            ImGuiCol_HeaderHovered,
            ImGuiCol_ButtonHovered,
            ImGuiCol_FrameBgHovered,
            ImGuiCol_ResizeGripHovered,
            ImGuiCol_SeparatorHovered,
        };
        for (const ImGuiCol color_id : hovered_colors) style.Colors[color_id] = hovered_color;

        style.Colors[ImGuiCol_TitleBgActive]    = imgui_srgb(0.465f, 0.465f, 0.465f, 1.0f);
        style.Colors[ImGuiCol_TitleBg]          = imgui_srgb(0.125f, 0.125f, 0.125f, 1.0f);
        style.Colors[ImGuiCol_Tab]              = imgui_srgb(0.05f, 0.05f, 0.05f, 0.5f);
        style.Colors[ImGuiCol_TabHovered]       = imgui_srgb(0.465f, 0.495f, 0.525f, 1.0f);
        style.Colors[ImGuiCol_TabActive]        = imgui_srgb(0.282f, 0.290f, 0.302f, 1.0f);
        style.Colors[ImGuiCol_ModalWindowDimBg] = imgui_srgb(0.465f, 0.465f, 0.465f, 0.350f);
        style.Colors[ImGuiCol_ButtonActive]     = static_cast<ImVec4>(ImColor::HSV(0.3F, 0.5F, 0.5F));
        ImGui::SetColorEditOptions(ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
        if (viewports) {
            style.WindowRounding              = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
    }
} // namespace

namespace xayah {
    struct SpectraPbrtScene {
        std::filesystem::path scene_path{};
        std::string scene_label{"No Scene"};
        std::string scene_path_text{};
        std::array<int, 2> film_resolution{0, 0};
        std::array<float, 16> camera_from_world{};
        int sampler_sample_count{0};

        void load(const std::filesystem::path& path) {
            if (!this->scene_path.empty()) throw std::runtime_error("PBRT scene metadata is already loaded");
            if (path.empty()) throw std::runtime_error("PBRT scene path is empty");
            if (!std::filesystem::exists(path)) throw std::runtime_error(std::string{"PBRT scene does not exist: "} + path.string());

            this->scene_path      = path;
            this->scene_label     = path.filename().string();
            this->scene_path_text = path.string();
        }

        void set_runtime_metadata(const std::array<int, 2>& resolution, const int samples_per_pixel, const std::array<float, 16>& camera_transform) {
            if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("PBRT film resolution must be positive");
            this->film_resolution = resolution;
            this->sampler_sample_count = samples_per_pixel;
            if (this->sampler_sample_count <= 0) throw std::runtime_error("PBRT sampler SPP must be positive");
            this->camera_from_world = camera_transform;
        }

        void unload_noexcept() noexcept {
            this->scene_path.clear();
            this->scene_label = "No Scene";
            this->scene_path_text.clear();
            this->film_resolution = {0, 0};
            this->camera_from_world = identity_matrix_array();
            this->sampler_sample_count = 0;
        }
    };

    struct SpectraPbrtInteractiveSession {
        struct FrameResource {
            vk::raii::Buffer interop_buffer{nullptr};
            vk::raii::DeviceMemory interop_memory{nullptr};
            vk::DeviceSize interop_allocation_size{0};
            vk::DeviceSize interop_buffer_size{0};
            vk::raii::Semaphore cuda_complete_semaphore{nullptr};
            cudaExternalMemory_t cuda_external_memory{};
            cudaExternalSemaphore_t cuda_external_semaphore{};
            float* cuda_pixels{nullptr};

            vk::raii::DeviceMemory image_memory{nullptr};
            vk::raii::Image image{nullptr};
            vk::raii::ImageView image_view{nullptr};
            vk::raii::Sampler sampler{nullptr};
            VkDescriptorSet imgui_descriptor{VK_NULL_HANDLE};
            vk::ImageLayout image_layout{vk::ImageLayout::eUndefined};
        };

        struct RenderFrameResult {
            std::uint64_t sample_pixels{0};
            bool rendered_sample{false};
            bool reset_accumulation{false};
        };

        std::filesystem::path scene_path{};
        pbrt::PBRTOptions options{};
        pbrt::BasicScene scene{};
        std::unique_ptr<pbrt::WavefrontPathIntegrator> integrator{};
        pbrt::Bounds2i pixel_bounds{};
        pbrt::Vector2i resolution{};
        vk::Format display_format{vk::Format::eR32G32B32A32Sfloat};
        pbrt::Transform render_from_camera{};
        pbrt::Transform camera_from_render{};
        pbrt::Transform camera_from_world{};
        pbrt::Float exposure{1.0f};
        pbrt::Float initial_move_scale{1.0f};
        int sample_index{0};
        int max_samples{0};
        int target_samples{0};
        bool reset_requested{false};
        bool pbrt_initialized{false};
        std::uint32_t active_frame_index{0};
        const vk::raii::PhysicalDevice* physical_device{nullptr};
        const vk::raii::Device* device{nullptr};
        std::uint32_t frame_count{0};
        std::vector<FrameResource> frames{};

        SpectraPbrtInteractiveSession(const std::filesystem::path& path, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count) : scene_path{path} {
            try {
            if (this->scene_path.empty()) throw std::runtime_error("PBRT scene path is empty");
            if (!std::filesystem::exists(this->scene_path)) throw std::runtime_error(std::string{"PBRT scene does not exist: "} + this->scene_path.string());
            if (frame_count == 0) throw std::runtime_error("PBRT interactive requires at least one frame in flight");
            this->physical_device = &physical_device;
            this->device          = &device;
            this->frame_count     = frame_count;

            this->options.useGPU         = true;
            this->options.wavefront      = false;
            this->options.nThreads       = 30;
            this->options.renderingSpace = pbrt::RenderingCoordinateSystem::CameraWorld;
            pbrt::InitPBRT(this->options);
            this->pbrt_initialized = true;

            std::vector<std::string> filenames{this->scene_path.string()};
            pbrt::BasicSceneBuilder builder{&this->scene};
            pbrt::ParseFiles(&builder, filenames);

            this->integrator = std::make_unique<pbrt::WavefrontPathIntegrator>(&pbrt::CUDATrackedMemoryResource::singleton, this->scene);
#ifdef PBRT_BUILD_GPU_RENDERER
            if (this->options.useGPU) this->integrator->PrefetchGPUAllocations();
#endif
            this->pixel_bounds = this->integrator->film.PixelBounds();
            this->resolution   = this->pixel_bounds.Diagonal();
            if (this->resolution.x <= 0 || this->resolution.y <= 0) throw std::runtime_error("PBRT film resolution must be positive");
            this->max_samples = this->integrator->sampler.SamplesPerPixel();
            if (this->max_samples <= 0) throw std::runtime_error("PBRT sampler SPP must be positive");
            this->target_samples = this->max_samples;
            this->render_one_sample(pbrt::Transform{});
            pbrt::GPUWait();

            this->render_from_camera = this->integrator->camera.GetCameraTransform().RenderFromCamera().startTransform;
            this->camera_from_render = pbrt::Inverse(this->render_from_camera);
            this->camera_from_world  = this->integrator->camera.GetCameraTransform().CameraFromWorld(this->integrator->camera.SampleTime(0.0f));
            const pbrt::Bounds3f scene_bounds = this->integrator->aggregate->Bounds();
            this->initial_move_scale          = pbrt::Length(scene_bounds.Diagonal()) / 1000.0f;
            if (!(this->initial_move_scale > 0.0f)) throw std::runtime_error("PBRT scene bounds must define a positive interactive move scale");

            this->validate_cuda_vulkan_device(physical_device);
            this->create_frame_resources(physical_device, device, frame_count);
            this->create_imgui_descriptors();
            } catch (...) {
                this->destroy_resources_noexcept();
                throw;
            }
        }

        ~SpectraPbrtInteractiveSession() noexcept {
            this->destroy_resources_noexcept();
        }

        void destroy_resources_noexcept() noexcept {
            try {
                if (this->device != nullptr) this->device->waitIdle();
                if (this->pbrt_initialized && pbrt::Options != nullptr && pbrt::Options->useGPU) pbrt::GPUWait();
            } catch (...) {
            }
            this->destroy_frame_resources_noexcept();
            this->integrator.reset();
            if (this->pbrt_initialized) {
                try {
                    pbrt::CleanupPBRT();
                } catch (...) {
                }
                this->pbrt_initialized = false;
            }
        }

        SpectraPbrtInteractiveSession(const SpectraPbrtInteractiveSession& other)                = delete;
        SpectraPbrtInteractiveSession(SpectraPbrtInteractiveSession&& other) noexcept            = delete;
        SpectraPbrtInteractiveSession& operator=(const SpectraPbrtInteractiveSession& other)     = delete;
        SpectraPbrtInteractiveSession& operator=(SpectraPbrtInteractiveSession&& other) noexcept = delete;

        [[nodiscard]] int current_sample() const {
            if (this->reset_requested) return 0;
            return this->sample_index;
        }

        [[nodiscard]] int sampler_sample_count() const {
            return this->max_samples;
        }

        [[nodiscard]] int target_sample_count() const {
            return this->target_samples;
        }

        [[nodiscard]] float current_exposure() const {
            return static_cast<float>(this->exposure);
        }

        [[nodiscard]] float camera_initial_move_scale() const {
            if (!(this->initial_move_scale > 0.0f)) throw std::runtime_error("PBRT interactive camera initial move scale must be positive");
            return static_cast<float>(this->initial_move_scale);
        }

        [[nodiscard]] std::array<int, 2> film_resolution() const {
            if (this->resolution.x <= 0 || this->resolution.y <= 0) throw std::runtime_error("PBRT film resolution must be positive before metadata is queried");
            return {this->resolution.x, this->resolution.y};
        }

        [[nodiscard]] std::array<float, 16> camera_from_world_matrix() const {
            return matrix_array_from_transform(this->camera_from_world);
        }

        [[nodiscard]] std::uint64_t film_pixel_count() const {
            if (this->resolution.x <= 0 || this->resolution.y <= 0) throw std::runtime_error("PBRT film resolution must be positive before statistics are queried");
            return static_cast<std::uint64_t>(this->resolution.x) * static_cast<std::uint64_t>(this->resolution.y);
        }

        [[nodiscard]] float completion_ratio() const {
            if (this->target_samples <= 0) throw std::runtime_error("PBRT target sample count must be positive before statistics are queried");
            const int visible_sample = this->current_sample();
            if (visible_sample < 0 || visible_sample > this->target_samples) throw std::runtime_error("PBRT visible sample count is outside the target sample range");
            return static_cast<float>(visible_sample) / static_cast<float>(this->target_samples);
        }

        [[nodiscard]] std::uint32_t active_frame() const {
            if (this->frames.empty()) throw std::runtime_error("PBRT interactive statistics require frame resources");
            return this->active_frame_index;
        }

        [[nodiscard]] vk::Format active_display_format() const {
            return this->display_format;
        }

        [[nodiscard]] vk::DeviceSize active_interop_buffer_size() const {
            if (this->frames.empty()) throw std::runtime_error("PBRT interactive statistics require frame resources");
            return this->frames.at(this->active_frame_index).interop_buffer_size;
        }

        [[nodiscard]] vk::ImageLayout active_image_layout() const {
            if (this->frames.empty()) throw std::runtime_error("PBRT interactive statistics require frame resources");
            return this->frames.at(this->active_frame_index).image_layout;
        }

        [[nodiscard]] bool active_cuda_semaphore_imported() const {
            if (this->frames.empty()) throw std::runtime_error("PBRT interactive statistics require frame resources");
            return this->frames.at(this->active_frame_index).cuda_external_semaphore != nullptr;
        }

        [[nodiscard]] bool active_cuda_pixel_buffer_mapped() const {
            if (this->frames.empty()) throw std::runtime_error("PBRT interactive statistics require frame resources");
            return this->frames.at(this->active_frame_index).cuda_pixels != nullptr;
        }

        [[nodiscard]] VkDescriptorSet active_descriptor() const {
            if (this->frames.empty()) return VK_NULL_HANDLE;
            return this->frames.at(this->active_frame_index).imgui_descriptor;
        }

        [[nodiscard]] vk::Semaphore active_cuda_complete_semaphore() const {
            if (this->frames.empty()) throw std::runtime_error("PBRT completion semaphore requested without frame resources");
            return *this->frames.at(this->active_frame_index).cuda_complete_semaphore;
        }

        void set_target_sample_count(const int target_sample_count) {
            if (target_sample_count < 1 || target_sample_count > this->max_samples) throw std::runtime_error("PBRT target sample count is outside the sampler SPP range");
            if (target_sample_count == this->target_samples) return;
            this->target_samples = target_sample_count;
            this->request_reset_accumulation();
        }

        void set_exposure(const float value) {
            if (!(value >= 0.001f && value <= 1000.0f)) throw std::runtime_error("PBRT exposure must be in [0.001, 1000]");
            this->exposure = static_cast<pbrt::Float>(value);
        }

        void request_reset_accumulation() {
            this->reset_requested = true;
        }

        void release_imgui_descriptors() noexcept {
            for (FrameResource& frame : this->frames) {
                if (frame.imgui_descriptor != VK_NULL_HANDLE) {
                    ImGui_ImplVulkan_RemoveTexture(frame.imgui_descriptor);
                    frame.imgui_descriptor = VK_NULL_HANDLE;
                }
            }
        }

        void create_imgui_descriptors() {
            for (FrameResource& frame : this->frames) {
                if (frame.imgui_descriptor != VK_NULL_HANDLE) throw std::runtime_error("PBRT interactive ImGui descriptor is already allocated");
                frame.imgui_descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(*frame.sampler), static_cast<VkImageView>(*frame.image_view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                if (frame.imgui_descriptor == VK_NULL_HANDLE) throw std::runtime_error("Failed to allocate ImGui descriptor for PBRT interactive image");
            }
        }

        void destroy_frame_resources_noexcept() noexcept {
            this->release_imgui_descriptors();
            for (FrameResource& frame : this->frames) {
                if (frame.cuda_pixels != nullptr) {
                    cudaFree(frame.cuda_pixels);
                    frame.cuda_pixels = nullptr;
                }
                if (frame.cuda_external_semaphore != nullptr) {
                    cudaDestroyExternalSemaphore(frame.cuda_external_semaphore);
                    frame.cuda_external_semaphore = nullptr;
                }
                if (frame.cuda_external_memory != nullptr) {
                    cudaDestroyExternalMemory(frame.cuda_external_memory);
                    frame.cuda_external_memory = nullptr;
                }
            }
            this->frames.clear();
            this->active_frame_index = 0;
        }

        void render_one_sample(const pbrt::Transform& camera_motion) {
            if (this->sample_index >= this->target_samples) throw std::runtime_error("PBRT sample index is already at the target sample count");
            this->integrator->RenderSample(this->pixel_bounds, camera_motion, this->sample_index);
            ++this->sample_index;
        }

        void rerender_after_reset(const std::uint32_t frame_index, const pbrt::Transform& camera_motion) {
            if (this->physical_device == nullptr || this->device == nullptr) throw std::runtime_error("PBRT interactive Vulkan handles are not available for reset");
            this->device->waitIdle();
            this->destroy_frame_resources_noexcept();
            this->integrator->ResetFilm(this->pixel_bounds);
            pbrt::GPUWait();
            this->sample_index     = 0;
            this->reset_requested  = false;
            this->render_one_sample(camera_motion);
            pbrt::GPUWait();
            this->create_frame_resources(*this->physical_device, *this->device, this->frame_count);
            this->create_imgui_descriptors();
            this->active_frame_index = frame_index;
        }

        void process_pathtracer_input(const bool input_enabled) {
            ImGuiIO& io = ImGui::GetIO();
            if (!input_enabled) return;
            if (ImGui::IsKeyPressed(ImGuiKey_B, false)) {
                if (io.KeyShift)
                    this->set_exposure(std::clamp(static_cast<float>(this->exposure / 1.125f), 0.001f, 1000.0f));
                else
                    this->set_exposure(std::clamp(static_cast<float>(this->exposure * 1.125f), 0.001f, 1000.0f));
            }
        }

        [[nodiscard]] RenderFrameResult render_frame(const std::uint32_t frame_index, const pbrt::Transform& moving_from_camera) {
            if (frame_index >= this->frames.size()) throw std::runtime_error("PBRT interactive frame index is out of range");
            this->active_frame_index = frame_index;
            RenderFrameResult result{};
            const pbrt::Transform camera_motion = this->render_from_camera * moving_from_camera * this->camera_from_render;
            if (this->reset_requested) {
                this->rerender_after_reset(frame_index, camera_motion);
                result.rendered_sample    = true;
                result.sample_pixels      = this->film_pixel_count() * static_cast<std::uint64_t>(this->sample_index);
                result.reset_accumulation = true;
            } else if (this->sample_index < this->target_samples) {
                this->render_one_sample(camera_motion);
                result.rendered_sample = true;
                result.sample_pixels   = this->film_pixel_count();
            }
            FrameResource& output_frame = this->frames.at(frame_index);
            this->integrator->UpdateFramebufferFromFilm(this->pixel_bounds, this->exposure, output_frame.cuda_pixels);

            cudaExternalSemaphoreSignalParams signal_params{};
            CUDA_CHECK(cudaSignalExternalSemaphoresAsync(&output_frame.cuda_external_semaphore, &signal_params, 1, 0));
            return result;
        }

        void record_copy(const vk::raii::CommandBuffer& command_buffer) {
            FrameResource& frame = this->frames.at(this->active_frame_index);
            const vk::PipelineStageFlags2 src_image_stage = frame.image_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eFragmentShader;
            const vk::AccessFlags2 src_image_access       = frame.image_layout == vk::ImageLayout::eUndefined ? vk::AccessFlagBits2::eNone : vk::AccessFlagBits2::eShaderSampledRead;
            transition_image_layout(command_buffer, *frame.image, frame.image_layout, vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor, src_image_stage, src_image_access, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
            frame.image_layout = vk::ImageLayout::eTransferDstOptimal;

            const vk::BufferMemoryBarrier2 buffer_barrier{
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryWrite,
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferRead,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                *frame.interop_buffer,
                0,
                frame.interop_buffer_size,
            };
            const vk::DependencyInfo dependency_info{{}, 0, nullptr, 1, &buffer_barrier, 0, nullptr};
            command_buffer.pipelineBarrier2(dependency_info);

            const vk::BufferImageCopy copy_region{
                0,
                0,
                0,
                {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                {0, 0, 0},
                {static_cast<std::uint32_t>(this->resolution.x), static_cast<std::uint32_t>(this->resolution.y), 1},
            };
            command_buffer.copyBufferToImage(*frame.interop_buffer, *frame.image, vk::ImageLayout::eTransferDstOptimal, copy_region);

            transition_image_layout(command_buffer, *frame.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
            frame.image_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }

    private:
        void validate_cuda_vulkan_device(const vk::raii::PhysicalDevice& physical_device) const {
            int cuda_device = 0;
            CUDA_CHECK(cudaGetDevice(&cuda_device));
            cudaDeviceProp cuda_properties{};
            CUDA_CHECK(cudaGetDeviceProperties(&cuda_properties, cuda_device));
            const auto vulkan_properties = physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
            const vk::PhysicalDeviceIDProperties& vulkan_id = vulkan_properties.get<vk::PhysicalDeviceIDProperties>();
#if defined(_WIN32)
            if (!vulkan_id.deviceLUIDValid) throw std::runtime_error("Selected Vulkan device does not expose a valid LUID for CUDA interop");
            for (std::size_t index = 0; index < VK_LUID_SIZE; ++index) {
                if (static_cast<unsigned char>(cuda_properties.luid[index]) != vulkan_id.deviceLUID[index]) throw std::runtime_error("CUDA device LUID does not match selected Vulkan device LUID");
            }
#else
            for (std::size_t index = 0; index < VK_UUID_SIZE; ++index) {
                if (static_cast<unsigned char>(cuda_properties.uuid.bytes[index]) != vulkan_id.deviceUUID[index]) throw std::runtime_error("CUDA device UUID does not match selected Vulkan device UUID");
            }
#endif
        }

        void create_frame_resources(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count) {
            const vk::FormatProperties format_properties = physical_device.getFormatProperties(this->display_format);
            constexpr vk::FormatFeatureFlags required_features = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eTransferDst;
            if ((format_properties.optimalTilingFeatures & required_features) != required_features) throw std::runtime_error("Vulkan device does not support sampled transfer destination R32G32B32A32_SFLOAT images");

            const vk::DeviceSize rgba_bytes = static_cast<vk::DeviceSize>(sizeof(float)) * 4u * static_cast<vk::DeviceSize>(this->resolution.x) * static_cast<vk::DeviceSize>(this->resolution.y);
            if (rgba_bytes == 0) throw std::runtime_error("PBRT interactive interop buffer cannot be zero bytes");
            this->frames.resize(frame_count);
            for (FrameResource& frame : this->frames) {
                this->create_interop_buffer(physical_device, device, frame, rgba_bytes);
                this->create_cuda_complete_semaphore(device, frame);
                this->create_display_image(physical_device, device, frame, this->display_format);
            }
        }

        void create_interop_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, FrameResource& frame, const vk::DeviceSize rgba_bytes) {
            const vk::ExternalMemoryBufferCreateInfo external_buffer_info{pbrt_external_memory_handle_type()};
            const vk::BufferCreateInfo buffer_create_info{{}, rgba_bytes, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive, 0, nullptr, &external_buffer_info};
            frame.interop_buffer = vk::raii::Buffer{device, buffer_create_info};

            const vk::MemoryRequirements memory_requirements = frame.interop_buffer.getMemoryRequirements();
            const std::uint32_t memory_type                  = find_memory_type_index(physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            const vk::ExportMemoryAllocateInfo export_allocate_info{pbrt_external_memory_handle_type()};
            const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, memory_type, &export_allocate_info};
            frame.interop_memory = vk::raii::DeviceMemory{device, allocate_info};
            frame.interop_buffer.bindMemory(*frame.interop_memory, 0);
            frame.interop_allocation_size = memory_requirements.size;
            frame.interop_buffer_size     = rgba_bytes;

            cudaExternalMemoryHandleDesc memory_handle_desc{};
            memory_handle_desc.type = pbrt_cuda_external_memory_handle_type();
            memory_handle_desc.size = static_cast<unsigned long long>(frame.interop_allocation_size);
#if defined(_WIN32)
            const vk::MemoryGetWin32HandleInfoKHR memory_handle_info{*frame.interop_memory, pbrt_external_memory_handle_type()};
            HANDLE memory_handle = device.getMemoryWin32HandleKHR(memory_handle_info);
            if (memory_handle == nullptr) throw std::runtime_error("Failed to export Vulkan memory Win32 handle for CUDA");
            memory_handle_desc.handle.win32.handle = memory_handle;
            CUDA_CHECK(cudaImportExternalMemory(&frame.cuda_external_memory, &memory_handle_desc));
            CloseHandle(memory_handle);
#else
            const vk::MemoryGetFdInfoKHR memory_handle_info{*frame.interop_memory, pbrt_external_memory_handle_type()};
            int memory_fd = device.getMemoryFdKHR(memory_handle_info);
            if (memory_fd < 0) throw std::runtime_error("Failed to export Vulkan memory FD for CUDA");
            memory_handle_desc.handle.fd = memory_fd;
            CUDA_CHECK(cudaImportExternalMemory(&frame.cuda_external_memory, &memory_handle_desc));
            close(memory_fd);
#endif

            cudaExternalMemoryBufferDesc buffer_desc{};
            buffer_desc.offset = 0;
            buffer_desc.size   = static_cast<unsigned long long>(frame.interop_buffer_size);
            CUDA_CHECK(cudaExternalMemoryGetMappedBuffer(reinterpret_cast<void**>(&frame.cuda_pixels), frame.cuda_external_memory, &buffer_desc));
            if (frame.cuda_pixels == nullptr) throw std::runtime_error("CUDA external memory mapped to a null PBRT RGBA pointer");
        }

        void create_cuda_complete_semaphore(const vk::raii::Device& device, FrameResource& frame) {
            const vk::ExportSemaphoreCreateInfo export_semaphore_info{pbrt_external_semaphore_handle_type()};
            const vk::SemaphoreCreateInfo semaphore_create_info{{}, &export_semaphore_info};
            frame.cuda_complete_semaphore = vk::raii::Semaphore{device, semaphore_create_info};

            cudaExternalSemaphoreHandleDesc semaphore_handle_desc{};
            semaphore_handle_desc.type = pbrt_cuda_external_semaphore_handle_type();
#if defined(_WIN32)
            const vk::SemaphoreGetWin32HandleInfoKHR semaphore_handle_info{*frame.cuda_complete_semaphore, pbrt_external_semaphore_handle_type()};
            HANDLE semaphore_handle = device.getSemaphoreWin32HandleKHR(semaphore_handle_info);
            if (semaphore_handle == nullptr) throw std::runtime_error("Failed to export Vulkan semaphore Win32 handle for CUDA");
            semaphore_handle_desc.handle.win32.handle = semaphore_handle;
            CUDA_CHECK(cudaImportExternalSemaphore(&frame.cuda_external_semaphore, &semaphore_handle_desc));
            CloseHandle(semaphore_handle);
#else
            const vk::SemaphoreGetFdInfoKHR semaphore_handle_info{*frame.cuda_complete_semaphore, pbrt_external_semaphore_handle_type()};
            int semaphore_fd = device.getSemaphoreFdKHR(semaphore_handle_info);
            if (semaphore_fd < 0) throw std::runtime_error("Failed to export Vulkan semaphore FD for CUDA");
            semaphore_handle_desc.handle.fd = semaphore_fd;
            CUDA_CHECK(cudaImportExternalSemaphore(&frame.cuda_external_semaphore, &semaphore_handle_desc));
            close(semaphore_fd);
#endif
            if (frame.cuda_external_semaphore == nullptr) throw std::runtime_error("CUDA external semaphore import returned null");
        }

        void create_display_image(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, FrameResource& frame, const vk::Format display_format) {
            const vk::ImageCreateInfo image_create_info{
                {},
                vk::ImageType::e2D,
                display_format,
                vk::Extent3D{static_cast<std::uint32_t>(this->resolution.x), static_cast<std::uint32_t>(this->resolution.y), 1},
                1,
                1,
                vk::SampleCountFlagBits::e1,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                vk::SharingMode::eExclusive,
                0,
                nullptr,
                vk::ImageLayout::eUndefined,
            };
            frame.image = vk::raii::Image{device, image_create_info};

            const vk::MemoryRequirements memory_requirements = frame.image.getMemoryRequirements();
            const std::uint32_t memory_type                  = find_memory_type_index(physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, memory_type};
            frame.image_memory = vk::raii::DeviceMemory{device, allocate_info};
            frame.image.bindMemory(*frame.image_memory, 0);

            const vk::ImageViewCreateInfo image_view_create_info{
                {},
                *frame.image,
                vk::ImageViewType::e2D,
                display_format,
                {},
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            frame.image_view = vk::raii::ImageView{device, image_view_create_info};

            const vk::SamplerCreateInfo sampler_create_info{
                {},
                vk::Filter::eNearest,
                vk::Filter::eNearest,
                vk::SamplerMipmapMode::eNearest,
                vk::SamplerAddressMode::eClampToEdge,
                vk::SamplerAddressMode::eClampToEdge,
                vk::SamplerAddressMode::eClampToEdge,
                0.0f,
                VK_FALSE,
                1.0f,
                VK_FALSE,
                vk::CompareOp::eNever,
                0.0f,
                0.0f,
                vk::BorderColor::eFloatOpaqueBlack,
                VK_FALSE,
            };
            frame.sampler = vk::raii::Sampler{device, sampler_create_info};
        }
    };

    Spectra::Spectra(const std::string_view& app_name, const std::string_view& engine_name, const std::uint32_t window_width, const std::uint32_t window_height) try {
        if (!glfwInit()) throw std::runtime_error("Failed to initialize GLFW");
        this->surface.glfw_initialized = true;
        const std::string app_name_string{app_name};
        const std::string engine_name_string{engine_name};
        this->window_title.base = app_name_string;

        constexpr std::array<const char*, 1> enabled_instance_layers{"VK_LAYER_KHRONOS_validation"};
        std::vector<const char*> enabled_device_extensions{vk::KHRSwapchainExtensionName, vk::KHRExternalMemoryExtensionName, vk::KHRExternalSemaphoreExtensionName};
#if defined(_WIN32)
        enabled_device_extensions.push_back(vk::KHRExternalMemoryWin32ExtensionName);
        enabled_device_extensions.push_back(vk::KHRExternalSemaphoreWin32ExtensionName);
#else
        enabled_device_extensions.push_back(vk::KHRExternalMemoryFdExtensionName);
        enabled_device_extensions.push_back(vk::KHRExternalSemaphoreFdExtensionName);
#endif
        std::vector<const char*> enabled_instance_extensions{};

        {
            std::uint32_t glfw_extension_count = 0;
            const char** glfw_extensions       = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
            if (glfw_extensions == nullptr) throw std::runtime_error("Failed to get GLFW Vulkan instance extensions");
            enabled_instance_extensions = {glfw_extensions, glfw_extensions + glfw_extension_count};
            enabled_instance_extensions.push_back(vk::EXTDebugUtilsExtensionName);

            const std::vector<vk::LayerProperties> available_layers = this->context.context.enumerateInstanceLayerProperties();
            for (const char* required_layer : enabled_instance_layers) {
                if (const auto found = std::ranges::find(available_layers, std::string_view{required_layer}, [](const vk::LayerProperties& layer) { return std::string_view{layer.layerName.data()}; }); found == available_layers.end()) throw std::runtime_error(std::string{"Required Vulkan layer not supported: "} + required_layer);
            }
            const std::vector<vk::ExtensionProperties> available_extensions = this->context.context.enumerateInstanceExtensionProperties();
            for (const char* required_extension : enabled_instance_extensions) {
                if (const auto found = std::ranges::find(available_extensions, std::string_view{required_extension}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); found == available_extensions.end()) throw std::runtime_error(std::string{"Required Vulkan instance extension not supported: "} + required_extension);
            }

            const vk::ApplicationInfo application_info{app_name_string.c_str(), VK_MAKE_VERSION(1, 0, 0), engine_name_string.c_str(), VK_MAKE_VERSION(1, 0, 0), vk::ApiVersion14};
            const vk::InstanceCreateInfo instance_create_info{{}, &application_info, static_cast<std::uint32_t>(enabled_instance_layers.size()), enabled_instance_layers.data(), static_cast<std::uint32_t>(enabled_instance_extensions.size()), enabled_instance_extensions.data()};
            this->context.instance = vk::raii::Instance{this->context.context, instance_create_info};
        }
        {
            constexpr vk::DebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{
                {},
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
                vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
                &debug_callback,
            };
            this->context.debug_messenger = this->context.instance.createDebugUtilsMessengerEXT(debug_messenger_create_info);
        }
        {
            if (window_width == 0 || window_height == 0 || window_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || window_height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Invalid GLFW window resolution");
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
            this->surface.window = std::shared_ptr<GLFWwindow>{glfwCreateWindow(static_cast<int>(window_width), static_cast<int>(window_height), app_name_string.c_str(), nullptr, nullptr), [](GLFWwindow* window) { glfwDestroyWindow(window); }};
            if (this->surface.window == nullptr) throw std::runtime_error("Failed to create GLFW window");
            glfwSetWindowUserPointer(this->surface.window.get(), this);
            glfwSetFramebufferSizeCallback(this->surface.window.get(), [](GLFWwindow* window, int, int) { static_cast<Spectra*>(glfwGetWindowUserPointer(window))->surface.resize_requested = true; });
        }
        {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (glfwCreateWindowSurface(*this->context.instance, this->surface.window.get(), nullptr, &surface) != VK_SUCCESS) throw std::runtime_error("Failed to create Vulkan surface");
            this->surface.surface = vk::raii::SurfaceKHR{this->context.instance, surface};
        }
        {
            int width  = 0;
            int height = 0;
            glfwGetFramebufferSize(this->surface.window.get(), &width, &height);
            if (width <= 0 || height <= 0) throw std::runtime_error("Invalid GLFW framebuffer size");
            this->surface.extent = vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        }
        {
            bool selected = false;
            for (const vk::raii::PhysicalDevice& physical_device : this->context.instance.enumeratePhysicalDevices()) {
                if (physical_device.getProperties().apiVersion < VK_API_VERSION_1_4) continue;

                const std::vector<vk::ExtensionProperties> available_extensions = physical_device.enumerateDeviceExtensionProperties();
                bool required_extensions_available                              = true;
                for (const char* required_extension : enabled_device_extensions) {
                    if (const auto found = std::ranges::find(available_extensions, std::string_view{required_extension}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); found == available_extensions.end()) required_extensions_available = false;
                }
                if (!required_extensions_available) continue;

                const std::vector<vk::QueueFamilyProperties> queue_families = physical_device.getQueueFamilyProperties();
                for (std::uint32_t queue_family_index = 0; queue_family_index < queue_families.size(); ++queue_family_index) {
                    if (!static_cast<bool>(queue_families[queue_family_index].queueFlags & vk::QueueFlagBits::eGraphics)) continue;
                    if (!physical_device.getSurfaceSupportKHR(queue_family_index, this->surface.surface)) continue;
                    this->context.physical_device      = physical_device;
                    this->context.graphics_queue_index = queue_family_index;
                    selected                           = true;
                    break;
                }
                if (selected) break;
            }
            if (!selected) throw std::runtime_error("Failed to find a Vulkan 1.4 physical device with swapchain, external memory, external semaphore, and graphics-present queue support");
        }
        {
            const auto supported_features = this->context.physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2) throw std::runtime_error("Device does not support synchronization2");
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering) throw std::runtime_error("Device does not support dynamicRendering");

            vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features> enabled_features{{}, {}};
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = VK_TRUE;

            constexpr std::array queue_priorities{1.0f};
            const vk::DeviceQueueCreateInfo queue_create_info{{}, this->context.graphics_queue_index, 1, queue_priorities.data()};
            const vk::DeviceCreateInfo device_create_info{{}, 1, &queue_create_info, 0, nullptr, static_cast<std::uint32_t>(enabled_device_extensions.size()), enabled_device_extensions.data(), nullptr, &enabled_features.get<vk::PhysicalDeviceFeatures2>()};
            this->context.device         = vk::raii::Device{this->context.physical_device, device_create_info};
            this->context.graphics_queue = vk::raii::Queue{this->context.device, this->context.graphics_queue_index, 0};
        }
        {
            const vk::CommandPoolCreateInfo command_pool_create_info{vk::CommandPoolCreateFlagBits::eResetCommandBuffer, this->context.graphics_queue_index};
            this->context.command_pool = vk::raii::CommandPool{this->context.device, command_pool_create_info};
        }
        this->create_swapchain();
        {
            constexpr vk::SemaphoreCreateInfo semaphore_create_info{};
            constexpr vk::FenceCreateInfo fence_create_info{vk::FenceCreateFlagBits::eSignaled};
            const vk::CommandBufferAllocateInfo command_buffer_allocate_info{*this->context.command_pool, vk::CommandBufferLevel::ePrimary, this->sync.frame_count};
            this->sync.command_buffers = vk::raii::CommandBuffers{this->context.device, command_buffer_allocate_info};
            if (this->sync.command_buffers.size() != this->sync.frame_count) throw std::runtime_error("Failed to allocate per-frame command buffers");

            this->sync.image_available_semaphores.reserve(this->sync.frame_count);
            this->sync.in_flight_fences.reserve(this->sync.frame_count);
            for (std::uint32_t frame_index = 0; frame_index < this->sync.frame_count; ++frame_index) {
                this->sync.image_available_semaphores.emplace_back(this->context.device, semaphore_create_info);
                this->sync.in_flight_fences.emplace_back(this->context.device, fence_create_info);
            }
        }
        this->create_imgui();

        const auto properties_chain = this->context.physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();
        const vk::PhysicalDeviceProperties& properties = properties_chain.get<vk::PhysicalDeviceProperties2>().properties;
        const vk::PhysicalDeviceDriverProperties& driver = properties_chain.get<vk::PhysicalDeviceDriverProperties>();
        std::println("Spectra Vulkan device: {} | Vulkan {}.{}.{} | Driver {} ({})", properties.deviceName.data(), vk::apiVersionMajor(properties.apiVersion), vk::apiVersionMinor(properties.apiVersion), vk::apiVersionPatch(properties.apiVersion), driver.driverName.data(), vk::to_string(driver.driverID));
        std::println("Spectra swapchain: {} {}x{} images {} present {}", vk::to_string(this->swapchain.format), this->swapchain.extent.width, this->swapchain.extent.height, this->swapchain.images.size(), vk::to_string(this->swapchain.present_mode));
    } catch (...) {
        this->destroy_imgui();
        if (this->surface.glfw_initialized) glfwTerminate();
        throw;
    }

    Spectra::~Spectra() noexcept {
        try {
            if (*this->context.device) this->context.device.waitIdle();
        } catch (...) {
        }

        this->pbrt_interactive.reset();
        this->unload_pbrt_scene_noexcept();
        this->destroy_imgui();
        this->sync.command_buffers.clear();
        this->sync.in_flight_fences.clear();
        this->sync.image_in_flight_frame.clear();
        this->sync.render_finished_semaphores.clear();
        this->sync.image_available_semaphores.clear();
        this->context.command_pool = nullptr;
        this->swapchain.image_views.clear();
        this->swapchain.handle = nullptr;
        this->swapchain.image_layouts.clear();
        this->swapchain.images.clear();
        this->context.graphics_queue  = nullptr;
        this->context.device          = nullptr;
        this->surface.surface         = nullptr;
        this->surface.window          = nullptr;
        this->context.physical_device = nullptr;
        this->context.debug_messenger = nullptr;
        this->context.instance        = nullptr;
        if (this->surface.glfw_initialized) glfwTerminate();
        this->surface.glfw_initialized = false;
    }

    void Spectra::create_imgui() {
        if (this->imgui.initialized) throw std::runtime_error("ImGui is already initialized");
        if (this->surface.window.get() == nullptr) throw std::runtime_error("Cannot initialize ImGui without a GLFW window");
        if (this->swapchain.images.empty()) throw std::runtime_error("Cannot initialize ImGui without swapchain images");

        bool context_created            = false;
        bool glfw_backend_initialized   = false;
        bool vulkan_backend_initialized = false;
        try {
            constexpr std::array descriptor_pool_sizes{
                vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformTexelBuffer, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageTexelBuffer, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformBufferDynamic, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageBufferDynamic, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eInputAttachment, 1000},
            };
            const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1000u * static_cast<std::uint32_t>(descriptor_pool_sizes.size()), static_cast<std::uint32_t>(descriptor_pool_sizes.size()), descriptor_pool_sizes.data()};
            this->imgui.descriptor_pool = vk::raii::DescriptorPool{this->context.device, descriptor_pool_create_info};
            this->imgui.color_format    = this->swapchain.format;
            this->imgui.min_image_count = std::max(2u, this->sync.frame_count);
            this->imgui.image_count     = static_cast<std::uint32_t>(this->swapchain.images.size());
            if (this->imgui.image_count < this->imgui.min_image_count) throw std::runtime_error("ImGui image count is smaller than minimum image count");

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            context_created = true;

            ImGuiIO& io = ImGui::GetIO();
            if (this->imgui.docking) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            if (this->imgui.viewports) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            load_imgui_fonts();
            apply_imgui_style(this->imgui.viewports);

            if (!ImGui_ImplGlfw_InitForVulkan(this->surface.window.get(), true)) throw std::runtime_error("ImGui_ImplGlfw_InitForVulkan failed");
            glfw_backend_initialized = true;

            auto color_attachment_format = static_cast<VkFormat>(this->imgui.color_format);
            VkPipelineRenderingCreateInfoKHR pipeline_rendering_create_info{};
            pipeline_rendering_create_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
            pipeline_rendering_create_info.colorAttachmentCount    = 1;
            pipeline_rendering_create_info.pColorAttachmentFormats = &color_attachment_format;

            ImGui_ImplVulkan_InitInfo init_info{};
            init_info.ApiVersion                                   = VK_API_VERSION_1_4;
            init_info.Instance                                     = static_cast<VkInstance>(*this->context.instance);
            init_info.PhysicalDevice                               = static_cast<VkPhysicalDevice>(*this->context.physical_device);
            init_info.Device                                       = static_cast<VkDevice>(*this->context.device);
            init_info.QueueFamily                                  = this->context.graphics_queue_index;
            init_info.Queue                                        = static_cast<VkQueue>(*this->context.graphics_queue);
            init_info.DescriptorPool                               = static_cast<VkDescriptorPool>(*this->imgui.descriptor_pool);
            init_info.MinImageCount                                = this->imgui.min_image_count;
            init_info.ImageCount                                   = this->imgui.image_count;
            init_info.PipelineInfoMain.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
            init_info.UseDynamicRendering                          = true;
            init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering_create_info;
            if (!ImGui_ImplVulkan_Init(&init_info)) throw std::runtime_error("ImGui_ImplVulkan_Init failed");
            vulkan_backend_initialized = true;
            this->imgui.initialized    = true;
        } catch (...) {
            if (vulkan_backend_initialized) ImGui_ImplVulkan_Shutdown();
            if (glfw_backend_initialized) ImGui_ImplGlfw_Shutdown();
            if (context_created) ImGui::DestroyContext();
            this->imgui.descriptor_pool = nullptr;
            this->imgui.color_format    = vk::Format::eUndefined;
            this->imgui.min_image_count = 2;
            this->imgui.image_count     = 2;
            this->imgui.initialized     = false;
            throw;
        }
    }

    void Spectra::destroy_imgui() noexcept {
        if (this->pbrt_interactive != nullptr) this->pbrt_interactive->release_imgui_descriptors();
        if (this->imgui.initialized) {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
        this->imgui.descriptor_pool = nullptr;
        this->imgui.color_format    = vk::Format::eUndefined;
        this->imgui.min_image_count = 2;
        this->imgui.image_count     = 2;
        this->imgui.initialized     = false;
        this->ui.dock_layout_initialized = false;
    }

    void Spectra::load_pbrt_scene(const std::filesystem::path& scene_path) {
        if (this->pbrt_scene != nullptr) throw std::runtime_error("PBRT shared scene is already loaded");
        std::unique_ptr<SpectraPbrtScene> loaded_scene = std::make_unique<SpectraPbrtScene>();
        try {
            loaded_scene->load(scene_path);
            this->pbrt_scene = std::move(loaded_scene);
        } catch (...) {
            loaded_scene->unload_noexcept();
            throw;
        }
    }

    void Spectra::unload_pbrt_scene_noexcept() noexcept {
        if (this->pbrt_scene == nullptr) return;
        this->pbrt_scene->unload_noexcept();
        this->pbrt_scene.reset();
    }

    void Spectra::render_pbrt_interactive(const std::filesystem::path& scene_path) {
        if (this->pbrt_scene != nullptr) throw std::runtime_error("PBRT shared scene is already active");
        if (this->pbrt_interactive != nullptr) throw std::runtime_error("PBRT interactive session is already active");
        this->session.mode_label       = "PBRT GPU Interactive";
        this->session.status           = "Initializing";
        this->session.message          = scene_path.filename().string();
        try {
            this->load_pbrt_scene(scene_path);
            this->pbrt_interactive = std::make_unique<SpectraPbrtInteractiveSession>(scene_path, this->context.physical_device, this->context.device, this->sync.frame_count);
            this->pbrt_scene->set_runtime_metadata(this->pbrt_interactive->film_resolution(), this->pbrt_interactive->sampler_sample_count(), this->pbrt_interactive->camera_from_world_matrix());
            this->initialize_camera_state();
            this->session.status  = "Rendering";
            this->session.message = this->pbrt_scene->scene_label;
            this->render_loop();
            this->context.device.waitIdle();
            this->pbrt_interactive.reset();
            this->unload_pbrt_scene_noexcept();
            this->session.mode_label = "Idle";
            this->session.status     = "Idle";
        } catch (...) {
            try {
                if (*this->context.device) this->context.device.waitIdle();
            } catch (...) {
            }
            this->pbrt_interactive.reset();
            this->unload_pbrt_scene_noexcept();
            this->session.mode_label = "Idle";
            this->session.status     = "Failed";
            throw;
        }
    }

    void Spectra::render_loop() {
        if (this->pbrt_scene == nullptr) throw std::runtime_error("Cannot enter Spectra render loop without an active PBRT scene");
        while (!glfwWindowShouldClose(this->surface.window.get())) {
            FrameState frame{};
            if (!this->begin_frame(frame)) continue;
            this->record_frame(frame);
            this->end_frame(frame);
        }
        this->context.device.waitIdle();
    }

    void Spectra::update_window_title(const float delta_seconds) {
        if (this->surface.window == nullptr) throw std::runtime_error("Cannot update window title without a GLFW window");

        ++this->window_title.frame_count;
        this->window_title.refresh_timer += delta_seconds;
        if (this->window_title.refresh_timer <= 1.0f) return;

        const ImGuiIO& io = ImGui::GetIO();
        if (io.Framerate <= 0.0f) return;

        std::uint32_t width  = this->swapchain.extent.width;
        std::uint32_t height = this->swapchain.extent.height;
        if (this->ui.viewport_known && this->ui.viewport_size[0] > 0.0f && this->ui.viewport_size[1] > 0.0f) {
            width  = static_cast<std::uint32_t>(std::max(1.0f, std::round(this->ui.viewport_size[0])));
            height = static_cast<std::uint32_t>(std::max(1.0f, std::round(this->ui.viewport_size[1])));
        }

        const std::string scene_label = this->pbrt_scene == nullptr ? "No Scene" : this->pbrt_scene->scene_label;
        const std::array<int, 2> sample_range = this->pbrt_scene == nullptr ? std::array<int, 2>{0, 0} : this->active_renderer_sample_range();
        const std::string title       = std::format("{} - {} | {} | {}x{} | sample {}/{} | {:.0f} FPS / {:.3f}ms | frame {}", this->window_title.base, scene_label, this->session.mode_label, width, height, sample_range[0], sample_range[1], io.Framerate, 1000.0f / io.Framerate, this->window_title.frame_count);
        glfwSetWindowTitle(this->surface.window.get(), title.c_str());
        this->window_title.refresh_timer = 0.0f;
    }

    void Spectra::clear_pathtracer_throughput_statistics() {
        this->statistics.throughput_mspp.clear();
        this->statistics.last_valid_throughput_mspp = 0.0f;
        this->statistics.has_throughput             = false;
    }

    void Spectra::update_frame_statistics(const FrameState& frame, const bool rendered_sample, const bool reset_accumulation, const std::uint64_t sample_pixels) {
        const ImGuiIO& io = ImGui::GetIO();
        if (!std::isfinite(io.DeltaTime) || !(io.DeltaTime > 0.0f)) throw std::runtime_error("ImGui frame delta time must be finite and positive for statistics");
        if (!rendered_sample && sample_pixels != 0) throw std::runtime_error("PBRT frame statistics reported sample-pixels without rendering a sample");
        if (rendered_sample && sample_pixels == 0) throw std::runtime_error("PBRT frame statistics rendered a sample without sample-pixels");

        const float frame_milliseconds = io.DeltaTime * 1000.0f;
        this->statistics.current_frame_id             = this->window_title.frame_count + 1;
        this->statistics.active_frame_index           = frame.frame_index;
        this->statistics.active_swapchain_image_index = frame.image_index;
        this->statistics.last_frame_milliseconds      = frame_milliseconds;
        this->statistics.last_frame_rendered_sample   = rendered_sample;
        this->statistics.frame_milliseconds.add(frame_milliseconds);

        if (reset_accumulation) this->clear_pathtracer_throughput_statistics();
        if (rendered_sample) {
            const float throughput = (static_cast<float>(sample_pixels) / 1000000.0f) / io.DeltaTime;
            this->statistics.throughput_mspp.add(throughput);
            this->statistics.last_valid_throughput_mspp = throughput;
            this->statistics.has_throughput             = true;
        }
    }

    [[nodiscard]] const char* Spectra::active_renderer_label() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                return "PBRT Pathtracer";
            case SpectraRenderMode::VulkanRasterizer:
                return "Vulkan Rasterizer";
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] VkDescriptorSet Spectra::active_viewport_descriptor() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("PBRT pathtracer viewport descriptor requested without an active PBRT session");
                return this->pbrt_interactive->active_descriptor();
            case SpectraRenderMode::VulkanRasterizer:
                throw std::runtime_error("Vulkan rasterizer viewport output is not implemented; first rasterizer version must consume a PBRT-derived Vulkan raster scene, not external scene formats");
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] std::array<int, 2> Spectra::active_renderer_sample_range() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) return {0, 0};
                return {this->pbrt_interactive->current_sample(), this->pbrt_interactive->target_sample_count()};
            case SpectraRenderMode::VulkanRasterizer:
                throw std::runtime_error("Vulkan rasterizer sampling state is not implemented; first rasterizer version must consume a PBRT-derived Vulkan raster scene, not external scene formats");
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] float Spectra::active_renderer_initial_move_scale() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("PBRT camera move scale requested without an active PBRT session");
                return this->pbrt_interactive->camera_initial_move_scale();
            case SpectraRenderMode::VulkanRasterizer:
                throw std::runtime_error("Vulkan rasterizer camera scale is not implemented; first rasterizer version must consume a PBRT-derived Vulkan raster scene, not external scene formats");
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] bool Spectra::active_renderer_uses_external_completion_semaphore() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("PBRT completion semaphore requested without an active PBRT session");
                return true;
            case SpectraRenderMode::VulkanRasterizer:
                throw std::runtime_error("Vulkan rasterizer completion semaphore path is not implemented; first rasterizer version must consume a PBRT-derived Vulkan raster scene, not external scene formats");
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] vk::Semaphore Spectra::active_renderer_complete_semaphore() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("PBRT completion semaphore requested without an active PBRT session");
                return this->pbrt_interactive->active_cuda_complete_semaphore();
            case SpectraRenderMode::VulkanRasterizer:
                throw std::runtime_error("Vulkan rasterizer completion semaphore is not implemented; first rasterizer version must consume a PBRT-derived Vulkan raster scene, not external scene formats");
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    void Spectra::process_active_renderer_input() {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("Cannot process PBRT pathtracer input without an active PBRT session");
                this->pbrt_interactive->process_pathtracer_input(this->camera.input_enabled);
                return;
            case SpectraRenderMode::VulkanRasterizer:
                throw std::runtime_error("Vulkan rasterizer input is not implemented; first rasterizer version must consume a PBRT-derived Vulkan raster scene, not external scene formats");
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] Spectra::ActiveRendererFrameResult Spectra::render_active_renderer_frame(const FrameState& frame) {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer: {
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("Cannot render PBRT pathtracer without an active PBRT session");
                const pbrt::Transform moving_from_camera = transform_from_matrix_array(this->camera.moving_from_camera);
                const SpectraPbrtInteractiveSession::RenderFrameResult render_result = this->pbrt_interactive->render_frame(frame.frame_index, moving_from_camera);
                return {render_result.sample_pixels, render_result.rendered_sample, render_result.reset_accumulation};
            }
            case SpectraRenderMode::VulkanRasterizer:
                throw std::runtime_error("Vulkan rasterizer render step is not implemented; first rasterizer version must consume a PBRT-derived Vulkan raster scene, not external scene formats");
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    void Spectra::record_active_renderer_output(const vk::raii::CommandBuffer& command_buffer) {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("Cannot record PBRT pathtracer output without an active PBRT session");
                this->pbrt_interactive->record_copy(command_buffer);
                return;
            case SpectraRenderMode::VulkanRasterizer:
                throw std::runtime_error("Vulkan rasterizer output recording is not implemented; first rasterizer version must consume a PBRT-derived Vulkan raster scene, not external scene formats");
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    void Spectra::reset_active_renderer_accumulation() {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("Cannot reset PBRT accumulation without an active PBRT session");
                this->pbrt_interactive->request_reset_accumulation();
                this->clear_pathtracer_throughput_statistics();
                return;
            case SpectraRenderMode::VulkanRasterizer:
                throw std::runtime_error("Vulkan rasterizer reset is not implemented; first rasterizer version must consume a PBRT-derived Vulkan raster scene, not external scene formats");
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    void Spectra::initialize_camera_state() {
        if (this->pbrt_scene == nullptr) throw std::runtime_error("Cannot initialize camera state without an active PBRT scene");
        this->camera.initialized        = true;
        this->camera.input_enabled      = false;
        this->camera.changed_this_frame = false;
        this->camera.move_scale         = this->active_renderer_initial_move_scale();
        this->camera.moving_from_camera = identity_matrix_array();
        this->camera.camera_from_world  = this->pbrt_scene->camera_from_world;
    }

    void Spectra::set_camera_move_scale(const float move_scale) {
        if (!std::isfinite(move_scale) || !(move_scale > 0.0f)) throw std::runtime_error("Camera move scale must be finite and positive");
        this->camera.move_scale = move_scale;
    }

    void Spectra::reset_camera() {
        if (!this->camera.initialized) throw std::runtime_error("Cannot reset camera before camera state is initialized");
        this->camera.moving_from_camera = identity_matrix_array();
        this->camera.changed_this_frame = true;
        this->reset_active_renderer_accumulation();
    }

    [[nodiscard]] std::string Spectra::camera_transform_text() const {
        if (!this->camera.initialized) throw std::runtime_error("Cannot format camera transform before camera state is initialized");
        const pbrt::Transform moving_from_camera = transform_from_matrix_array(this->camera.moving_from_camera);
        const pbrt::Transform camera_from_world  = transform_from_matrix_array(this->camera.camera_from_world);
        return pbrt_transform_text(pbrt::Inverse(moving_from_camera) * camera_from_world);
    }

    void Spectra::copy_camera_transform() {
        const std::string text = this->camera_transform_text();
        ImGui::SetClipboardText(text.c_str());
    }

    void Spectra::print_camera_transform() {
        const std::string text = this->camera_transform_text();
        pbrt::Printf("Current camera transform:\n%s\n", text.c_str());
        std::fflush(stdout);
    }

    void Spectra::process_camera_input(GLFWwindow* window) {
        if (window == nullptr) throw std::runtime_error("Cannot process camera input without a GLFW window");
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) glfwSetWindowShouldClose(window, GLFW_TRUE);

        const ImVec2 mouse_position = io.MousePos;
        const bool in_viewport_rect = this->ui.viewport_known && mouse_position.x >= this->ui.viewport_position[0] && mouse_position.x < this->ui.viewport_position[0] + this->ui.viewport_size[0] && mouse_position.y >= this->ui.viewport_position[1] && mouse_position.y < this->ui.viewport_position[1] + this->ui.viewport_size[1];
        this->camera.input_enabled  = in_viewport_rect && (this->ui.viewport_hovered || this->ui.viewport_focused) && !io.WantTextInput;
        this->camera.changed_this_frame = false;
        if (!this->camera.input_enabled) return;
        if (!this->camera.initialized) throw std::runtime_error("Cannot process camera input before camera state is initialized");

        pbrt::Transform moving_from_camera = transform_from_matrix_array(this->camera.moving_from_camera);
        bool needs_reset                   = false;
        if (ImGui::IsKeyDown(ImGuiKey_A)) {
            moving_from_camera = moving_from_camera * pbrt::Translate(pbrt::Vector3f(-this->camera.move_scale, 0.0f, 0.0f));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_D)) {
            moving_from_camera = moving_from_camera * pbrt::Translate(pbrt::Vector3f(this->camera.move_scale, 0.0f, 0.0f));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_S)) {
            moving_from_camera = moving_from_camera * pbrt::Translate(pbrt::Vector3f(0.0f, 0.0f, -this->camera.move_scale));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_W)) {
            moving_from_camera = moving_from_camera * pbrt::Translate(pbrt::Vector3f(0.0f, 0.0f, this->camera.move_scale));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) {
            moving_from_camera = moving_from_camera * pbrt::Translate(pbrt::Vector3f(0.0f, -this->camera.move_scale, 0.0f));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_E)) {
            moving_from_camera = moving_from_camera * pbrt::Translate(pbrt::Vector3f(0.0f, this->camera.move_scale, 0.0f));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
            moving_from_camera = moving_from_camera * pbrt::Rotate(-0.5f, pbrt::Vector3f(0.0f, 1.0f, 0.0f));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
            moving_from_camera = moving_from_camera * pbrt::Rotate(0.5f, pbrt::Vector3f(0.0f, 1.0f, 0.0f));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
            moving_from_camera = moving_from_camera * pbrt::Rotate(-0.5f, pbrt::Vector3f(1.0f, 0.0f, 0.0f));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
            moving_from_camera = moving_from_camera * pbrt::Rotate(0.5f, pbrt::Vector3f(1.0f, 0.0f, 0.0f));
            needs_reset        = true;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
            if (io.MouseDelta.x < 0.0f) moving_from_camera = moving_from_camera * pbrt::Rotate(-1.0f, pbrt::Vector3f(0.0f, 1.0f, 0.0f));
            if (io.MouseDelta.x > 0.0f) moving_from_camera = moving_from_camera * pbrt::Rotate(1.0f, pbrt::Vector3f(0.0f, 1.0f, 0.0f));
            if (io.MouseDelta.y > 0.0f) moving_from_camera = moving_from_camera * pbrt::Rotate(-1.0f, pbrt::Vector3f(1.0f, 0.0f, 0.0f));
            if (io.MouseDelta.y < 0.0f) moving_from_camera = moving_from_camera * pbrt::Rotate(1.0f, pbrt::Vector3f(1.0f, 0.0f, 0.0f));
            needs_reset = needs_reset || io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            moving_from_camera = pbrt::Transform{};
            needs_reset        = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Equal, false)) this->set_camera_move_scale(this->camera.move_scale * 2.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_Minus, false)) this->set_camera_move_scale(this->camera.move_scale * 0.5f);

        if (needs_reset) {
            this->camera.moving_from_camera = matrix_array_from_transform(moving_from_camera);
            this->camera.changed_this_frame = true;
            this->reset_active_renderer_accumulation();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) this->print_camera_transform();
    }

    bool Spectra::begin_frame(FrameState& frame) {
        glfwPollEvents();
        if (this->surface.resize_requested) {
            this->recreate_swapchain();
            return false;
        }

        frame.recreate_after_present = false;
        frame.frame_index            = this->sync.frame_index;
        if (this->context.device.waitForFences(*this->sync.in_flight_fences[frame.frame_index], VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for frame fence");

        try {
            const vk::ResultValue<std::uint32_t> acquired_image = this->swapchain.handle.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *this->sync.image_available_semaphores[frame.frame_index], nullptr);
            if (acquired_image.result != vk::Result::eSuccess && acquired_image.result != vk::Result::eSuboptimalKHR) throw std::runtime_error(std::string{"Failed to acquire swapchain image: "} + vk::to_string(acquired_image.result));
            frame.recreate_after_present = acquired_image.result == vk::Result::eSuboptimalKHR;
            frame.image_index            = acquired_image.value;
        } catch (const vk::OutOfDateKHRError&) {
            this->recreate_swapchain();
            return false;
        }

        if (const std::uint32_t previous_frame_index = this->sync.image_in_flight_frame.at(frame.image_index); previous_frame_index != std::numeric_limits<std::uint32_t>::max()) {
            if (this->context.device.waitForFences(*this->sync.in_flight_fences.at(previous_frame_index), VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for swapchain image fence");
        }
        this->sync.image_in_flight_frame.at(frame.image_index) = frame.frame_index;
        this->context.device.resetFences(*this->sync.in_flight_fences[frame.frame_index]);
        if (!this->imgui.initialized) throw std::runtime_error("ImGui is not initialized");
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::GetMainViewport() == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        if (this->pbrt_scene == nullptr) throw std::runtime_error("Cannot update renderer frame without an active PBRT scene");
        this->process_camera_input(this->surface.window.get());
        this->process_active_renderer_input();
        const ActiveRendererFrameResult render_result = this->render_active_renderer_frame(frame);
        this->update_frame_statistics(frame, render_result.rendered_sample, render_result.reset_accumulation, render_result.sample_pixels);
        return true;
    }

    void Spectra::record_frame(const FrameState& frame) {
        this->draw_main_menu();
        this->draw_dockspace();
        this->draw_viewport_window();
        this->draw_camera_window();
        this->draw_settings_window();
        this->draw_environment_window();
        this->draw_tonemapper_window();
        this->draw_session_window();
        this->draw_statistics_window();

        const vk::raii::CommandBuffer& command_buffer = this->sync.command_buffers[frame.frame_index];
        command_buffer.reset();
        constexpr vk::CommandBufferBeginInfo command_buffer_begin_info{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        command_buffer.begin(command_buffer_begin_info);
        if (this->pbrt_scene != nullptr) this->record_active_renderer_output(command_buffer);

        {
            const vk::ImageMemoryBarrier2 color_barrier{
                vk::PipelineStageFlagBits2::eAllCommands,
                {},
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                this->swapchain.image_layouts[frame.image_index],
                vk::ImageLayout::eColorAttachmentOptimal,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                this->swapchain.images[frame.image_index],
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            const vk::DependencyInfo dependency_info{{}, 0, nullptr, 0, nullptr, 1, &color_barrier};
            command_buffer.pipelineBarrier2(dependency_info);
        }
        this->swapchain.image_layouts[frame.image_index] = vk::ImageLayout::eColorAttachmentOptimal;

        const std::array<float, 4> clear_color{this->ui.background_color[0], this->ui.background_color[1], this->ui.background_color[2], 1.0f};
        const vk::ClearValue color_clear_value{vk::ClearColorValue{clear_color}};
        const vk::RenderingAttachmentInfo color_attachment{
            *this->swapchain.image_views[frame.image_index],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            color_clear_value,
        };
        const vk::RenderingInfo clear_rendering_info{{}, {{0, 0}, this->swapchain.extent}, 1, 0, 1, &color_attachment, nullptr, nullptr};
        command_buffer.beginRendering(clear_rendering_info);
        command_buffer.endRendering();

        ImGui::Render();
        const vk::RenderingAttachmentInfo imgui_color_attachment{
            *this->swapchain.image_views[frame.image_index],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eLoad,
            vk::AttachmentStoreOp::eStore,
            {},
        };
        const vk::RenderingInfo imgui_rendering_info{{}, {{0, 0}, this->swapchain.extent}, 1, 0, 1, &imgui_color_attachment, nullptr, nullptr};
        command_buffer.beginRendering(imgui_rendering_info);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *command_buffer);
        command_buffer.endRendering();

        transition_image_layout(command_buffer, this->swapchain.images[frame.image_index], vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR, vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eAllCommands, {});
        this->swapchain.image_layouts[frame.image_index] = vk::ImageLayout::ePresentSrcKHR;
        command_buffer.end();
    }

    void Spectra::end_frame(FrameState& frame) {
        if (this->imgui.viewports) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        std::array<vk::SemaphoreSubmitInfo, 2> wait_semaphore_infos{
            vk::SemaphoreSubmitInfo{*this->sync.image_available_semaphores[frame.frame_index], 0, vk::PipelineStageFlagBits2::eAllCommands},
            vk::SemaphoreSubmitInfo{},
        };
        std::uint32_t wait_semaphore_count = 1;
        if (this->active_renderer_uses_external_completion_semaphore()) {
            wait_semaphore_infos[1] = vk::SemaphoreSubmitInfo{this->active_renderer_complete_semaphore(), 0, vk::PipelineStageFlagBits2::eTransfer};
            wait_semaphore_count    = 2;
        }
        const vk::CommandBufferSubmitInfo command_buffer_submit_info{*this->sync.command_buffers[frame.frame_index]};
        const vk::SemaphoreSubmitInfo signal_semaphore_info{*this->sync.render_finished_semaphores[frame.image_index], 0, vk::PipelineStageFlagBits2::eAllCommands};
        const vk::SubmitInfo2 submit_info{{}, wait_semaphore_count, wait_semaphore_infos.data(), 1, &command_buffer_submit_info, 1, &signal_semaphore_info};
        this->context.graphics_queue.submit2(submit_info, *this->sync.in_flight_fences[frame.frame_index]);

        const vk::Semaphore render_finished_semaphore = *this->sync.render_finished_semaphores[frame.image_index];
        const vk::SwapchainKHR swapchain              = *this->swapchain.handle;
        const vk::PresentInfoKHR present_info{1, &render_finished_semaphore, 1, &swapchain, &frame.image_index};
        bool frame_presented = true;
        try {
            if (const vk::Result present_result = this->context.graphics_queue.presentKHR(present_info); present_result == vk::Result::eSuboptimalKHR)
                frame.recreate_after_present = true;
            else if (present_result == vk::Result::eErrorSurfaceLostKHR) {
                frame.recreate_after_present = true;
                frame_presented              = false;
            } else if (present_result != vk::Result::eSuccess)
                throw std::runtime_error(std::string{"Failed to present swapchain image: "} + vk::to_string(present_result));
        } catch (const vk::OutOfDateKHRError&) {
            frame.recreate_after_present = true;
            frame_presented              = false;
        } catch (const vk::SystemError& error) {
            if (error.code().value() == static_cast<int>(vk::Result::eErrorOutOfDateKHR)) {
                frame.recreate_after_present = true;
                frame_presented              = false;
            } else if (error.code().value() == static_cast<int>(vk::Result::eSuboptimalKHR))
                frame.recreate_after_present = true;
            else if (error.code().value() == static_cast<int>(vk::Result::eErrorSurfaceLostKHR)) {
                frame.recreate_after_present = true;
                frame_presented              = false;
            } else
                throw;
        }
        if (frame.recreate_after_present) this->recreate_swapchain();
        if (frame_presented) this->update_window_title(ImGui::GetIO().DeltaTime);

        this->sync.frame_index = (this->sync.frame_index + 1) % this->sync.frame_count;
    }

    void Spectra::draw_main_menu() {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) this->ui.camera_visible = !this->ui.camera_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) this->ui.settings_visible = !this->ui.settings_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) this->ui.statistics_visible = !this->ui.statistics_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F4, false)) this->ui.environment_visible = !this->ui.environment_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) this->ui.tonemapper_visible = !this->ui.tonemapper_visible;
        }

        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem(ICON_MS_CLOSE " Exit", "Esc")) glfwSetWindowShouldClose(this->surface.window.get(), GLFW_TRUE);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem(ICON_MS_PHOTO_CAMERA " Camera", "F1", &this->ui.camera_visible);
            ImGui::MenuItem(ICON_MS_SETTINGS " Settings", "F2", &this->ui.settings_visible);
            ImGui::MenuItem(ICON_MS_ANALYTICS " Statistics", "F3", &this->ui.statistics_visible);
            ImGui::MenuItem(ICON_MS_PUBLIC " Environment", "F4", &this->ui.environment_visible);
            ImGui::MenuItem(ICON_MS_TONALITY " Tonemapper", "F5", &this->ui.tonemapper_visible);
            ImGui::MenuItem(ICON_MS_TUNE " PBRT Session", nullptr, &this->ui.session_visible);
            ImGui::EndMenu();
        }
        this->draw_menu_toolbar();
        ImGui::EndMainMenuBar();
    }

    void Spectra::draw_menu_toolbar() {
        struct ToggleButton {
            const char* icon;
            const char* shortcut;
            bool* visible;
            const char* tooltip;
        };

        const std::array<ToggleButton, 6> toggles{{
            {ICON_MS_PHOTO_CAMERA, "F1", &this->ui.camera_visible, "Camera"},
            {ICON_MS_SETTINGS, "F2", &this->ui.settings_visible, "Settings"},
            {ICON_MS_ANALYTICS, "F3", &this->ui.statistics_visible, "Statistics"},
            {ICON_MS_PUBLIC, "F4", &this->ui.environment_visible, "Environment"},
            {ICON_MS_TONALITY, "F5", &this->ui.tonemapper_visible, "Tonemapper"},
            {ICON_MS_TUNE, nullptr, &this->ui.session_visible, "PBRT Session"},
        }};

        const float button_size  = ImGui::GetFrameHeight();
        const float total_width  = 2.0f + static_cast<float>(toggles.size()) * button_size + static_cast<float>(toggles.size() + 1) * 2.0f;
        const float window_width = ImGui::GetWindowWidth();
        if (window_width <= total_width + 180.0f) return;

        ImGui::SameLine(window_width * 0.5f - total_width * 0.5f);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        for (const ToggleButton& toggle : toggles) {
            ImGui::PushStyleColor(ImGuiCol_Button, *toggle.visible ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_ChildBg]);
            if (ImGui::Button(toggle.icon, ImVec2{button_size, button_size})) *toggle.visible = !*toggle.visible;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && toggle.shortcut != nullptr) ImGui::SetTooltip("Toggle %s Window (%s)", toggle.tooltip, toggle.shortcut);
            if (ImGui::IsItemHovered() && toggle.shortcut == nullptr) ImGui::SetTooltip("Toggle %s Window", toggle.tooltip);
            ImGui::SameLine(0.0f, 2.0f);
        }
    }

    void Spectra::draw_dockspace() {
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        if (main_viewport == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        if (main_viewport->WorkSize.x <= 640.0f || main_viewport->WorkSize.y <= 360.0f) throw std::runtime_error("Viewport is too small for docked workspace");

        constexpr ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode;
        const ImVec4 dockspace_window_background     = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{dockspace_window_background.x, dockspace_window_background.y, dockspace_window_background.z, 0.0f});
        const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, main_viewport, dockspace_flags);
        ImGui::PopStyleColor();
        if (dockspace_id == 0) throw std::runtime_error("Failed to create Spectra dockspace");
        if (this->ui.dock_layout_initialized) return;

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace | dockspace_flags);
        ImGui::DockBuilderSetNodePos(dockspace_id, main_viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockspace_id, main_viewport->WorkSize);

        ImGuiID center_id = dockspace_id;
        ImGuiID right_id  = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Right, 0.23f, nullptr, &center_id);
        ImGuiID bottom_id = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Down, 0.22f, nullptr, &center_id);
        if (right_id == 0 || bottom_id == 0 || center_id == 0) throw std::runtime_error("Failed to build Spectra dock layout");

        ImGui::DockBuilderDockWindow("Viewport", center_id);
        ImGui::DockBuilderDockWindow("Camera", right_id);
        ImGui::DockBuilderDockWindow("Settings", right_id);
        ImGui::DockBuilderDockWindow("Environment", right_id);
        ImGui::DockBuilderDockWindow("Tonemapper", right_id);
        ImGui::DockBuilderDockWindow("PBRT Session", right_id);
        ImGui::DockBuilderDockWindow("Statistics", bottom_id);
        ImGuiDockNode* central_node = ImGui::DockBuilderGetCentralNode(dockspace_id);
        if (central_node == nullptr) throw std::runtime_error("Failed to find Spectra central dock node");
        central_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        ImGui::DockBuilderFinish(dockspace_id);
        this->ui.dock_layout_initialized = true;
    }

    void Spectra::draw_viewport_window() {
        constexpr ImGuiWindowFlags viewport_window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        if (ImGui::Begin("Viewport", nullptr, viewport_window_flags)) {
            const ImVec2 viewport_position = ImGui::GetCursorScreenPos();
            const ImVec2 viewport_size     = ImGui::GetContentRegionAvail();
            if (viewport_size.x <= 0.0f || viewport_size.y <= 0.0f) throw std::runtime_error("Viewport dock window has no drawable area");
            this->ui.viewport_known    = true;
            this->ui.viewport_position = {viewport_position.x, viewport_position.y};
            this->ui.viewport_size     = {viewport_size.x, viewport_size.y};
            this->ui.viewport_hovered  = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootWindow);
            this->ui.viewport_focused  = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow);
            if (this->pbrt_scene != nullptr) {
                const VkDescriptorSet descriptor = this->active_viewport_descriptor();
                if (descriptor == VK_NULL_HANDLE) throw std::runtime_error("Active renderer viewport descriptor is null");
                const ImTextureID texture_id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(descriptor));
                ImGui::Image(ImTextureRef{texture_id}, viewport_size, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f});
                ImGui::SetCursorScreenPos(viewport_position);
            }
            ImGui::InvisibleButton("ViewportInputSurface", viewport_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        } else {
            this->ui.viewport_known   = false;
            this->ui.viewport_hovered = false;
            this->ui.viewport_focused = false;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void Spectra::draw_camera_window() {
        if (!this->ui.camera_visible) return;
        if (!ImGui::Begin("Camera", &this->ui.camera_visible)) {
            ImGui::End();
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        if (ImGui::BeginTable("SpectraCameraControls", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Move Scale");
            ImGui::TableSetColumnIndex(1);
            float move_scale = this->camera.move_scale;
            const float drag_speed = std::max(std::abs(move_scale) * 0.01f, 0.000001f);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##CameraMoveScale", &move_scale, drag_speed, 0.0f, 0.0f, "%.6g")) this->set_camera_move_scale(move_scale);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera movement distance per key step. Changing this does not reset accumulation.");

            draw_statistics_row("Input", this->camera.input_enabled ? "Enabled" : "Disabled");
            draw_statistics_row("Viewport Known", this->ui.viewport_known ? "Yes" : "No");
            draw_statistics_row("Viewport Hovered", this->ui.viewport_hovered ? "Yes" : "No");
            draw_statistics_row("Viewport Focused", this->ui.viewport_focused ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Actions");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(!this->camera.initialized || this->pbrt_scene == nullptr);
            if (ImGui::Button(ICON_MS_RESTART_ALT)) this->reset_camera();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Camera");
            ImGui::SameLine();
            if (ImGui::Button(ICON_MS_CONTENT_COPY)) this->copy_camera_transform();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy Camera Transform");
            ImGui::SameLine();
            if (ImGui::Button(ICON_MS_PRINT)) this->print_camera_transform();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Print Camera Transform");
            ImGui::EndDisabled();

            draw_statistics_row("Changed This Frame", this->camera.changed_this_frame ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Controls");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled(ICON_MS_KEYBOARD);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("WASD/QE move\nArrow keys or left-drag rotate\n+ / - speed\nR reset camera\nC print camera transform");

            ImGui::EndTable();
        }
        ImGui::End();
    }

    void Spectra::draw_session_window() {
        if (!this->ui.session_visible) return;
        if (!ImGui::Begin("PBRT Session", &this->ui.session_visible)) {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted(this->session.mode_label.c_str());
        ImGui::Separator();
        if (this->pbrt_scene == nullptr) {
            ImGui::TextDisabled("No active PBRT scene");
            ImGui::End();
            return;
        }

        ImGui::Text("Status: %s", this->session.status.c_str());
        ImGui::Text("Message: %s", this->session.message.c_str());
        ImGui::TextWrapped("Scene: %s", this->pbrt_scene->scene_path_text.c_str());
        ImGui::End();
    }

    void Spectra::draw_settings_window() {
        if (!this->ui.settings_visible) return;
        if (!ImGui::Begin("Settings", &this->ui.settings_visible)) {
            ImGui::End();
            return;
        }
        if (ImGui::BeginTable("SpectraRendererSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Active Renderer");
            ImGui::TableSetColumnIndex(1);
            const char* active_renderer_label = this->active_renderer_label();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##ActiveRenderer", active_renderer_label)) {
                const bool pbrt_selected = this->ui.active_render_mode == SpectraRenderMode::PbrtPathtracer;
                if (ImGui::Selectable("PBRT Pathtracer", pbrt_selected)) this->ui.active_render_mode = SpectraRenderMode::PbrtPathtracer;
                if (pbrt_selected) ImGui::SetItemDefaultFocus();
                // Rasterizer v1 will consume a Vulkan raster scene derived from PBRT, never external scene formats.
                ImGui::BeginDisabled();
                const bool rasterizer_selected = this->ui.active_render_mode == SpectraRenderMode::VulkanRasterizer;
                ImGui::Selectable("Vulkan Rasterizer", rasterizer_selected);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Vulkan Rasterizer is reserved for the next milestone");
                ImGui::EndCombo();
            }
            ImGui::EndTable();
        }
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Path Tracer", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (this->pbrt_interactive == nullptr) {
                ImGui::TextDisabled("No active PBRT interactive session");
            } else if (ImGui::BeginTable("SpectraPathTracerSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("PBRT Sampler SPP");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", this->pbrt_scene->sampler_sample_count);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Current Sample");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d / %d", this->pbrt_interactive->current_sample(), this->pbrt_interactive->target_sample_count());

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Max Iterations");
                ImGui::TableSetColumnIndex(1);
                const int previous_target_sample_count = this->pbrt_interactive->target_sample_count();
                int target_sample_count                = previous_target_sample_count;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderInt("##MaxIterations", &target_sample_count, 1, this->pbrt_scene->sampler_sample_count)) {
                    this->pbrt_interactive->set_target_sample_count(target_sample_count);
                    if (target_sample_count != previous_target_sample_count) this->clear_pathtracer_throughput_statistics();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Interactive stop sample count. Changing it resets accumulation.");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Accumulation");
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button("Reset Accumulation")) {
                    this->reset_active_renderer_accumulation();
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Controls");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled(ICON_MS_KEYBOARD);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("WASD/QE move\nArrow keys or left-drag rotate\nB / Shift+B exposure\n+ / - speed\nR reset camera\nC print camera transform");

                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    void Spectra::draw_environment_window() {
        if (!this->ui.environment_visible) return;
        if (!ImGui::Begin("Environment", &this->ui.environment_visible)) {
            ImGui::End();
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg;
        if (ImGui::BeginTable("SpectraEnvironmentSettings", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Background Color");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::ColorEdit3("##BackgroundColor", this->ui.background_color.data(), ImGuiColorEditFlags_Float);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Vulkan viewport clear color. This does not change PBRT scene lighting.");

            ImGui::EndTable();
        }
        ImGui::End();
    }

    void Spectra::draw_tonemapper_window() {
        if (!this->ui.tonemapper_visible) return;
        if (!ImGui::Begin("Tonemapper", &this->ui.tonemapper_visible)) {
            ImGui::End();
            return;
        }
        if (this->pbrt_interactive == nullptr) {
            ImGui::TextDisabled("No active PBRT interactive session");
            ImGui::End();
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg;
        if (ImGui::BeginTable("SpectraTonemapperSettings", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Exposure");
            ImGui::TableSetColumnIndex(1);
            float exposure = this->pbrt_interactive->current_exposure();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##TonemapperExposure", &exposure, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) this->pbrt_interactive->set_exposure(exposure);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Viewport exposure multiplier. This does not reset accumulation.");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Actions");
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button(ICON_MS_RESTART_ALT " Reset Exposure")) this->pbrt_interactive->set_exposure(1.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset exposure to 1.0 without resetting accumulation.");

            ImGui::EndTable();
        }
        ImGui::End();
    }

    void Spectra::draw_statistics_window() {
        if (!this->ui.statistics_visible) return;
        if (!ImGui::Begin("Statistics", &this->ui.statistics_visible)) {
            ImGui::End();
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        const std::string viewport_resolution    = this->ui.viewport_known ? std::format("{:.0f} x {:.0f}", this->ui.viewport_size[0], this->ui.viewport_size[1]) : "Unknown";

        ImGui::SeparatorText("Runtime");
        if (ImGui::BeginTable("SpectraRuntimeStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Active Renderer", this->active_renderer_label());
            draw_statistics_row("Frame ID", std::format("{}", this->statistics.current_frame_id));
            draw_statistics_row("Frame Slot", std::format("{}", this->statistics.active_frame_index));
            draw_statistics_row("Swapchain Image", std::format("{}", this->statistics.active_swapchain_image_index));
            draw_statistics_row("Frames In Flight", std::format("{}", this->sync.frame_count));
            draw_statistics_row("Swapchain Resolution", std::format("{} x {}", this->swapchain.extent.width, this->swapchain.extent.height));
            draw_statistics_row("Viewport Resolution", viewport_resolution);
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Performance");
        if (ImGui::BeginTable("SpectraPerformanceStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Frame Time", std::format("{:.3f} ms", this->statistics.last_frame_milliseconds));
            if (this->statistics.frame_milliseconds.has_value()) {
                const float average_frame_milliseconds = this->statistics.frame_milliseconds.average();
                if (!(average_frame_milliseconds > 0.0f)) throw std::runtime_error("Average frame time must be positive after statistics are collected");
                draw_statistics_row("Frame Time Avg", std::format("{:.3f} ms over {} frames", average_frame_milliseconds, this->statistics.frame_milliseconds.count));
                draw_statistics_row("FPS Avg", std::format("{:.1f}", 1000.0f / average_frame_milliseconds));
            } else {
                draw_statistics_row("Frame Time Avg", "Collecting");
                draw_statistics_row("FPS Avg", "Collecting");
            }
            ImGui::EndTable();
        }

        if (this->pbrt_scene == nullptr) {
            ImGui::TextDisabled("No active PBRT scene");
            ImGui::End();
            return;
        }

        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                break;
            case SpectraRenderMode::VulkanRasterizer:
                throw std::runtime_error("Vulkan rasterizer statistics are not implemented; first rasterizer version must consume a PBRT-derived Vulkan raster scene, not external scene formats");
        }

        if (this->pbrt_interactive == nullptr) {
            ImGui::TextDisabled("No active PBRT interactive session");
            ImGui::End();
            return;
        }

        const std::array<int, 2> film_resolution = this->pbrt_scene->film_resolution;
        const int current_sample                 = this->pbrt_interactive->current_sample();
        const int target_sample                  = this->pbrt_interactive->target_sample_count();
        const float completion_ratio             = this->pbrt_interactive->completion_ratio();
        const float completion_percent           = completion_ratio * 100.0f;
        const bool sampling_completed            = current_sample >= target_sample;
        const std::string sampling_state         = sampling_completed ? "Completed" : "Sampling";

        ImGui::SeparatorText("Path Tracer");
        if (ImGui::BeginTable("SpectraPathTracerStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("State", sampling_state);
            draw_statistics_row("Sample", std::format("{} / {}", current_sample, target_sample));
            draw_statistics_row("Completion", std::format("{:.1f}%", completion_percent));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Progress");
            ImGui::TableSetColumnIndex(1);
            const std::string progress_label = std::format("{:.1f}%", completion_percent);
            ImGui::ProgressBar(completion_ratio, ImVec2{-1.0f, 0.0f}, progress_label.c_str());

            draw_statistics_row("Film Resolution", std::format("{} x {}", film_resolution[0], film_resolution[1]));
            if (this->statistics.throughput_mspp.has_value())
                draw_statistics_row("Throughput Avg", std::format("{:.2f} MSPP/s over {} sample frames", this->statistics.throughput_mspp.average(), this->statistics.throughput_mspp.count));
            else
                draw_statistics_row("Throughput Avg", sampling_completed ? "Completed" : "Collecting");
            draw_statistics_row("Last Sample Throughput", this->statistics.has_throughput ? std::format("{:.2f} MSPP/s", this->statistics.last_valid_throughput_mspp) : "No sample yet");
            draw_statistics_row("Current Frame Work", this->statistics.last_frame_rendered_sample ? "Rendered sample" : "No PBRT sample");
            ImGui::EndTable();
        }

        ImGui::SeparatorText("CUDA/Vulkan Interop");
        if (ImGui::BeginTable("SpectraInteropStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Active PBRT Frame", std::format("{} / {}", this->pbrt_interactive->active_frame(), this->sync.frame_count));
            draw_statistics_row("Display Format", vk::to_string(this->pbrt_interactive->active_display_format()));
            draw_statistics_row("Interop Buffer", format_device_bytes(this->pbrt_interactive->active_interop_buffer_size()));
            draw_statistics_row("Image Layout", vk::to_string(this->pbrt_interactive->active_image_layout()));
            draw_statistics_row("CUDA Semaphore", this->pbrt_interactive->active_cuda_semaphore_imported() ? "Imported" : "Missing");
            draw_statistics_row("CUDA Pixel Buffer", this->pbrt_interactive->active_cuda_pixel_buffer_mapped() ? "Mapped" : "Missing");
            ImGui::EndTable();
        }
        ImGui::End();
    }

    void Spectra::create_swapchain(vk::raii::SwapchainKHR old_swapchain) {
        {
            const vk::SurfaceCapabilitiesKHR surface_capabilities   = this->context.physical_device.getSurfaceCapabilitiesKHR(this->surface.surface);
            const std::vector<vk::SurfaceFormatKHR> surface_formats = this->context.physical_device.getSurfaceFormatsKHR(this->surface.surface);
            const std::vector<vk::PresentModeKHR> present_modes     = this->context.physical_device.getSurfacePresentModesKHR(this->surface.surface);
            if (surface_formats.empty()) throw std::runtime_error("Surface has no formats");
            if (present_modes.empty()) throw std::runtime_error("Surface has no present modes");

            if (surface_formats.size() == 1 && surface_formats.front().format == vk::Format::eUndefined) {
                this->swapchain.format      = vk::Format::eB8G8R8A8Unorm;
                this->swapchain.color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
            } else {
                this->swapchain.format      = surface_formats.front().format;
                this->swapchain.color_space = surface_formats.front().colorSpace;

                bool selected = false;
                constexpr std::array preferred_surface_formats{
                    vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
                    vk::SurfaceFormatKHR{vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
                };
                for (const vk::SurfaceFormatKHR preferred_surface_format : preferred_surface_formats) {
                    for (const vk::SurfaceFormatKHR& surface_format : surface_formats) {
                        if (surface_format.format == preferred_surface_format.format && surface_format.colorSpace == preferred_surface_format.colorSpace) {
                            this->swapchain.format      = surface_format.format;
                            this->swapchain.color_space = surface_format.colorSpace;
                            selected                    = true;
                            break;
                        }
                    }
                    if (selected) break;
                }
            }

            this->swapchain.present_mode = vk::PresentModeKHR::eFifo;
            for (const vk::PresentModeKHR present_mode : present_modes) {
                if (present_mode == vk::PresentModeKHR::eMailbox) {
                    this->swapchain.present_mode = present_mode;
                    break;
                }
                if (present_mode == vk::PresentModeKHR::eImmediate) this->swapchain.present_mode = present_mode;
            }

            this->swapchain.extent = surface_capabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max() ? vk::Extent2D{std::clamp(this->surface.extent.width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width), std::clamp(this->surface.extent.height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height)} : surface_capabilities.currentExtent;
            if (this->swapchain.extent.width == 0 || this->swapchain.extent.height == 0) throw std::runtime_error("Cannot create swapchain with zero extent");

            this->swapchain.image_count = surface_capabilities.minImageCount + 1;
            if (this->swapchain.image_count < 2) this->swapchain.image_count = 2;
            if (surface_capabilities.maxImageCount != 0 && this->swapchain.image_count > surface_capabilities.maxImageCount) this->swapchain.image_count = surface_capabilities.maxImageCount;

            if ((surface_capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment) != vk::ImageUsageFlagBits::eColorAttachment) throw std::runtime_error("Swapchain must support color attachment usage");
            this->swapchain.usage = vk::ImageUsageFlagBits::eColorAttachment;
        }
        {
            const vk::SurfaceCapabilitiesKHR surface_capabilities = this->context.physical_device.getSurfaceCapabilitiesKHR(this->surface.surface);
            auto composite_alpha                                  = vk::CompositeAlphaFlagBitsKHR::eOpaque;
            if (!(surface_capabilities.supportedCompositeAlpha & composite_alpha)) {
                if (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
                    composite_alpha = vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
                else if (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied)
                    composite_alpha = vk::CompositeAlphaFlagBitsKHR::ePostMultiplied;
                else if (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit)
                    composite_alpha = vk::CompositeAlphaFlagBitsKHR::eInherit;
                else
                    throw std::runtime_error("Surface has no supported composite alpha mode");
            }

            const vk::SurfaceTransformFlagBitsKHR pre_transform = surface_capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity ? vk::SurfaceTransformFlagBitsKHR::eIdentity : surface_capabilities.currentTransform;
            const vk::SwapchainCreateInfoKHR swapchain_create_info{{}, *this->surface.surface, this->swapchain.image_count, this->swapchain.format, this->swapchain.color_space, this->swapchain.extent, 1, this->swapchain.usage, vk::SharingMode::eExclusive, 0, nullptr, pre_transform, composite_alpha, this->swapchain.present_mode, VK_TRUE, *old_swapchain};
            this->swapchain.handle = vk::raii::SwapchainKHR{this->context.device, swapchain_create_info};
        }
        {
            this->swapchain.images = this->swapchain.handle.getImages();
            if (this->swapchain.images.empty()) throw std::runtime_error("Swapchain has no images");
            this->swapchain.image_layouts.assign(this->swapchain.images.size(), vk::ImageLayout::eUndefined);
        }
        {
            this->swapchain.image_views.clear();
            this->swapchain.image_views.reserve(this->swapchain.images.size());
            for (const vk::Image image : this->swapchain.images) {
                const vk::ImageViewCreateInfo image_view_create_info{{}, image, vk::ImageViewType::e2D, this->swapchain.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
                this->swapchain.image_views.emplace_back(this->context.device, image_view_create_info);
            }
            if (this->swapchain.image_views.size() != this->swapchain.images.size()) throw std::runtime_error("Failed to create all swapchain image views");
        }
        {
            constexpr vk::SemaphoreCreateInfo semaphore_create_info{};
            this->sync.render_finished_semaphores.clear();
            this->sync.render_finished_semaphores.reserve(this->swapchain.images.size());
            for (std::uint32_t image_index = 0; image_index < this->swapchain.images.size(); ++image_index) this->sync.render_finished_semaphores.emplace_back(this->context.device, semaphore_create_info);
            this->sync.image_in_flight_frame.assign(this->swapchain.images.size(), std::numeric_limits<std::uint32_t>::max());
        }
    }

    void Spectra::recreate_swapchain() {
        {
            int width  = 0;
            int height = 0;
            while (width == 0 || height == 0) {
                glfwGetFramebufferSize(this->surface.window.get(), &width, &height);
                if (width == 0 || height == 0) glfwWaitEvents();
            }
            this->surface.extent = vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        }

        this->context.device.waitIdle();
        vk::raii::SwapchainKHR old_swapchain = std::move(this->swapchain.handle);
        this->swapchain.image_views.clear();
        this->sync.render_finished_semaphores.clear();
        this->sync.image_in_flight_frame.clear();
        this->swapchain.image_layouts.clear();
        this->swapchain.images.clear();
        this->create_swapchain(std::move(old_swapchain));

        const std::uint32_t image_count = static_cast<std::uint32_t>(this->swapchain.images.size());
        if (!this->imgui.initialized) throw std::runtime_error("ImGui is not initialized during swapchain recreation");
        if (this->imgui.color_format != this->swapchain.format || this->imgui.image_count != image_count) {
            const bool docking   = this->imgui.docking;
            const bool viewports = this->imgui.viewports;
            this->destroy_imgui();
            this->imgui.docking   = docking;
            this->imgui.viewports = viewports;
            this->create_imgui();
            if (this->pbrt_interactive != nullptr) this->pbrt_interactive->create_imgui_descriptors();
        }
        this->surface.resize_requested = false;
    }
} // namespace xayah
