module;

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>

#include <vulkan/vulkan_raii.hpp>

module spectra.rasterizer;

import spectra.scene;
import spectra.contract;
import std;

namespace {
    void transition_image_layout(const vk::raii::CommandBuffer& command_buffer, const vk::Image image, const vk::ImageLayout old_layout, const vk::ImageLayout new_layout, const vk::PipelineStageFlags2 src_stage, const vk::AccessFlags2 src_access, const vk::PipelineStageFlags2 dst_stage, const vk::AccessFlags2 dst_access) {
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
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        const vk::DependencyInfo dependency_info{{}, 0, nullptr, 0, nullptr, 1, &image_memory_barrier};
        command_buffer.pipelineBarrier2(dependency_info);
    }

    [[nodiscard]] std::uint32_t find_memory_type_index(const vk::raii::PhysicalDevice& physical_device, const std::uint32_t memory_type_bits, const vk::MemoryPropertyFlags required_properties) {
        const vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            const bool supported = (memory_type_bits & (1u << index)) != 0;
            const bool matching  = (memory_properties.memoryTypes[index].propertyFlags & required_properties) == required_properties;
            if (supported && matching) return index;
        }
        throw std::runtime_error("No matching Vulkan memory type for Spectra rasterizer viewport");
    }
} // namespace

namespace spectra::rasterizer {
    [[nodiscard]] spectra::scene::SceneTranslationReport analyze_rasterizer_scene(const spectra::scene::SceneSnapshot& document) {
        spectra::scene::SceneTranslationReport report{.target = std::string{RasterizerRenderer::target_name()}, .supported = false};
        if (document.name.empty()) {
            report.diagnostics.push_back(spectra::scene::SceneDiagnostic{
                .source  = spectra::scene::SceneSourceLocation{.filename = document.source, .line = 1, .column = 1},
                .message = "Rasterizer scene translation requires a named scene document",
            });
        } else {
            report.diagnostics.push_back(spectra::scene::SceneDiagnostic{
                .source  = spectra::scene::SceneSourceLocation{.filename = document.source, .line = 1, .column = 1},
                .message = "Rasterizer backend does not currently provide PBRT scene rasterization translation",
            });
        }
        return report;
    }

    class RasterizerRenderer::Impl {
    public:
        explicit Impl(std::shared_ptr<spectra::scene::SceneWorkspace> source_workspace);
        ~Impl() noexcept;

        [[nodiscard]] std::string_view name() const;
        void attach(RasterizerHostView host);
        void detach() noexcept;
        void before_imgui_shutdown() noexcept;
        void after_imgui_created();
        [[nodiscard]] SpectraFrameResult begin_frame(RasterizerHostView host, const SpectraFrameInfo& frame);
        void record_frame(const vk::raii::CommandBuffer& command_buffer);

    private:
        void update_host(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, std::uint32_t frame_count, vk::Extent2D swapchain_extent);
        void register_panels(RasterizerHostView& host);
        void synchronize_scene_workspace();
        [[nodiscard]] std::string window_detail() const;

        void create_viewport_resources(vk::Extent2D extent);
        void destroy_viewport_resources_noexcept() noexcept;
        void ensure_viewport_resources();
        void create_imgui_descriptor();
        void destroy_imgui_descriptor_noexcept() noexcept;

        void draw_viewport_window();
        void draw_settings_window();

        const vk::raii::PhysicalDevice* physical_device{};
        const vk::raii::Device* device{};
        vk::Extent2D swapchain_extent{};
        bool attached{false};
        bool imgui_ready{false};
        std::shared_ptr<spectra::scene::SceneWorkspace> source_workspace{};

        std::shared_ptr<const spectra::scene::SceneSnapshot> scene_snapshot{};
        std::optional<spectra::scene::SceneInfo> scene_info{};

        struct {
            vk::Extent2D requested_extent{};
        } ui;

        struct {
            vk::Extent2D extent{};
            vk::Format format{vk::Format::eR16G16B16A16Sfloat};
            vk::ImageLayout layout{vk::ImageLayout::eUndefined};
            vk::raii::Image image{nullptr};
            vk::raii::DeviceMemory memory{nullptr};
            vk::raii::ImageView view{nullptr};
            vk::raii::Sampler sampler{nullptr};
            VkDescriptorSet imgui_descriptor{VK_NULL_HANDLE};
        } viewport;
    };

    RasterizerRenderer::RasterizerRenderer(std::shared_ptr<spectra::scene::SceneWorkspace> source_workspace) : impl(std::make_unique<Impl>(std::move(source_workspace))) {}

    RasterizerRenderer::~RasterizerRenderer() noexcept = default;

    RasterizerRenderer::RasterizerRenderer(RasterizerRenderer&& other) noexcept = default;

    RasterizerRenderer& RasterizerRenderer::operator=(RasterizerRenderer&& other) noexcept = default;

    std::string_view RasterizerRenderer::target_name() {
        return "Spectra Rasterizer";
    }

    spectra::scene::SceneTranslationTarget RasterizerRenderer::translation_target() {
        return spectra::scene::SceneTranslationTarget{
            .rendererName = std::string{RasterizerRenderer::target_name()},
            .analyze      = [](const spectra::scene::SceneSnapshot& document) { return analyze_rasterizer_scene(document); },
        };
    }

    std::string_view RasterizerRenderer::name() const {
        return this->impl->name();
    }

    void RasterizerRenderer::attach(RasterizerHostView host) {
        this->impl->attach(std::move(host));
    }

    void RasterizerRenderer::detach() noexcept {
        this->impl->detach();
    }

    void RasterizerRenderer::before_imgui_shutdown() noexcept {
        this->impl->before_imgui_shutdown();
    }

    void RasterizerRenderer::after_imgui_created() {
        this->impl->after_imgui_created();
    }

    SpectraFrameResult RasterizerRenderer::begin_frame(RasterizerHostView host, const SpectraFrameInfo& frame) {
        return this->impl->begin_frame(std::move(host), frame);
    }

    void RasterizerRenderer::record_frame(const vk::raii::CommandBuffer& command_buffer) {
        this->impl->record_frame(command_buffer);
    }

    RasterizerRenderer::Impl::Impl(std::shared_ptr<spectra::scene::SceneWorkspace> source_workspace) : source_workspace(std::move(source_workspace)) {
        if (this->source_workspace == nullptr) throw std::runtime_error("Spectra rasterizer requires a scene workspace");
    }

    RasterizerRenderer::Impl::~Impl() noexcept = default;

    std::string_view RasterizerRenderer::Impl::name() const {
        return "Spectra Rasterizer";
    }

    void RasterizerRenderer::Impl::update_host(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count, const vk::Extent2D swapchain_extent) {
        if (frame_count == 0) throw std::runtime_error("Spectra rasterizer host frame count must be positive");
        if (swapchain_extent.width == 0 || swapchain_extent.height == 0) throw std::runtime_error("Spectra rasterizer host swapchain extent must be positive");
        this->physical_device  = &physical_device;
        this->device           = &device;
        this->swapchain_extent = swapchain_extent;
        if (this->ui.requested_extent.width == 0 || this->ui.requested_extent.height == 0) this->ui.requested_extent = swapchain_extent;
    }

    void RasterizerRenderer::Impl::register_panels(RasterizerHostView& host) {
        host.register_panel(SpectraPanel{
            .id                  = "rasterizer.viewport",
            .title               = "Rasterizer",
            .icon                = ICON_MS_GRID_VIEW,
            .shortcut_label      = "F7",
            .shortcut_key        = ImGuiKey_F7,
            .dock_slot           = SpectraDockSlot::Center,
            .window_flags        = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse,
            .visible             = false,
            .show_in_toolbar     = true,
            .zero_window_padding = true,
            .draw                = [this] { this->draw_viewport_window(); },
        });
        host.register_panel(SpectraPanel{
            .id              = "rasterizer.settings",
            .title           = "Rasterizer Settings",
            .icon            = ICON_MS_TUNE,
            .shortcut_label  = "F8",
            .shortcut_key    = ImGuiKey_F8,
            .dock_slot       = SpectraDockSlot::Right,
            .visible         = false,
            .show_in_toolbar = true,
            .draw            = [this] { this->draw_settings_window(); },
        });
    }

    void RasterizerRenderer::Impl::synchronize_scene_workspace() {
        std::shared_ptr<const spectra::scene::SceneSnapshot> next_snapshot = this->source_workspace->snapshot();
        if (next_snapshot == nullptr) throw std::runtime_error("Spectra rasterizer source scene workspace returned an empty scene snapshot");
        if (this->scene_snapshot != nullptr && this->scene_snapshot->revision == next_snapshot->revision) return;
        const spectra::scene::SceneTranslationReport report = analyze_rasterizer_scene(*next_snapshot);
        if (!report.supported) {
            std::string message = std::format("{} cannot translate scene \"{}\"", RasterizerRenderer::target_name(), next_snapshot->name);
            if (!report.diagnostics.empty()) message = std::format("{}: {}", message, report.diagnostics.front().message);
            throw std::runtime_error(message);
        }
        this->scene_snapshot = std::move(next_snapshot);
        this->scene_info     = spectra::scene::DescribeScene(*this->scene_snapshot);
    }

    std::string RasterizerRenderer::Impl::window_detail() const {
        const std::string scene_title = !this->scene_info.has_value() ? "No Scene" : this->scene_info->title;
        const std::uint32_t width     = this->viewport.extent.width != 0 ? this->viewport.extent.width : this->swapchain_extent.width;
        const std::uint32_t height    = this->viewport.extent.height != 0 ? this->viewport.extent.height : this->swapchain_extent.height;
        return std::format("{} | Spectra Rasterizer | {}x{}", scene_title, width, height);
    }

    void RasterizerRenderer::Impl::create_viewport_resources(const vk::Extent2D extent) {
        if (this->physical_device == nullptr || this->device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer viewport without Vulkan handles");
        if (extent.width == 0 || extent.height == 0) throw std::runtime_error("Cannot create Spectra rasterizer viewport with a zero extent");
        const vk::ImageCreateInfo image_create_info{
            {},
            vk::ImageType::e2D,
            this->viewport.format,
            vk::Extent3D{extent.width, extent.height, 1},
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::SharingMode::eExclusive,
            0,
            nullptr,
            vk::ImageLayout::eUndefined,
        };
        this->viewport.image                             = vk::raii::Image{*this->device, image_create_info};
        const vk::MemoryRequirements memory_requirements = this->viewport.image.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(*this->physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        this->viewport.memory = vk::raii::DeviceMemory{*this->device, memory_allocate_info};
        this->viewport.image.bindMemory(*this->viewport.memory, 0);

        const vk::ImageViewCreateInfo image_view_create_info{{}, *this->viewport.image, vk::ImageViewType::e2D, this->viewport.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        this->viewport.view = vk::raii::ImageView{*this->device, image_view_create_info};
        const vk::SamplerCreateInfo sampler_create_info{
            {},
            vk::Filter::eLinear,
            vk::Filter::eLinear,
            vk::SamplerMipmapMode::eLinear,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            0.0f,
            VK_FALSE,
            1.0f,
            VK_FALSE,
            vk::CompareOp::eAlways,
            0.0f,
            0.0f,
            vk::BorderColor::eFloatOpaqueBlack,
            VK_FALSE,
        };
        this->viewport.sampler = vk::raii::Sampler{*this->device, sampler_create_info};
        this->viewport.extent  = extent;
        this->viewport.layout  = vk::ImageLayout::eUndefined;
        if (this->imgui_ready) this->create_imgui_descriptor();
    }

    void RasterizerRenderer::Impl::destroy_imgui_descriptor_noexcept() noexcept {
        if (this->viewport.imgui_descriptor == VK_NULL_HANDLE) return;
        ImGui_ImplVulkan_RemoveTexture(this->viewport.imgui_descriptor);
        this->viewport.imgui_descriptor = VK_NULL_HANDLE;
    }

    void RasterizerRenderer::Impl::destroy_viewport_resources_noexcept() noexcept {
        try {
            this->destroy_imgui_descriptor_noexcept();
            if (this->device != nullptr) this->device->waitIdle();
        } catch (...) {
        }
        this->viewport.sampler = nullptr;
        this->viewport.view    = nullptr;
        this->viewport.image   = nullptr;
        this->viewport.memory  = nullptr;
        this->viewport.extent  = vk::Extent2D{};
        this->viewport.layout  = vk::ImageLayout::eUndefined;
    }

    void RasterizerRenderer::Impl::ensure_viewport_resources() {
        if (this->ui.requested_extent.width == 0 || this->ui.requested_extent.height == 0) return;
        if (*this->viewport.image && this->viewport.extent.width == this->ui.requested_extent.width && this->viewport.extent.height == this->ui.requested_extent.height) return;
        this->destroy_viewport_resources_noexcept();
        this->create_viewport_resources(this->ui.requested_extent);
    }

    void RasterizerRenderer::Impl::create_imgui_descriptor() {
        if (!*this->viewport.sampler || !*this->viewport.view) throw std::runtime_error("Cannot create Spectra rasterizer descriptor before viewport resources exist");
        if (this->viewport.imgui_descriptor != VK_NULL_HANDLE) throw std::runtime_error("Spectra rasterizer viewport descriptor is already allocated");
        this->viewport.imgui_descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(*this->viewport.sampler), static_cast<VkImageView>(*this->viewport.view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (this->viewport.imgui_descriptor == VK_NULL_HANDLE) throw std::runtime_error("Failed to allocate Spectra rasterizer viewport descriptor");
    }

    void RasterizerRenderer::Impl::attach(RasterizerHostView host) {
        if (this->attached) throw std::runtime_error("Spectra rasterizer plugin is already attached");
        this->update_host(host.physical_device(), host.device(), host.frame_count(), host.swapchain_extent());
        this->attached = true;
        try {
            this->register_panels(host);
            host.set_window_detail(this->window_detail());
        } catch (...) {
            this->detach();
            throw;
        }
    }

    void RasterizerRenderer::Impl::detach() noexcept {
        this->destroy_viewport_resources_noexcept();
        this->physical_device  = nullptr;
        this->device           = nullptr;
        this->swapchain_extent = vk::Extent2D{};
        this->attached         = false;
        this->imgui_ready      = false;
    }

    void RasterizerRenderer::Impl::before_imgui_shutdown() noexcept {
        this->destroy_imgui_descriptor_noexcept();
        this->imgui_ready = false;
    }

    void RasterizerRenderer::Impl::after_imgui_created() {
        this->imgui_ready = true;
        if (*this->viewport.image && this->viewport.imgui_descriptor == VK_NULL_HANDLE) this->create_imgui_descriptor();
    }

    SpectraFrameResult RasterizerRenderer::Impl::begin_frame(RasterizerHostView host, const SpectraFrameInfo&) {
        this->update_host(host.physical_device(), host.device(), host.frame_count(), host.swapchain_extent());
        this->synchronize_scene_workspace();
        SpectraFrameResult result{};
        this->ensure_viewport_resources();
        result.window_detail = this->window_detail();
        return result;
    }

    void RasterizerRenderer::Impl::record_frame(const vk::raii::CommandBuffer& command_buffer) {
        if (!*this->viewport.image) return;
        transition_image_layout(command_buffer, *this->viewport.image, this->viewport.layout, vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
        this->viewport.layout = vk::ImageLayout::eColorAttachmentOptimal;

        constexpr std::array<float, 4> clear_color{0.08f, 0.085f, 0.075f, 1.0f};
        const vk::ClearValue clear_value{vk::ClearColorValue{clear_color}};
        const vk::RenderingAttachmentInfo color_attachment{
            *this->viewport.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            clear_value,
        };
        const vk::RenderingInfo rendering_info{{}, {{0, 0}, this->viewport.extent}, 1, 0, 1, &color_attachment, nullptr, nullptr};
        command_buffer.beginRendering(rendering_info);
        command_buffer.endRendering();

        transition_image_layout(command_buffer, *this->viewport.image, this->viewport.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        this->viewport.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    void RasterizerRenderer::Impl::draw_viewport_window() {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x > 1.0f && available.y > 1.0f) {
            this->ui.requested_extent = vk::Extent2D{static_cast<std::uint32_t>(available.x), static_cast<std::uint32_t>(available.y)};
        }
        if (this->viewport.imgui_descriptor == VK_NULL_HANDLE) return;
        ImGui::Image(reinterpret_cast<ImTextureID>(this->viewport.imgui_descriptor), available, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f});
    }

    void RasterizerRenderer::Impl::draw_settings_window() {
        if (this->scene_info.has_value()) {
            ImGui::TextUnformatted(this->scene_info->title.c_str());
            ImGui::Text("Revision: %llu", static_cast<unsigned long long>(this->scene_snapshot->revision.value));
            ImGui::Text("Shapes: %llu", static_cast<unsigned long long>(this->scene_info->shape_count));
            ImGui::Text("Materials: %llu", static_cast<unsigned long long>(this->scene_info->material_count));
            ImGui::Text("Textures: %llu", static_cast<unsigned long long>(this->scene_info->texture_count));
        }
    }
} // namespace spectra::rasterizer
