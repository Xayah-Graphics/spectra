module;

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>

#include <vulkan/vulkan_raii.hpp>

module spectra.rasterizer;

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

    void draw_status_row(const char* label, const std::string_view value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value.data(), value.data() + value.size());
    }
} // namespace

namespace spectra::rasterizer {
    class RasterizerRenderer::Impl {
    public:
        Impl();
        ~Impl() noexcept;

        [[nodiscard]] std::string_view name() const;
        void attach(RasterizerHostView host);
        void detach() noexcept;
        void before_imgui_shutdown() noexcept;
        void after_imgui_created();
        [[nodiscard]] RasterizerFrameResult begin_frame(RasterizerHostView host, const RasterizerFrameInfo& frame);
        void record_frame(const vk::raii::CommandBuffer& command_buffer);

    private:
        void update_host(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, std::uint32_t frame_count, vk::Extent2D swapchain_extent);
        void register_panels(RasterizerHostView& host);
        [[nodiscard]] std::string window_detail() const;

        void create_viewport_resources(vk::Extent2D extent);
        void destroy_viewport_resources_noexcept() noexcept;
        void ensure_viewport_resources();
        void create_imgui_descriptor();
        void destroy_imgui_descriptor_noexcept() noexcept;

        void draw_viewport_window();
        void draw_rasterizer_window();

        const vk::raii::PhysicalDevice* physical_device{};
        const vk::raii::Device* device{};
        vk::Extent2D swapchain_extent{};
        bool attached{false};
        bool imgui_ready{false};

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

    RasterizerRenderer::RasterizerRenderer() : impl(std::make_unique<Impl>()) {}

    RasterizerRenderer::~RasterizerRenderer() noexcept = default;

    RasterizerRenderer::RasterizerRenderer(RasterizerRenderer&& other) noexcept = default;

    RasterizerRenderer& RasterizerRenderer::operator=(RasterizerRenderer&& other) noexcept = default;

    std::string_view RasterizerRenderer::target_name() {
        return "Spectra Rasterizer";
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

    RasterizerFrameResult RasterizerRenderer::begin_frame(RasterizerHostView host, const RasterizerFrameInfo& frame) {
        return this->impl->begin_frame(std::move(host), frame);
    }

    void RasterizerRenderer::record_frame(const vk::raii::CommandBuffer& command_buffer) {
        this->impl->record_frame(command_buffer);
    }

    RasterizerRenderer::Impl::Impl() = default;

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
        host.register_panel(RasterizerPanel{
            .id                  = "rasterizer.viewport",
            .title               = "Rasterizer Viewport",
            .icon                = ICON_MS_GRID_VIEW,
            .shortcut_label      = "F7",
            .shortcut_key        = ImGuiKey_F7,
            .dock_slot           = RasterizerDockSlot::Center,
            .window_flags        = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse,
            .closable            = false,
            .show_in_toolbar     = true,
            .zero_window_padding = true,
            .draw                = [this] { this->draw_viewport_window(); },
        });
        host.register_panel(RasterizerPanel{
            .id              = "rasterizer.panel",
            .title           = "Rasterizer",
            .icon            = ICON_MS_TUNE,
            .shortcut_label  = "F8",
            .shortcut_key    = ImGuiKey_F8,
            .dock_slot       = RasterizerDockSlot::Right,
            .show_in_toolbar = true,
            .draw            = [this] { this->draw_rasterizer_window(); },
        });
    }

    std::string RasterizerRenderer::Impl::window_detail() const {
        const std::uint32_t width  = this->viewport.extent.width != 0 ? this->viewport.extent.width : this->swapchain_extent.width;
        const std::uint32_t height = this->viewport.extent.height != 0 ? this->viewport.extent.height : this->swapchain_extent.height;
        return std::format("Spectra Rasterizer | {}x{}", width, height);
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

    RasterizerFrameResult RasterizerRenderer::Impl::begin_frame(RasterizerHostView host, const RasterizerFrameInfo&) {
        this->update_host(host.physical_device(), host.device(), host.frame_count(), host.swapchain_extent());
        RasterizerFrameResult result{};
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

    void RasterizerRenderer::Impl::draw_rasterizer_window() {
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        ImGui::SeparatorText("Status");
        if (ImGui::BeginTable("SpectraRasterizerStatus", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_status_row("Renderer", "Development");
            draw_status_row("Scene Translation", "Not implemented");
            draw_status_row("Viewport", std::format("{} x {}", this->viewport.extent.width, this->viewport.extent.height));
            draw_status_row("Swapchain", std::format("{} x {}", this->swapchain_extent.width, this->swapchain_extent.height));
            ImGui::EndTable();
        }
    }
} // namespace spectra::rasterizer
