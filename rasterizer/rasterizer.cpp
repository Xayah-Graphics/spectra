module;

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <spectra_rasterizer_mesh_fragment_spv.h>
#include <spectra_rasterizer_mesh_vertex_spv.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <vulkan/vulkan_raii.hpp>

module spectra.rasterizer;

import std;

namespace {
    struct MathVector3 {
        float x{};
        float y{};
        float z{};
    };

    struct MathMatrix4 {
        std::array<float, 16> values{};

        [[nodiscard]] float& at(const std::size_t row, const std::size_t column) {
            return this->values.at(row * 4u + column);
        }

        [[nodiscard]] const float& at(const std::size_t row, const std::size_t column) const {
            return this->values.at(row * 4u + column);
        }
    };

    struct RasterizerVertex {
        float px{};
        float py{};
        float pz{};
        float nx{};
        float ny{};
        float nz{};
    };

    struct CameraUniformData {
        std::array<float, 16> viewProjection{};
        std::array<float, 4> cameraPosition{};
        std::array<float, 4> lightDirection{};
        std::array<float, 4> lightColorIntensity{};
    };

    struct DrawPushConstantsData {
        std::array<float, 16> model{};
        std::array<float, 4> baseColor{};
        std::array<float, 4> emission{};
    };

    struct GpuBuffer {
        vk::raii::Buffer buffer{nullptr};
        vk::raii::DeviceMemory memory{nullptr};
        void* mapped{};
        vk::DeviceSize capacity{};
    };

    struct RenderDrawCommand {
        std::string name{};
        std::uint32_t firstIndex{};
        std::uint32_t indexCount{};
        spectra::rasterizer::SceneTransform transform{};
        spectra::rasterizer::SceneMaterial material{};
    };

    [[nodiscard]] MathMatrix4 identity_matrix() {
        MathMatrix4 matrix{};
        matrix.at(0u, 0u) = 1.0f;
        matrix.at(1u, 1u) = 1.0f;
        matrix.at(2u, 2u) = 1.0f;
        matrix.at(3u, 3u) = 1.0f;
        return matrix;
    }

    [[nodiscard]] MathMatrix4 multiply_matrix(const MathMatrix4& lhs, const MathMatrix4& rhs) {
        MathMatrix4 result{};
        for (std::size_t row = 0; row < 4u; ++row) {
            for (std::size_t column = 0; column < 4u; ++column) {
                for (std::size_t index = 0; index < 4u; ++index) result.at(row, column) += lhs.at(row, index) * rhs.at(index, column);
            }
        }
        return result;
    }

    [[nodiscard]] MathVector3 to_math_vector(const spectra::rasterizer::SceneVector3& value) {
        return MathVector3{value.x, value.y, value.z};
    }

    [[nodiscard]] float dot_vector(const MathVector3 lhs, const MathVector3 rhs) {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }

    [[nodiscard]] MathVector3 cross_vector(const MathVector3 lhs, const MathVector3 rhs) {
        return MathVector3{
            lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x,
        };
    }

    [[nodiscard]] MathVector3 subtract_vector(const MathVector3 lhs, const MathVector3 rhs) {
        return MathVector3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
    }

    [[nodiscard]] MathVector3 normalize_vector(const MathVector3 value) {
        const float length = std::sqrt(dot_vector(value, value));
        if (!std::isfinite(length) || length <= 0.0f) throw std::runtime_error("Cannot normalize a zero-length rasterizer vector");
        return MathVector3{value.x / length, value.y / length, value.z / length};
    }

    [[nodiscard]] MathMatrix4 transform_matrix(const spectra::rasterizer::SceneTransform& transform) {
        const spectra::rasterizer::SceneQuaternion rotation = transform.rotation;
        const float length_squared = rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
        if (!std::isfinite(length_squared) || length_squared <= 0.0f) throw std::runtime_error("Rasterizer scene transform contains an invalid quaternion");
        const float inverse_length = 1.0f / std::sqrt(length_squared);
        const float x              = rotation.x * inverse_length;
        const float y              = rotation.y * inverse_length;
        const float z              = rotation.z * inverse_length;
        const float w              = rotation.w * inverse_length;

        MathMatrix4 scale = identity_matrix();
        scale.at(0u, 0u) = transform.scale.x;
        scale.at(1u, 1u) = transform.scale.y;
        scale.at(2u, 2u) = transform.scale.z;

        MathMatrix4 rotate = identity_matrix();
        rotate.at(0u, 0u) = 1.0f - 2.0f * y * y - 2.0f * z * z;
        rotate.at(0u, 1u) = 2.0f * x * y + 2.0f * w * z;
        rotate.at(0u, 2u) = 2.0f * x * z - 2.0f * w * y;
        rotate.at(1u, 0u) = 2.0f * x * y - 2.0f * w * z;
        rotate.at(1u, 1u) = 1.0f - 2.0f * x * x - 2.0f * z * z;
        rotate.at(1u, 2u) = 2.0f * y * z + 2.0f * w * x;
        rotate.at(2u, 0u) = 2.0f * x * z + 2.0f * w * y;
        rotate.at(2u, 1u) = 2.0f * y * z - 2.0f * w * x;
        rotate.at(2u, 2u) = 1.0f - 2.0f * x * x - 2.0f * y * y;

        MathMatrix4 translate = identity_matrix();
        translate.at(3u, 0u) = transform.position.x;
        translate.at(3u, 1u) = transform.position.y;
        translate.at(3u, 2u) = transform.position.z;
        return multiply_matrix(multiply_matrix(scale, rotate), translate);
    }

    [[nodiscard]] MathMatrix4 look_at_matrix(const MathVector3 eye, const MathVector3 target) {
        const MathVector3 forward = normalize_vector(subtract_vector(target, eye));
        constexpr MathVector3 world_up{0.0f, 1.0f, 0.0f};
        const MathVector3 side = normalize_vector(cross_vector(forward, world_up));
        const MathVector3 up   = cross_vector(side, forward);

        MathMatrix4 view = identity_matrix();
        view.at(0u, 0u) = side.x;
        view.at(1u, 0u) = side.y;
        view.at(2u, 0u) = side.z;
        view.at(0u, 1u) = up.x;
        view.at(1u, 1u) = up.y;
        view.at(2u, 1u) = up.z;
        view.at(0u, 2u) = -forward.x;
        view.at(1u, 2u) = -forward.y;
        view.at(2u, 2u) = -forward.z;
        view.at(3u, 0u) = -dot_vector(side, eye);
        view.at(3u, 1u) = -dot_vector(up, eye);
        view.at(3u, 2u) = dot_vector(forward, eye);
        return view;
    }

    [[nodiscard]] MathMatrix4 perspective_matrix(const float vertical_fov_degrees, const float aspect, const float near_plane, const float far_plane) {
        if (!std::isfinite(vertical_fov_degrees) || vertical_fov_degrees <= 0.0f || vertical_fov_degrees >= 179.0f) throw std::runtime_error("Rasterizer camera vertical FOV must be in (0, 179)");
        if (!std::isfinite(aspect) || aspect <= 0.0f) throw std::runtime_error("Rasterizer camera aspect ratio must be positive");
        if (!std::isfinite(near_plane) || !std::isfinite(far_plane) || near_plane <= 0.0f || far_plane <= near_plane) throw std::runtime_error("Rasterizer camera clipping planes are invalid");
        const float f = 1.0f / std::tan(vertical_fov_degrees * std::numbers::pi_v<float> / 360.0f);
        MathMatrix4 projection{};
        projection.at(0u, 0u) = f / aspect;
        projection.at(1u, 1u) = -f;
        projection.at(2u, 2u) = far_plane / (near_plane - far_plane);
        projection.at(2u, 3u) = -1.0f;
        projection.at(3u, 2u) = -(far_plane * near_plane) / (far_plane - near_plane);
        return projection;
    }

    [[nodiscard]] CameraUniformData make_camera_uniform(const spectra::rasterizer::SceneDocument& scene, const vk::Extent2D extent) {
        if (!scene.camera.has_value()) throw std::runtime_error("Rasterizer mesh rendering requires a scene camera");
        const spectra::rasterizer::SceneCamera& camera = *scene.camera;
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const MathVector3 eye = to_math_vector(camera.transform.position);
        const MathVector3 target = to_math_vector(camera.target);
        const MathMatrix4 view_projection = multiply_matrix(look_at_matrix(eye, target), perspective_matrix(camera.verticalFovDegrees, aspect, camera.nearPlane, camera.farPlane));

        MathVector3 light_direction{-0.35f, -0.8f, -0.45f};
        MathVector3 light_color{1.0f, 1.0f, 1.0f};
        float light_intensity = 1.0f;
        for (const spectra::rasterizer::SceneLight& light : scene.lights) {
            if (light.kind != spectra::rasterizer::SceneLightKind::Directional) continue;
            light_color     = to_math_vector(light.color);
            light_intensity = light.intensity;
            break;
        }

        const MathVector3 normalized_light_direction = normalize_vector(light_direction);
        return CameraUniformData{
            .viewProjection      = view_projection.values,
            .cameraPosition      = {eye.x, eye.y, eye.z, 0.0f},
            .lightDirection      = {normalized_light_direction.x, normalized_light_direction.y, normalized_light_direction.z, 0.0f},
            .lightColorIntensity = {light_color.x, light_color.y, light_color.z, light_intensity},
        };
    }

    void transition_image_layout(const vk::raii::CommandBuffer& command_buffer, const vk::Image image, const vk::ImageAspectFlags aspect, const vk::ImageLayout old_layout, const vk::ImageLayout new_layout, const vk::PipelineStageFlags2 src_stage, const vk::AccessFlags2 src_access, const vk::PipelineStageFlags2 dst_stage, const vk::AccessFlags2 dst_access) {
        if (old_layout == new_layout) return;
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

    [[nodiscard]] std::uint32_t find_memory_type_index(const vk::raii::PhysicalDevice& physical_device, const std::uint32_t memory_type_bits, const vk::MemoryPropertyFlags required_properties) {
        const vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            const bool supported = (memory_type_bits & (1u << index)) != 0;
            const bool matching  = (memory_properties.memoryTypes[index].propertyFlags & required_properties) == required_properties;
            if (supported && matching) return index;
        }
        throw std::runtime_error("No matching Vulkan memory type for Spectra rasterizer");
    }

    void draw_status_row(const char* label, const std::string_view value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value.data(), value.data() + value.size());
    }

    [[nodiscard]] const char* timeline_mode_text(const spectra::rasterizer::SimulationTimelineMode mode) {
        switch (mode) {
        case spectra::rasterizer::SimulationTimelineMode::Live: return "Live";
        case spectra::rasterizer::SimulationTimelineMode::Record: return "Record";
        case spectra::rasterizer::SimulationTimelineMode::Playback: return "Playback";
        }
        throw std::runtime_error("Unknown Spectra rasterizer timeline mode");
    }
} // namespace

namespace spectra::rasterizer {
    class RasterizerRenderer::Impl {
    public:
        explicit Impl(std::shared_ptr<SceneWorkspace> scene_workspace);
        ~Impl() noexcept;

        [[nodiscard]] std::string_view name() const;
        void attach(RasterizerHostView host);
        void detach() noexcept;
        void before_imgui_shutdown() noexcept;
        void after_imgui_created();
        [[nodiscard]] RasterizerFrameResult begin_frame(RasterizerHostView host, const RasterizerFrameInfo& frame);
        void record_frame(const vk::raii::CommandBuffer& command_buffer);

    private:
        struct FrameSceneResources {
            GpuBuffer vertexBuffer{};
            GpuBuffer indexBuffer{};
            SceneRevision uploadedRevision{};
            std::vector<RenderDrawCommand> drawCommands{};
        };

        void update_host(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, std::uint32_t frame_count, vk::Extent2D swapchain_extent);
        void register_panels(RasterizerHostView& host);
        [[nodiscard]] std::string window_detail() const;

        void create_viewport_resources(vk::Extent2D extent);
        void destroy_viewport_resources_noexcept() noexcept;
        void ensure_viewport_resources();
        void create_imgui_descriptor();
        void destroy_imgui_descriptor_noexcept() noexcept;

        void ensure_mesh_resources();
        void destroy_mesh_resources_noexcept() noexcept;
        void ensure_host_buffer(GpuBuffer& buffer, vk::DeviceSize required_size, vk::BufferUsageFlags usage);
        void destroy_host_buffer_noexcept(GpuBuffer& buffer) noexcept;
        void upload_scene_resources(std::uint32_t frame_index);
        void update_camera_uniform(std::uint32_t frame_index);
        [[nodiscard]] std::vector<SceneMesh> collect_render_meshes() const;
        [[nodiscard]] SceneMaterial resolve_material(std::string_view material_name) const;

        void draw_viewport_window();
        void draw_rasterizer_window();
        void commit_timeline_from_ui(SimulationTimeline timeline);

        const vk::raii::PhysicalDevice* physical_device{};
        const vk::raii::Device* device{};
        vk::Extent2D swapchain_extent{};
        std::uint32_t frame_count{};
        std::uint32_t active_frame_index{};
        bool attached{false};
        bool imgui_ready{false};
        std::shared_ptr<SceneWorkspace> scene_workspace{};

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
            vk::Format depth_format{vk::Format::eD32Sfloat};
            vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
            vk::raii::Image depth_image{nullptr};
            vk::raii::DeviceMemory depth_memory{nullptr};
            vk::raii::ImageView depth_view{nullptr};
        } viewport;

        struct {
            std::uint32_t frame_count{};
            vk::raii::DescriptorSetLayout descriptor_set_layout{nullptr};
            vk::raii::DescriptorPool descriptor_pool{nullptr};
            vk::raii::DescriptorSets descriptor_sets{nullptr};
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::Pipeline pipeline{nullptr};
            std::vector<GpuBuffer> uniform_buffers{};
            std::vector<FrameSceneResources> frame_scenes{};
        } mesh_pass;
    };

    RasterizerRenderer::RasterizerRenderer(std::shared_ptr<SceneWorkspace> scene_workspace) : impl(std::make_unique<Impl>(std::move(scene_workspace))) {}

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

    RasterizerRenderer::Impl::Impl(std::shared_ptr<SceneWorkspace> scene_workspace) : scene_workspace(std::move(scene_workspace)) {
        if (this->scene_workspace == nullptr) throw std::runtime_error("Spectra rasterizer requires a scene workspace");
        if (!this->scene_workspace->loaded()) throw std::runtime_error("Spectra rasterizer requires a loaded scene workspace");
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
        this->frame_count      = frame_count;
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
            .zero_window_padding = true,
            .draw                = [this] { this->draw_viewport_window(); },
        });
        host.register_sidebar_tab(RasterizerSidebarTab{
            .id             = "rasterizer.panel",
            .title          = "Rasterizer",
            .icon           = ICON_MS_TUNE,
            .shortcut_label = "F8",
            .shortcut_key   = ImGuiKey_F8,
            .draw           = [this] { this->draw_rasterizer_window(); },
        });
    }

    std::string RasterizerRenderer::Impl::window_detail() const {
        const std::uint32_t width  = this->viewport.extent.width != 0 ? this->viewport.extent.width : this->swapchain_extent.width;
        const std::uint32_t height = this->viewport.extent.height != 0 ? this->viewport.extent.height : this->swapchain_extent.height;
        const std::shared_ptr<const SceneDocument> scene = this->scene_workspace->document();
        const SimulationTimeline timeline = this->scene_workspace->timeline();
        return std::format("{} | {} | {}x{}", scene->title.empty() ? scene->name : scene->title, timeline_mode_text(timeline.mode), width, height);
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

        const vk::ImageCreateInfo depth_image_create_info{
            {},
            vk::ImageType::e2D,
            this->viewport.depth_format,
            vk::Extent3D{extent.width, extent.height, 1},
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::SharingMode::eExclusive,
            0,
            nullptr,
            vk::ImageLayout::eUndefined,
        };
        this->viewport.depth_image                             = vk::raii::Image{*this->device, depth_image_create_info};
        const vk::MemoryRequirements depth_memory_requirements = this->viewport.depth_image.getMemoryRequirements();
        const std::uint32_t depth_memory_type                  = find_memory_type_index(*this->physical_device, depth_memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo depth_memory_allocate_info{depth_memory_requirements.size, depth_memory_type};
        this->viewport.depth_memory = vk::raii::DeviceMemory{*this->device, depth_memory_allocate_info};
        this->viewport.depth_image.bindMemory(*this->viewport.depth_memory, 0);
        const vk::ImageViewCreateInfo depth_view_create_info{{}, *this->viewport.depth_image, vk::ImageViewType::e2D, this->viewport.depth_format, {}, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}};
        this->viewport.depth_view = vk::raii::ImageView{*this->device, depth_view_create_info};

        this->viewport.extent       = extent;
        this->viewport.layout       = vk::ImageLayout::eUndefined;
        this->viewport.depth_layout = vk::ImageLayout::eUndefined;
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
        this->viewport.depth_view   = nullptr;
        this->viewport.depth_image  = nullptr;
        this->viewport.depth_memory = nullptr;
        this->viewport.sampler      = nullptr;
        this->viewport.view         = nullptr;
        this->viewport.image        = nullptr;
        this->viewport.memory       = nullptr;
        this->viewport.extent       = vk::Extent2D{};
        this->viewport.layout       = vk::ImageLayout::eUndefined;
        this->viewport.depth_layout = vk::ImageLayout::eUndefined;
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
        this->destroy_mesh_resources_noexcept();
        this->physical_device  = nullptr;
        this->device           = nullptr;
        this->swapchain_extent = vk::Extent2D{};
        this->frame_count      = 0;
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

    void RasterizerRenderer::Impl::destroy_host_buffer_noexcept(GpuBuffer& buffer) noexcept {
        try {
            if (buffer.mapped != nullptr && this->device != nullptr && *buffer.memory) vkUnmapMemory(static_cast<VkDevice>(**this->device), static_cast<VkDeviceMemory>(*buffer.memory));
        } catch (...) {
        }
        buffer.mapped   = nullptr;
        buffer.buffer   = nullptr;
        buffer.memory   = nullptr;
        buffer.capacity = 0;
    }

    void RasterizerRenderer::Impl::ensure_host_buffer(GpuBuffer& buffer, const vk::DeviceSize required_size, const vk::BufferUsageFlags usage) {
        if (required_size == 0) throw std::runtime_error("Cannot allocate an empty Spectra rasterizer buffer");
        if (this->physical_device == nullptr || this->device == nullptr) throw std::runtime_error("Cannot allocate Spectra rasterizer buffers without Vulkan handles");
        if (*buffer.buffer && buffer.capacity >= required_size) return;
        this->destroy_host_buffer_noexcept(buffer);
        const vk::BufferCreateInfo buffer_create_info{{}, required_size, usage, vk::SharingMode::eExclusive};
        buffer.buffer = vk::raii::Buffer{*this->device, buffer_create_info};
        const vk::MemoryRequirements memory_requirements = buffer.buffer.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(*this->physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        const vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        buffer.memory = vk::raii::DeviceMemory{*this->device, memory_allocate_info};
        buffer.buffer.bindMemory(*buffer.memory, 0);
        if (vkMapMemory(static_cast<VkDevice>(**this->device), static_cast<VkDeviceMemory>(*buffer.memory), 0, required_size, 0, &buffer.mapped) != VK_SUCCESS) throw std::runtime_error("Failed to map Spectra rasterizer buffer memory");
        buffer.capacity = required_size;
        if (buffer.mapped == nullptr) throw std::runtime_error("Failed to map Spectra rasterizer buffer memory");
    }

    void RasterizerRenderer::Impl::destroy_mesh_resources_noexcept() noexcept {
        try {
            if (this->device != nullptr) this->device->waitIdle();
        } catch (...) {
        }
        for (GpuBuffer& uniform_buffer : this->mesh_pass.uniform_buffers) this->destroy_host_buffer_noexcept(uniform_buffer);
        for (FrameSceneResources& frame_scene : this->mesh_pass.frame_scenes) {
            this->destroy_host_buffer_noexcept(frame_scene.vertexBuffer);
            this->destroy_host_buffer_noexcept(frame_scene.indexBuffer);
        }
        this->mesh_pass.frame_scenes.clear();
        this->mesh_pass.uniform_buffers.clear();
        this->mesh_pass.pipeline          = nullptr;
        this->mesh_pass.pipeline_layout   = nullptr;
        this->mesh_pass.descriptor_sets   = nullptr;
        this->mesh_pass.descriptor_pool   = nullptr;
        this->mesh_pass.descriptor_set_layout = nullptr;
        this->mesh_pass.frame_count       = 0;
    }

    void RasterizerRenderer::Impl::ensure_mesh_resources() {
        if (this->physical_device == nullptr || this->device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer mesh resources without Vulkan handles");
        if (this->frame_count == 0) throw std::runtime_error("Spectra rasterizer frame count must be positive");
        if (*this->mesh_pass.pipeline && this->mesh_pass.frame_count == this->frame_count) return;
        this->destroy_mesh_resources_noexcept();

        const vk::DescriptorSetLayoutBinding camera_binding{0u, vk::DescriptorType::eUniformBuffer, 1u, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment};
        const vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{{}, 1u, &camera_binding};
        this->mesh_pass.descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->device, descriptor_set_layout_create_info};

        const vk::DescriptorPoolSize descriptor_pool_size{vk::DescriptorType::eUniformBuffer, this->frame_count};
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, this->frame_count, 1u, &descriptor_pool_size};
        this->mesh_pass.descriptor_pool = vk::raii::DescriptorPool{*this->device, descriptor_pool_create_info};
        const vk::DescriptorSetLayout descriptor_set_layout = *this->mesh_pass.descriptor_set_layout;
        std::vector<vk::DescriptorSetLayout> descriptor_set_layouts(this->frame_count, descriptor_set_layout);
        const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*this->mesh_pass.descriptor_pool, this->frame_count, descriptor_set_layouts.data()};
        this->mesh_pass.descriptor_sets = vk::raii::DescriptorSets{*this->device, descriptor_set_allocate_info};
        if (this->mesh_pass.descriptor_sets.size() != this->frame_count) throw std::runtime_error("Failed to allocate Spectra rasterizer descriptor sets");

        this->mesh_pass.uniform_buffers.resize(this->frame_count);
        this->mesh_pass.frame_scenes.resize(this->frame_count);
        for (GpuBuffer& uniform_buffer : this->mesh_pass.uniform_buffers) this->ensure_host_buffer(uniform_buffer, sizeof(CameraUniformData), vk::BufferUsageFlagBits::eUniformBuffer);

        std::vector<vk::DescriptorBufferInfo> buffer_infos{};
        std::vector<vk::WriteDescriptorSet> descriptor_writes{};
        buffer_infos.reserve(this->frame_count);
        descriptor_writes.reserve(this->frame_count);
        for (std::uint32_t frame_index = 0; frame_index < this->frame_count; ++frame_index) {
            buffer_infos.emplace_back(*this->mesh_pass.uniform_buffers.at(frame_index).buffer, 0, sizeof(CameraUniformData));
            descriptor_writes.emplace_back(*this->mesh_pass.descriptor_sets.at(frame_index), 0u, 0u, 1u, vk::DescriptorType::eUniformBuffer, nullptr, &buffer_infos.back(), nullptr);
        }
        this->device->updateDescriptorSets(descriptor_writes, {});

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_mesh_vertex_spv_sizeInBytes, spectra_rasterizer_mesh_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_mesh_fragment_spv_sizeInBytes, spectra_rasterizer_mesh_fragment_spv};
        const vk::raii::ShaderModule vertex_shader{*this->device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->device, fragment_shader_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        const vk::VertexInputBindingDescription vertex_binding{0u, sizeof(RasterizerVertex), vk::VertexInputRate::eVertex};
        const std::array vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(offsetof(RasterizerVertex, px))},
            vk::VertexInputAttributeDescription{1u, 0u, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(offsetof(RasterizerVertex, nx))},
        };
        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1u, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment{
            VK_FALSE,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        const vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(DrawPushConstantsData)};
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1u, &descriptor_set_layout, 1u, &push_constant_range};
        this->mesh_pass.pipeline_layout = vk::raii::PipelineLayout{*this->device, pipeline_layout_create_info};
        const vk::Format color_format = this->viewport.format;
        const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{{}, 1u, &color_format, this->viewport.depth_format};
        vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};
        graphics_pipeline_create_info.setPNext(&pipeline_rendering_create_info);
        graphics_pipeline_create_info.setStageCount(static_cast<std::uint32_t>(shader_stages.size()));
        graphics_pipeline_create_info.setPStages(shader_stages.data());
        graphics_pipeline_create_info.setPVertexInputState(&vertex_input_state);
        graphics_pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
        graphics_pipeline_create_info.setPViewportState(&viewport_state);
        graphics_pipeline_create_info.setPRasterizationState(&rasterization_state);
        graphics_pipeline_create_info.setPMultisampleState(&multisample_state);
        graphics_pipeline_create_info.setPDepthStencilState(&depth_stencil_state);
        graphics_pipeline_create_info.setPColorBlendState(&color_blend_state);
        graphics_pipeline_create_info.setPDynamicState(&dynamic_state);
        graphics_pipeline_create_info.setLayout(*this->mesh_pass.pipeline_layout);
        this->mesh_pass.pipeline    = vk::raii::Pipeline{*this->device, nullptr, graphics_pipeline_create_info};
        this->mesh_pass.frame_count = this->frame_count;
    }

    std::vector<SceneMesh> RasterizerRenderer::Impl::collect_render_meshes() const {
        const std::shared_ptr<const SceneDocument> scene = this->scene_workspace->document();
        std::vector<SceneMesh> meshes = scene->meshes;
        const std::optional<SceneFrameSnapshot> frame = this->scene_workspace->frame();
        if (!frame.has_value()) return meshes;
        for (const SceneMesh& frame_mesh : frame->meshes) {
            bool replaced = false;
            for (SceneMesh& mesh : meshes) {
                if (mesh.name != frame_mesh.name) continue;
                mesh     = frame_mesh;
                replaced = true;
                break;
            }
            if (!replaced) meshes.push_back(frame_mesh);
        }
        return meshes;
    }

    SceneMaterial RasterizerRenderer::Impl::resolve_material(const std::string_view material_name) const {
        if (material_name.empty()) throw std::runtime_error("Rasterizer mesh material name must not be empty");
        const std::shared_ptr<const SceneDocument> scene = this->scene_workspace->document();
        for (const SceneMaterial& material : scene->materials) {
            if (material.name == material_name) return material;
        }
        throw std::runtime_error(std::format("Rasterizer material \"{}\" does not exist", material_name));
    }

    void RasterizerRenderer::Impl::upload_scene_resources(const std::uint32_t frame_index) {
        if (frame_index >= this->mesh_pass.frame_scenes.size()) throw std::runtime_error("Spectra rasterizer frame scene index is out of range");
        FrameSceneResources& frame_scene = this->mesh_pass.frame_scenes.at(frame_index);
        const SceneRevision scene_revision = this->scene_workspace->revision();
        if (frame_scene.uploadedRevision == scene_revision) return;

        std::vector<RasterizerVertex> vertices{};
        std::vector<std::uint32_t> indices{};
        std::vector<RenderDrawCommand> draw_commands{};
        for (const SceneMesh& mesh : this->collect_render_meshes()) {
            if (mesh.positions.empty()) continue;
            if (mesh.normals.size() != mesh.positions.size()) throw std::runtime_error(std::format("Rasterizer mesh \"{}\" must provide one normal per position", mesh.name));
            if (mesh.indices.empty() || mesh.indices.size() % 3u != 0u) throw std::runtime_error(std::format("Rasterizer mesh \"{}\" must provide triangle indices", mesh.name));
            if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Rasterizer vertex count exceeds uint32 range");
            const std::uint32_t vertex_offset = static_cast<std::uint32_t>(vertices.size());
            vertices.reserve(vertices.size() + mesh.positions.size());
            for (std::size_t vertex_index = 0; vertex_index < mesh.positions.size(); ++vertex_index) {
                const SceneVector3 position = mesh.positions.at(vertex_index);
                const SceneVector3 normal   = mesh.normals.at(vertex_index);
                vertices.push_back(RasterizerVertex{
                    .px = position.x,
                    .py = position.y,
                    .pz = position.z,
                    .nx = normal.x,
                    .ny = normal.y,
                    .nz = normal.z,
                });
            }
            const std::uint32_t first_index = static_cast<std::uint32_t>(indices.size());
            indices.reserve(indices.size() + mesh.indices.size());
            for (const std::uint32_t index : mesh.indices) {
                if (index >= mesh.positions.size()) throw std::runtime_error(std::format("Rasterizer mesh \"{}\" has an out-of-range index", mesh.name));
                indices.push_back(vertex_offset + index);
            }
            draw_commands.push_back(RenderDrawCommand{
                .name       = mesh.name,
                .firstIndex = first_index,
                .indexCount = static_cast<std::uint32_t>(mesh.indices.size()),
                .transform  = mesh.transform,
                .material   = this->resolve_material(mesh.materialName),
            });
        }

        if (!vertices.empty()) {
            const vk::DeviceSize vertex_bytes = static_cast<vk::DeviceSize>(vertices.size() * sizeof(RasterizerVertex));
            const vk::DeviceSize index_bytes  = static_cast<vk::DeviceSize>(indices.size() * sizeof(std::uint32_t));
            this->ensure_host_buffer(frame_scene.vertexBuffer, vertex_bytes, vk::BufferUsageFlagBits::eVertexBuffer);
            this->ensure_host_buffer(frame_scene.indexBuffer, index_bytes, vk::BufferUsageFlagBits::eIndexBuffer);
            std::memcpy(frame_scene.vertexBuffer.mapped, vertices.data(), static_cast<std::size_t>(vertex_bytes));
            std::memcpy(frame_scene.indexBuffer.mapped, indices.data(), static_cast<std::size_t>(index_bytes));
        }
        frame_scene.drawCommands     = std::move(draw_commands);
        frame_scene.uploadedRevision = scene_revision;
    }

    void RasterizerRenderer::Impl::update_camera_uniform(const std::uint32_t frame_index) {
        if (frame_index >= this->mesh_pass.uniform_buffers.size()) throw std::runtime_error("Spectra rasterizer uniform frame index is out of range");
        const std::shared_ptr<const SceneDocument> scene = this->scene_workspace->document();
        const CameraUniformData camera_uniform = make_camera_uniform(*scene, this->viewport.extent);
        std::memcpy(this->mesh_pass.uniform_buffers.at(frame_index).mapped, &camera_uniform, sizeof(camera_uniform));
    }

    RasterizerFrameResult RasterizerRenderer::Impl::begin_frame(RasterizerHostView host, const RasterizerFrameInfo& frame) {
        this->update_host(host.physical_device(), host.device(), host.frame_count(), host.swapchain_extent());
        if (frame.frame_index >= this->frame_count) throw std::runtime_error("Spectra rasterizer frame index is out of range");
        this->active_frame_index = frame.frame_index;
        RasterizerFrameResult result{};
        this->ensure_viewport_resources();
        this->ensure_mesh_resources();
        this->upload_scene_resources(frame.frame_index);
        this->update_camera_uniform(frame.frame_index);
        result.window_detail = this->window_detail();
        return result;
    }

    void RasterizerRenderer::Impl::record_frame(const vk::raii::CommandBuffer& command_buffer) {
        if (!*this->viewport.image) return;
        if (this->active_frame_index >= this->mesh_pass.frame_scenes.size()) throw std::runtime_error("Spectra rasterizer active frame index is out of range");
        FrameSceneResources& frame_scene = this->mesh_pass.frame_scenes.at(this->active_frame_index);

        transition_image_layout(command_buffer, *this->viewport.image, vk::ImageAspectFlagBits::eColor, this->viewport.layout, vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
        this->viewport.layout = vk::ImageLayout::eColorAttachmentOptimal;
        transition_image_layout(command_buffer, *this->viewport.depth_image, vk::ImageAspectFlagBits::eDepth, this->viewport.depth_layout, vk::ImageLayout::eDepthAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
        this->viewport.depth_layout = vk::ImageLayout::eDepthAttachmentOptimal;

        constexpr std::array<float, 4> clear_color{0.06f, 0.065f, 0.062f, 1.0f};
        const vk::ClearValue clear_value{vk::ClearColorValue{clear_color}};
        const vk::ClearValue depth_clear_value{vk::ClearDepthStencilValue{1.0f, 0u}};
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
        const vk::RenderingAttachmentInfo depth_attachment{
            *this->viewport.depth_view,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eDontCare,
            depth_clear_value,
        };
        const vk::RenderingInfo rendering_info{{}, {{0, 0}, this->viewport.extent}, 1, 0, 1, &color_attachment, &depth_attachment, nullptr};
        command_buffer.beginRendering(rendering_info);
        if (!frame_scene.drawCommands.empty()) {
            const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
            const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
            command_buffer.setViewport(0u, viewport);
            command_buffer.setScissor(0u, scissor);
            command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->mesh_pass.pipeline);
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->mesh_pass.pipeline_layout, 0u, *this->mesh_pass.descriptor_sets.at(this->active_frame_index), {});
            const std::array<vk::Buffer, 1> vertex_buffers{*frame_scene.vertexBuffer.buffer};
            constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
            command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
            command_buffer.bindIndexBuffer(*frame_scene.indexBuffer.buffer, 0, vk::IndexType::eUint32);
            for (const RenderDrawCommand& draw_command : frame_scene.drawCommands) {
                const MathMatrix4 model_matrix = transform_matrix(draw_command.transform);
                const DrawPushConstantsData push_constants{
                    .model     = model_matrix.values,
                    .baseColor = {draw_command.material.baseColor.x, draw_command.material.baseColor.y, draw_command.material.baseColor.z, draw_command.material.baseColor.w},
                    .emission  = {draw_command.material.emissionColor.x * draw_command.material.emissionStrength, draw_command.material.emissionColor.y * draw_command.material.emissionStrength, draw_command.material.emissionColor.z * draw_command.material.emissionStrength, 0.0f},
                };
                command_buffer.pushConstants(*this->mesh_pass.pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
                command_buffer.drawIndexed(draw_command.indexCount, 1u, draw_command.firstIndex, 0, 0u);
            }
        }
        command_buffer.endRendering();

        transition_image_layout(command_buffer, *this->viewport.image, vk::ImageAspectFlagBits::eColor, this->viewport.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        this->viewport.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    void RasterizerRenderer::Impl::draw_viewport_window() {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x > 1.0f && available.y > 1.0f) this->ui.requested_extent = vk::Extent2D{static_cast<std::uint32_t>(available.x), static_cast<std::uint32_t>(available.y)};
        if (this->viewport.imgui_descriptor == VK_NULL_HANDLE) return;
        ImGui::Image(reinterpret_cast<ImTextureID>(this->viewport.imgui_descriptor), available, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f});

        const SimulationTimeline timeline = this->scene_workspace->timeline();
        const std::string overlay_text = std::format("{}  frame {}  {:.2f}s  recorded {}", timeline_mode_text(timeline.mode), timeline.cursor.frameIndex, timeline.cursor.timeSeconds, timeline.recordedFrames.size());
        const ImVec2 image_min = ImGui::GetItemRectMin();
        const ImVec2 padding{10.0f, 8.0f};
        const ImVec2 text_size = ImGui::CalcTextSize(overlay_text.c_str());
        const ImVec2 box_min{image_min.x + 12.0f, image_min.y + 12.0f};
        const ImVec2 box_max{box_min.x + text_size.x + padding.x * 2.0f, box_min.y + text_size.y + padding.y * 2.0f};
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(box_min, box_max, IM_COL32(18, 22, 24, 176), 6.0f);
        draw_list->AddText(ImVec2{box_min.x + padding.x, box_min.y + padding.y}, IM_COL32(232, 236, 238, 255), overlay_text.c_str());
    }

    void RasterizerRenderer::Impl::commit_timeline_from_ui(SimulationTimeline timeline) {
        SceneEditBuilder edit{};
        edit.replaceTimeline(std::move(timeline));
        const SceneEditBatch batch = this->scene_workspace->commit(std::move(edit));
        if (!HasSceneDirtyFlag(batch.dirty, SceneDirtyFlags::Timeline)) throw std::runtime_error("Rasterizer timeline UI edit did not mark the timeline dirty");
    }

    void RasterizerRenderer::Impl::draw_rasterizer_window() {
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_NoBordersInBodyUntilResize;
        const std::shared_ptr<const SceneDocument> scene = this->scene_workspace->document();
        SimulationTimeline timeline = this->scene_workspace->timeline();
        bool timeline_changed = false;

        ImGui::TextUnformatted("Rasterizer");
        ImGui::SameLine();
        ImGui::TextDisabled("Simulation");
        ImGui::SeparatorText("Timeline");
        if (ImGui::Button(timeline.playing ? ICON_MS_PAUSE : ICON_MS_PLAY_ARROW)) {
            timeline.playing = !timeline.playing;
            timeline_changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_MS_RESTART_ALT)) {
            ++timeline.resetRequestSerial;
            timeline_changed = true;
        }
        ImGui::SameLine();
        const bool live_selected = timeline.mode == SimulationTimelineMode::Live;
        const bool record_selected = timeline.mode == SimulationTimelineMode::Record;
        const bool playback_selected = timeline.mode == SimulationTimelineMode::Playback;
        if (ImGui::RadioButton("Live", live_selected)) {
            timeline.mode = SimulationTimelineMode::Live;
            timeline_changed = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Record", record_selected)) {
            timeline.mode = SimulationTimelineMode::Record;
            timeline_changed = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Playback", playback_selected)) {
            timeline.mode = SimulationTimelineMode::Playback;
            timeline_changed = true;
        }
        bool loop = timeline.loop;
        if (ImGui::Checkbox("Loop", &loop)) {
            timeline.loop = loop;
            timeline_changed = true;
        }
        if (timeline.recordedFrames.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Rasterizer recorded frame count exceeds ImGui slider range");
        int selected_frame = static_cast<int>(timeline.selectedFrameIndex);
        const int max_frame = timeline.recordedFrames.empty() ? 0 : static_cast<int>(timeline.recordedFrames.size() - 1u);
        if (selected_frame > max_frame) selected_frame = max_frame;
        ImGui::BeginDisabled(timeline.recordedFrames.empty());
        if (ImGui::SliderInt("Playback Frame", &selected_frame, 0, max_frame)) {
            timeline.selectedFrameIndex = static_cast<std::uint64_t>(selected_frame);
            timeline.mode = SimulationTimelineMode::Playback;
            timeline_changed = true;
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(timeline.recordedFrames.empty());
        if (ImGui::Button("Clear Recording")) {
            ++timeline.clearRecordingRequestSerial;
            timeline_changed = true;
        }
        ImGui::EndDisabled();
        if (timeline_changed) this->commit_timeline_from_ui(std::move(timeline));

        const SimulationTimeline current_timeline = this->scene_workspace->timeline();
        ImGui::SeparatorText("Status");
        if (ImGui::BeginTable("SpectraRasterizerStatus", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_status_row("Renderer", "Mesh Pass");
            draw_status_row("Scene", scene->title.empty() ? scene->name : scene->title);
            draw_status_row("Timeline", timeline_mode_text(current_timeline.mode));
            draw_status_row("Frame", std::format("{} / {:.3f}s", current_timeline.cursor.frameIndex, current_timeline.cursor.timeSeconds));
            draw_status_row("Recorded", std::format("{}", current_timeline.recordedFrames.size()));
            draw_status_row("Viewport", std::format("{} x {}", this->viewport.extent.width, this->viewport.extent.height));
            draw_status_row("Swapchain", std::format("{} x {}", this->swapchain_extent.width, this->swapchain_extent.height));
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Scene");
        if (ImGui::BeginTable("SpectraRasterizerScene", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_status_row("Materials", std::format("{}", scene->materials.size()));
            draw_status_row("Lights", std::format("{}", scene->lights.size()));
            draw_status_row("Meshes", std::format("{}", scene->meshes.size()));
            draw_status_row("Particle Sets", std::format("{}", scene->particleSets.size()));
            draw_status_row("Volumes", std::format("{}", scene->volumes.size()));
            draw_status_row("Cloths", std::format("{}", scene->cloths.size()));
            draw_status_row("Rigid Bodies", std::format("{}", scene->rigidBodies.size()));
            draw_status_row("Colliders", std::format("{}", scene->colliders.size()));
            ImGui::EndTable();
        }
    }
} // namespace spectra::rasterizer
