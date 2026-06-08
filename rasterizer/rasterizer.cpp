module;

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <spectra_rasterizer_mesh_fragment_spv.h>
#include <spectra_rasterizer_mesh_vertex_spv.h>
#include <spectra_rasterizer_particle_fragment_spv.h>
#include <spectra_rasterizer_particle_vertex_spv.h>
#include <spectra_rasterizer_volume_fragment_spv.h>
#include <spectra_rasterizer_volume_vertex_spv.h>

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

    struct ParticleInstance {
        float px{};
        float py{};
        float pz{};
        float radius{};
        float r{};
        float g{};
        float b{};
        float a{};
    };

    struct CameraUniformData {
        std::array<float, 16> viewProjection{};
        std::array<float, 4> cameraPosition{};
        std::array<float, 4> lightDirection{};
        std::array<float, 4> lightColorIntensity{};
        std::array<float, 4> cameraRight{};
        std::array<float, 4> cameraUp{};
    };

    struct DrawPushConstantsData {
        std::array<float, 16> model{};
        std::array<float, 4> baseColor{};
        std::array<float, 4> emission{};
    };

    struct VolumePushConstantsData {
        std::array<float, 4> originDensityScale{};
        std::array<float, 4> extentStepScale{};
        std::array<float, 4> baseColor{};
        std::array<float, 4> emission{};
    };

    struct GpuBuffer {
        vk::raii::Buffer buffer{nullptr};
        vk::raii::DeviceMemory memory{nullptr};
        void* mapped{};
        vk::DeviceSize capacity{};
    };

    struct GpuImage3D {
        vk::raii::Image image{nullptr};
        vk::raii::DeviceMemory memory{nullptr};
        vk::raii::ImageView view{nullptr};
        vk::Extent3D extent{};
        vk::ImageLayout layout{vk::ImageLayout::eUndefined};
    };

    struct RenderDrawCommand {
        std::string name{};
        std::uint32_t firstIndex{};
        std::uint32_t indexCount{};
        spectra::rasterizer::SceneTransform transform{};
        spectra::rasterizer::SceneMaterial material{};
    };

    struct ParticleDrawCommand {
        std::uint32_t firstInstance{};
        std::uint32_t instanceCount{};
    };

    struct VolumeDrawCommand {
        spectra::rasterizer::SceneVolumeGrid volume{};
        spectra::rasterizer::SceneMaterial material{};
    };

    struct CameraBasis {
        MathVector3 eye{};
        MathVector3 target{};
        MathVector3 forward{};
        MathVector3 side{};
        MathVector3 up{};
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

    [[nodiscard]] CameraBasis camera_basis(const spectra::rasterizer::SceneCamera& camera) {
        CameraBasis basis{};
        basis.eye     = to_math_vector(camera.transform.position);
        basis.target  = to_math_vector(camera.target);
        basis.forward = normalize_vector(subtract_vector(basis.target, basis.eye));
        constexpr MathVector3 world_up{0.0f, 1.0f, 0.0f};
        basis.side = normalize_vector(cross_vector(basis.forward, world_up));
        basis.up   = cross_vector(basis.side, basis.forward);
        return basis;
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
        CameraBasis basis{};
        basis.eye     = eye;
        basis.target  = target;
        basis.forward = normalize_vector(subtract_vector(basis.target, basis.eye));
        constexpr MathVector3 world_up{0.0f, 1.0f, 0.0f};
        basis.side = normalize_vector(cross_vector(basis.forward, world_up));
        basis.up   = cross_vector(basis.side, basis.forward);
        MathMatrix4 view = identity_matrix();
        view.at(0u, 0u) = basis.side.x;
        view.at(1u, 0u) = basis.side.y;
        view.at(2u, 0u) = basis.side.z;
        view.at(0u, 1u) = basis.up.x;
        view.at(1u, 1u) = basis.up.y;
        view.at(2u, 1u) = basis.up.z;
        view.at(0u, 2u) = -basis.forward.x;
        view.at(1u, 2u) = -basis.forward.y;
        view.at(2u, 2u) = -basis.forward.z;
        view.at(3u, 0u) = -dot_vector(basis.side, eye);
        view.at(3u, 1u) = -dot_vector(basis.up, eye);
        view.at(3u, 2u) = dot_vector(basis.forward, eye);
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

    [[nodiscard]] MathVector3 transform_point(const MathMatrix4& matrix, const MathVector3 point) {
        return MathVector3{
            point.x * matrix.at(0u, 0u) + point.y * matrix.at(1u, 0u) + point.z * matrix.at(2u, 0u) + matrix.at(3u, 0u),
            point.x * matrix.at(0u, 1u) + point.y * matrix.at(1u, 1u) + point.z * matrix.at(2u, 1u) + matrix.at(3u, 1u),
            point.x * matrix.at(0u, 2u) + point.y * matrix.at(1u, 2u) + point.z * matrix.at(2u, 2u) + matrix.at(3u, 2u),
        };
    }

    [[nodiscard]] CameraUniformData make_camera_uniform(const spectra::rasterizer::SceneDocument& scene, const vk::Extent2D extent) {
        if (!scene.camera.has_value()) throw std::runtime_error("Rasterizer mesh rendering requires a scene camera");
        const spectra::rasterizer::SceneCamera& camera = *scene.camera;
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const CameraBasis basis = camera_basis(camera);
        const MathMatrix4 view_projection = multiply_matrix(look_at_matrix(basis.eye, basis.target), perspective_matrix(camera.verticalFovDegrees, aspect, camera.nearPlane, camera.farPlane));

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
            .cameraPosition      = {basis.eye.x, basis.eye.y, basis.eye.z, 0.0f},
            .lightDirection      = {normalized_light_direction.x, normalized_light_direction.y, normalized_light_direction.z, 0.0f},
            .lightColorIntensity = {light_color.x, light_color.y, light_color.z, light_intensity},
            .cameraRight         = {basis.side.x, basis.side.y, basis.side.z, 0.0f},
            .cameraUp            = {basis.up.x, basis.up.y, basis.up.z, 0.0f},
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
    class Renderer::Impl {
    public:
        explicit Impl(std::shared_ptr<SceneWorkspace> scene_workspace);
        ~Impl() noexcept;

        [[nodiscard]] std::string_view name() const;
        void set_scene_workspace(std::shared_ptr<SceneWorkspace> scene_workspace);
        void set_control_panel_extension(std::move_only_function<void()> draw);
        void attach(HostView host);
        void detach() noexcept;
        void before_imgui_shutdown() noexcept;
        void after_imgui_created();
        [[nodiscard]] FrameResult begin_frame(HostView host, const FrameContext& frame);
        void record_frame(const vk::raii::CommandBuffer& command_buffer);

    private:
        struct FrameSceneResources {
            GpuBuffer vertexBuffer{};
            GpuBuffer indexBuffer{};
            SceneRevision uploadedRevision{};
            std::vector<RenderDrawCommand> drawCommands{};
        };

        struct FrameParticleResources {
            GpuBuffer instanceBuffer{};
            SceneRevision uploadedRevision{};
            std::vector<ParticleDrawCommand> drawCommands{};
        };

        struct FrameVolumeResources {
            GpuBuffer densityStagingBuffer{};
            GpuBuffer temperatureStagingBuffer{};
            GpuImage3D densityImage{};
            GpuImage3D temperatureImage{};
            SceneRevision uploadedRevision{};
            bool uploadPending{};
            bool descriptorValid{};
            VolumeDrawCommand drawCommand{};
        };

        void update_host(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, std::uint32_t frame_count, vk::Extent2D swapchain_extent);
        void wait_device_idle_for_cleanup() noexcept;
        void register_workspace_contributions(HostView& host);
        [[nodiscard]] std::string window_detail() const;

        void create_viewport_resources(vk::Extent2D extent);
        void ensure_viewport_resources();
        void destroy_viewport_resources() noexcept;
        void create_imgui_descriptor();
        void destroy_imgui_descriptor() noexcept;

        void ensure_mesh_resources();
        void destroy_mesh_resources() noexcept;
        void ensure_particle_resources();
        void destroy_particle_resources() noexcept;
        void ensure_volume_resources();
        void destroy_volume_resources() noexcept;
        void ensure_host_buffer(GpuBuffer& buffer, vk::DeviceSize required_size, vk::BufferUsageFlags usage);
        void destroy_host_buffer(GpuBuffer& buffer) noexcept;
        void create_volume_image(GpuImage3D& image, vk::Extent3D extent);
        void destroy_volume_image(GpuImage3D& image) noexcept;

        [[nodiscard]] std::vector<SceneMesh> collect_render_meshes() const;
        [[nodiscard]] std::vector<SceneParticleSet> collect_render_particle_sets() const;
        [[nodiscard]] std::vector<SceneVolumeGrid> collect_render_volumes() const;
        [[nodiscard]] SceneMaterial resolve_material(std::string_view material_name) const;
        [[nodiscard]] const SceneVolumeChannel& require_volume_channel(const SceneVolumeGrid& volume, std::string_view channel_name, SceneVolumeChannelLayout layout) const;
        [[nodiscard]] const SceneVolumeGrid* select_render_volume_grid(const std::vector<SceneVolumeGrid>& volumes) const;

        void upload_scene_resources(std::uint32_t frame_index);
        void upload_particle_resources(std::uint32_t frame_index);
        void upload_volume_resources(std::uint32_t frame_index);
        void update_camera_uniform(std::uint32_t frame_index);
        void record_pending_volume_upload(const vk::raii::CommandBuffer& command_buffer, FrameVolumeResources& frame_volume);

        void record_mesh_pass(const vk::raii::CommandBuffer& command_buffer);
        void record_particle_pass(const vk::raii::CommandBuffer& command_buffer);
        void record_volume_pass(const vk::raii::CommandBuffer& command_buffer);

        void draw_viewport_window();
        void draw_rasterizer_window();
        void commit_timeline_from_ui(SimulationTimeline timeline);
        [[nodiscard]] bool timeline_playing() const;
        void toggle_timeline_playback();
        void request_timeline_reset();

        struct {
            const vk::raii::PhysicalDevice* physical_device{};
            const vk::raii::Device* device{};
            vk::Extent2D swapchain_extent{};
            std::uint32_t frame_count{};
        } host;

        struct {
            std::uint32_t active_frame_index{};
            bool attached{false};
            bool imgui_ready{false};
        } lifecycle;

        struct {
            std::shared_ptr<SceneWorkspace> workspace{};
        } scene;

        struct {
            vk::Extent2D requested_extent{};
            std::move_only_function<void()> control_panel_extension{};
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

        struct {
            std::uint32_t frame_count{};
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::Pipeline pipeline{nullptr};
            std::vector<FrameParticleResources> frame_particles{};
        } particle_pass;

        struct {
            std::uint32_t frame_count{};
            vk::raii::DescriptorSetLayout descriptor_set_layout{nullptr};
            vk::raii::DescriptorPool descriptor_pool{nullptr};
            vk::raii::DescriptorSets descriptor_sets{nullptr};
            vk::raii::Sampler sampler{nullptr};
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::Pipeline pipeline{nullptr};
            std::vector<FrameVolumeResources> frame_volumes{};
        } volume_pass;
    };

    Renderer::Renderer(std::shared_ptr<SceneWorkspace> scene_workspace) : impl(std::make_unique<Impl>(std::move(scene_workspace))) {}

    Renderer::~Renderer() noexcept = default;

    Renderer::Renderer(Renderer&& other) noexcept = default;

    Renderer& Renderer::operator=(Renderer&& other) noexcept = default;

    std::string_view Renderer::target_name() {
        return "Spectra Rasterizer";
    }

    std::string_view Renderer::name() const {
        return this->impl->name();
    }

    void Renderer::set_scene_workspace(std::shared_ptr<SceneWorkspace> scene_workspace) {
        this->set_scene_workspace_impl(std::move(scene_workspace));
    }

    void Renderer::set_control_panel_extension(std::move_only_function<void()> draw) {
        this->set_control_panel_extension_impl(std::move(draw));
    }

    void Renderer::attach(HostView host) {
        this->impl->attach(std::move(host));
    }

    void Renderer::detach() noexcept {
        this->impl->detach();
    }

    void Renderer::before_imgui_shutdown() noexcept {
        this->impl->before_imgui_shutdown();
    }

    void Renderer::after_imgui_created() {
        this->impl->after_imgui_created();
    }

    void Renderer::set_scene_workspace_impl(std::shared_ptr<SceneWorkspace> scene_workspace) {
        this->impl->set_scene_workspace(std::move(scene_workspace));
    }

    void Renderer::set_control_panel_extension_impl(std::move_only_function<void()> draw) {
        this->impl->set_control_panel_extension(std::move(draw));
    }

    FrameResult Renderer::begin_frame(HostView host, const FrameContext& frame) {
        return this->impl->begin_frame(std::move(host), frame);
    }

    void Renderer::record_frame(const vk::raii::CommandBuffer& command_buffer) {
        this->impl->record_frame(command_buffer);
    }

    Renderer::Impl::Impl(std::shared_ptr<SceneWorkspace> scene_workspace) {
        this->scene.workspace = std::move(scene_workspace);
        if (this->scene.workspace == nullptr) throw std::runtime_error("Spectra rasterizer requires a scene workspace");
        if (!this->scene.workspace->loaded()) throw std::runtime_error("Spectra rasterizer requires a loaded scene workspace");
    }

    Renderer::Impl::~Impl() noexcept = default;

    std::string_view Renderer::Impl::name() const {
        return "Spectra Rasterizer";
    }

    void Renderer::Impl::set_scene_workspace(std::shared_ptr<SceneWorkspace> scene_workspace) {
        if (scene_workspace == nullptr) throw std::runtime_error("Spectra rasterizer scene workspace must not be null");
        if (!scene_workspace->loaded()) throw std::runtime_error("Spectra rasterizer scene workspace must be loaded");
        if (this->host.device != nullptr) this->host.device->waitIdle();
        this->destroy_mesh_resources();
        this->destroy_particle_resources();
        this->destroy_volume_resources();
        this->scene.workspace = std::move(scene_workspace);
    }

    void Renderer::Impl::set_control_panel_extension(std::move_only_function<void()> draw) {
        this->ui.control_panel_extension = std::move(draw);
    }

    void Renderer::Impl::attach(HostView host) {
        if (this->lifecycle.attached) throw std::runtime_error("Spectra rasterizer plugin is already attached");
        this->update_host(host.physical_device(), host.device(), host.frame_count(), host.swapchain_extent());
        this->register_workspace_contributions(host);
        this->lifecycle.attached = true;
    }

    void Renderer::Impl::detach() noexcept {
        this->destroy_viewport_resources();
        this->destroy_mesh_resources();
        this->destroy_particle_resources();
        this->destroy_volume_resources();
        this->host.physical_device  = nullptr;
        this->host.device           = nullptr;
        this->host.swapchain_extent = vk::Extent2D{};
        this->host.frame_count      = 0;
        this->lifecycle.attached    = false;
        this->lifecycle.imgui_ready = false;
    }

    void Renderer::Impl::before_imgui_shutdown() noexcept {
        this->destroy_imgui_descriptor();
        this->lifecycle.imgui_ready = false;
    }

    void Renderer::Impl::after_imgui_created() {
        this->lifecycle.imgui_ready = true;
        if (*this->viewport.image && this->viewport.imgui_descriptor == VK_NULL_HANDLE) this->create_imgui_descriptor();
    }

    void Renderer::Impl::update_host(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count, const vk::Extent2D swapchain_extent) {
        if (frame_count == 0) throw std::runtime_error("Spectra rasterizer host frame count must be positive");
        if (swapchain_extent.width == 0 || swapchain_extent.height == 0) throw std::runtime_error("Spectra rasterizer host swapchain extent must be positive");
        this->host.physical_device  = &physical_device;
        this->host.device           = &device;
        this->host.swapchain_extent = swapchain_extent;
        this->host.frame_count      = frame_count;
        if (this->ui.requested_extent.width == 0 || this->ui.requested_extent.height == 0) this->ui.requested_extent = swapchain_extent;
    }

    void Renderer::Impl::wait_device_idle_for_cleanup() noexcept {
        try {
            if (this->host.device != nullptr) this->host.device->waitIdle();
        } catch (...) {
        }
    }

    void Renderer::Impl::register_workspace_contributions(HostView& host) {
        host.register_panel(Panel{
            .id                  = "rasterizer.viewport",
            .title               = "Rasterizer Viewport",
            .icon                = ICON_MS_GRID_VIEW,
            .shortcut_label      = "F7",
            .shortcut_key        = ImGuiKey_F7,
            .dock_slot           = DockSlot::Center,
            .window_flags        = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse,
            .closable            = false,
            .zero_window_padding = true,
            .draw                = [this] { this->draw_viewport_window(); },
        });
        host.register_sidebar_tab(SidebarTab{
            .id             = "rasterizer.panel",
            .title          = "Rasterizer",
            .icon           = ICON_MS_TUNE,
            .shortcut_label = "F8",
            .shortcut_key   = ImGuiKey_F8,
            .draw           = [this] { this->draw_rasterizer_window(); },
        });
        host.register_toolbar_action(ToolbarAction{
            .id             = "rasterizer.timeline.play_pause",
            .title          = "Play / Pause",
            .icon           = ICON_MS_PLAY_PAUSE,
            .shortcut_label = "Space",
            .shortcut_key   = ImGuiKey_Space,
            .active         = [this] { return this->timeline_playing(); },
            .trigger        = [this] { this->toggle_timeline_playback(); },
        });
        host.register_toolbar_action(ToolbarAction{
            .id             = "rasterizer.timeline.reset",
            .title          = "Reset",
            .icon           = ICON_MS_RESTART_ALT,
            .shortcut_label = "R",
            .shortcut_key   = ImGuiKey_R,
            .active         = [] { return false; },
            .trigger        = [this] { this->request_timeline_reset(); },
        });
    }

    std::string Renderer::Impl::window_detail() const {
        const std::uint32_t width  = this->viewport.extent.width != 0 ? this->viewport.extent.width : this->host.swapchain_extent.width;
        const std::uint32_t height = this->viewport.extent.height != 0 ? this->viewport.extent.height : this->host.swapchain_extent.height;
        const std::shared_ptr<const SceneDocument> scene = this->scene.workspace->document();
        const SimulationTimeline timeline = this->scene.workspace->timeline();
        return std::format("{} | {} | {}x{}", scene->title.empty() ? scene->name : scene->title, timeline_mode_text(timeline.mode), width, height);
    }

    void Renderer::Impl::create_viewport_resources(const vk::Extent2D extent) {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer viewport without Vulkan handles");
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
        this->viewport.image                             = vk::raii::Image{*this->host.device, image_create_info};
        const vk::MemoryRequirements memory_requirements = this->viewport.image.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(*this->host.physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        this->viewport.memory = vk::raii::DeviceMemory{*this->host.device, memory_allocate_info};
        this->viewport.image.bindMemory(*this->viewport.memory, 0);

        const vk::ImageViewCreateInfo image_view_create_info{{}, *this->viewport.image, vk::ImageViewType::e2D, this->viewport.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        this->viewport.view = vk::raii::ImageView{*this->host.device, image_view_create_info};
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
        this->viewport.sampler = vk::raii::Sampler{*this->host.device, sampler_create_info};

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
        this->viewport.depth_image                             = vk::raii::Image{*this->host.device, depth_image_create_info};
        const vk::MemoryRequirements depth_memory_requirements = this->viewport.depth_image.getMemoryRequirements();
        const std::uint32_t depth_memory_type                  = find_memory_type_index(*this->host.physical_device, depth_memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo depth_memory_allocate_info{depth_memory_requirements.size, depth_memory_type};
        this->viewport.depth_memory = vk::raii::DeviceMemory{*this->host.device, depth_memory_allocate_info};
        this->viewport.depth_image.bindMemory(*this->viewport.depth_memory, 0);
        const vk::ImageViewCreateInfo depth_view_create_info{{}, *this->viewport.depth_image, vk::ImageViewType::e2D, this->viewport.depth_format, {}, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}};
        this->viewport.depth_view = vk::raii::ImageView{*this->host.device, depth_view_create_info};

        this->viewport.extent       = extent;
        this->viewport.layout       = vk::ImageLayout::eUndefined;
        this->viewport.depth_layout = vk::ImageLayout::eUndefined;
        if (this->lifecycle.imgui_ready) this->create_imgui_descriptor();
    }

    void Renderer::Impl::destroy_imgui_descriptor() noexcept {
        if (this->viewport.imgui_descriptor == VK_NULL_HANDLE) return;
        this->wait_device_idle_for_cleanup();
        ImGui_ImplVulkan_RemoveTexture(this->viewport.imgui_descriptor);
        this->viewport.imgui_descriptor = VK_NULL_HANDLE;
    }

    void Renderer::Impl::destroy_viewport_resources() noexcept {
        this->destroy_imgui_descriptor();
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

    void Renderer::Impl::ensure_viewport_resources() {
        if (this->ui.requested_extent.width == 0 || this->ui.requested_extent.height == 0) return;
        if (*this->viewport.image && this->viewport.extent.width == this->ui.requested_extent.width && this->viewport.extent.height == this->ui.requested_extent.height) return;
        this->destroy_viewport_resources();
        this->create_viewport_resources(this->ui.requested_extent);
    }

    void Renderer::Impl::create_imgui_descriptor() {
        if (!*this->viewport.sampler || !*this->viewport.view) throw std::runtime_error("Cannot create Spectra rasterizer descriptor before viewport resources exist");
        if (this->viewport.imgui_descriptor != VK_NULL_HANDLE) throw std::runtime_error("Spectra rasterizer viewport descriptor is already allocated");
        this->viewport.imgui_descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(*this->viewport.sampler), static_cast<VkImageView>(*this->viewport.view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (this->viewport.imgui_descriptor == VK_NULL_HANDLE) throw std::runtime_error("Failed to allocate Spectra rasterizer viewport descriptor");
    }

    void Renderer::Impl::destroy_host_buffer(GpuBuffer& buffer) noexcept {
        if (buffer.mapped != nullptr && this->host.device != nullptr && *buffer.memory) vkUnmapMemory(static_cast<VkDevice>(**this->host.device), static_cast<VkDeviceMemory>(*buffer.memory));
        buffer.mapped   = nullptr;
        buffer.buffer   = nullptr;
        buffer.memory   = nullptr;
        buffer.capacity = 0;
    }

    void Renderer::Impl::ensure_host_buffer(GpuBuffer& buffer, const vk::DeviceSize required_size, const vk::BufferUsageFlags usage) {
        if (required_size == 0) throw std::runtime_error("Cannot allocate an empty Spectra rasterizer buffer");
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot allocate Spectra rasterizer buffers without Vulkan handles");
        if (*buffer.buffer && buffer.capacity >= required_size) return;
        this->destroy_host_buffer(buffer);
        const vk::BufferCreateInfo buffer_create_info{{}, required_size, usage, vk::SharingMode::eExclusive};
        buffer.buffer = vk::raii::Buffer{*this->host.device, buffer_create_info};
        const vk::MemoryRequirements memory_requirements = buffer.buffer.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(*this->host.physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        const vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        buffer.memory = vk::raii::DeviceMemory{*this->host.device, memory_allocate_info};
        buffer.buffer.bindMemory(*buffer.memory, 0);
        if (vkMapMemory(static_cast<VkDevice>(**this->host.device), static_cast<VkDeviceMemory>(*buffer.memory), 0, required_size, 0, &buffer.mapped) != VK_SUCCESS) throw std::runtime_error("Failed to map Spectra rasterizer buffer memory");
        buffer.capacity = required_size;
        if (buffer.mapped == nullptr) throw std::runtime_error("Failed to map Spectra rasterizer buffer memory");
    }

    void Renderer::Impl::create_volume_image(GpuImage3D& image, const vk::Extent3D extent) {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer volume image without Vulkan handles");
        if (extent.width == 0 || extent.height == 0 || extent.depth == 0) throw std::runtime_error("Cannot create Spectra rasterizer volume image with zero dimensions");
        this->destroy_volume_image(image);
        const vk::ImageCreateInfo image_create_info{
            {},
            vk::ImageType::e3D,
            vk::Format::eR32Sfloat,
            extent,
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::SharingMode::eExclusive,
            0,
            nullptr,
            vk::ImageLayout::eUndefined,
        };
        image.image                                      = vk::raii::Image{*this->host.device, image_create_info};
        const vk::MemoryRequirements memory_requirements = image.image.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(*this->host.physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        image.memory = vk::raii::DeviceMemory{*this->host.device, memory_allocate_info};
        image.image.bindMemory(*image.memory, 0);
        const vk::ImageViewCreateInfo image_view_create_info{{}, *image.image, vk::ImageViewType::e3D, vk::Format::eR32Sfloat, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        image.view   = vk::raii::ImageView{*this->host.device, image_view_create_info};
        image.extent = extent;
        image.layout = vk::ImageLayout::eUndefined;
    }

    void Renderer::Impl::destroy_volume_image(GpuImage3D& image) noexcept {
        image.view   = nullptr;
        image.image  = nullptr;
        image.memory = nullptr;
        image.extent = vk::Extent3D{};
        image.layout = vk::ImageLayout::eUndefined;
    }

    void Renderer::Impl::destroy_mesh_resources() noexcept {
        this->wait_device_idle_for_cleanup();
        for (GpuBuffer& uniform_buffer : this->mesh_pass.uniform_buffers) this->destroy_host_buffer(uniform_buffer);
        for (FrameSceneResources& frame_scene : this->mesh_pass.frame_scenes) {
            this->destroy_host_buffer(frame_scene.vertexBuffer);
            this->destroy_host_buffer(frame_scene.indexBuffer);
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

    void Renderer::Impl::destroy_particle_resources() noexcept {
        this->wait_device_idle_for_cleanup();
        for (FrameParticleResources& frame_particles : this->particle_pass.frame_particles) this->destroy_host_buffer(frame_particles.instanceBuffer);
        this->particle_pass.frame_particles.clear();
        this->particle_pass.pipeline        = nullptr;
        this->particle_pass.pipeline_layout = nullptr;
        this->particle_pass.frame_count     = 0;
    }

    void Renderer::Impl::destroy_volume_resources() noexcept {
        this->wait_device_idle_for_cleanup();
        for (FrameVolumeResources& frame_volume : this->volume_pass.frame_volumes) {
            this->destroy_host_buffer(frame_volume.densityStagingBuffer);
            this->destroy_host_buffer(frame_volume.temperatureStagingBuffer);
            this->destroy_volume_image(frame_volume.densityImage);
            this->destroy_volume_image(frame_volume.temperatureImage);
        }
        this->volume_pass.frame_volumes.clear();
        this->volume_pass.pipeline              = nullptr;
        this->volume_pass.pipeline_layout       = nullptr;
        this->volume_pass.sampler               = nullptr;
        this->volume_pass.descriptor_sets       = nullptr;
        this->volume_pass.descriptor_pool       = nullptr;
        this->volume_pass.descriptor_set_layout = nullptr;
        this->volume_pass.frame_count           = 0;
    }

    void Renderer::Impl::ensure_mesh_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer mesh resources without Vulkan handles");
        if (this->host.frame_count == 0) throw std::runtime_error("Spectra rasterizer frame count must be positive");
        if (*this->mesh_pass.pipeline && this->mesh_pass.frame_count == this->host.frame_count) return;
        this->destroy_mesh_resources();

        const vk::DescriptorSetLayoutBinding camera_binding{0u, vk::DescriptorType::eUniformBuffer, 1u, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment};
        const vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{{}, 1u, &camera_binding};
        this->mesh_pass.descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->host.device, descriptor_set_layout_create_info};

        const vk::DescriptorPoolSize descriptor_pool_size{vk::DescriptorType::eUniformBuffer, this->host.frame_count};
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, this->host.frame_count, 1u, &descriptor_pool_size};
        this->mesh_pass.descriptor_pool = vk::raii::DescriptorPool{*this->host.device, descriptor_pool_create_info};
        const vk::DescriptorSetLayout descriptor_set_layout = *this->mesh_pass.descriptor_set_layout;
        std::vector<vk::DescriptorSetLayout> descriptor_set_layouts(this->host.frame_count, descriptor_set_layout);
        const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*this->mesh_pass.descriptor_pool, this->host.frame_count, descriptor_set_layouts.data()};
        this->mesh_pass.descriptor_sets = vk::raii::DescriptorSets{*this->host.device, descriptor_set_allocate_info};
        if (this->mesh_pass.descriptor_sets.size() != this->host.frame_count) throw std::runtime_error("Failed to allocate Spectra rasterizer descriptor sets");

        this->mesh_pass.uniform_buffers.resize(this->host.frame_count);
        this->mesh_pass.frame_scenes.resize(this->host.frame_count);
        for (GpuBuffer& uniform_buffer : this->mesh_pass.uniform_buffers) this->ensure_host_buffer(uniform_buffer, sizeof(CameraUniformData), vk::BufferUsageFlagBits::eUniformBuffer);

        std::vector<vk::DescriptorBufferInfo> buffer_infos{};
        std::vector<vk::WriteDescriptorSet> descriptor_writes{};
        buffer_infos.reserve(this->host.frame_count);
        descriptor_writes.reserve(this->host.frame_count);
        for (std::uint32_t frame_index = 0; frame_index < this->host.frame_count; ++frame_index) {
            buffer_infos.emplace_back(*this->mesh_pass.uniform_buffers.at(frame_index).buffer, 0, sizeof(CameraUniformData));
            descriptor_writes.emplace_back(*this->mesh_pass.descriptor_sets.at(frame_index), 0u, 0u, 1u, vk::DescriptorType::eUniformBuffer, nullptr, &buffer_infos.back(), nullptr);
        }
        this->host.device->updateDescriptorSets(descriptor_writes, {});

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_mesh_vertex_spv_sizeInBytes, spectra_rasterizer_mesh_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_mesh_fragment_spv_sizeInBytes, spectra_rasterizer_mesh_fragment_spv};
        const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
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
        this->mesh_pass.pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};
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
        this->mesh_pass.pipeline    = vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        this->mesh_pass.frame_count = this->host.frame_count;
    }

    void Renderer::Impl::ensure_particle_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer particle resources without Vulkan handles");
        if (!*this->mesh_pass.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer particle pass requires mesh camera descriptors");
        if (*this->particle_pass.pipeline && this->particle_pass.frame_count == this->host.frame_count) return;
        this->destroy_particle_resources();

        const vk::DescriptorSetLayout descriptor_set_layout = *this->mesh_pass.descriptor_set_layout;
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1u, &descriptor_set_layout, 0u, nullptr};
        this->particle_pass.pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_particle_vertex_spv_sizeInBytes, spectra_rasterizer_particle_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_particle_fragment_spv_sizeInBytes, spectra_rasterizer_particle_fragment_spv};
        const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        const vk::VertexInputBindingDescription vertex_binding{0u, sizeof(ParticleInstance), vk::VertexInputRate::eInstance};
        const std::array vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(ParticleInstance, px))},
            vk::VertexInputAttributeDescription{1u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(ParticleInstance, r))},
        };
        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1u, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOne,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
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
        graphics_pipeline_create_info.setLayout(*this->particle_pass.pipeline_layout);
        this->particle_pass.pipeline    = vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        this->particle_pass.frame_count = this->host.frame_count;
        this->particle_pass.frame_particles.resize(this->host.frame_count);
    }

    void Renderer::Impl::ensure_volume_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer volume resources without Vulkan handles");
        if (*this->volume_pass.pipeline && this->volume_pass.frame_count == this->host.frame_count) return;
        this->destroy_volume_resources();

        const std::array descriptor_bindings{
            vk::DescriptorSetLayoutBinding{0u, vk::DescriptorType::eUniformBuffer, 1u, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{1u, vk::DescriptorType::eSampledImage, 1u, vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{2u, vk::DescriptorType::eSampledImage, 1u, vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{3u, vk::DescriptorType::eSampler, 1u, vk::ShaderStageFlagBits::eFragment},
        };
        const vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{{}, static_cast<std::uint32_t>(descriptor_bindings.size()), descriptor_bindings.data()};
        this->volume_pass.descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->host.device, descriptor_set_layout_create_info};
        const std::array descriptor_pool_sizes{
            vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, this->host.frame_count},
            vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, this->host.frame_count * 2u},
            vk::DescriptorPoolSize{vk::DescriptorType::eSampler, this->host.frame_count},
        };
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, this->host.frame_count, static_cast<std::uint32_t>(descriptor_pool_sizes.size()), descriptor_pool_sizes.data()};
        this->volume_pass.descriptor_pool = vk::raii::DescriptorPool{*this->host.device, descriptor_pool_create_info};
        const vk::DescriptorSetLayout descriptor_set_layout = *this->volume_pass.descriptor_set_layout;
        std::vector<vk::DescriptorSetLayout> descriptor_set_layouts(this->host.frame_count, descriptor_set_layout);
        const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*this->volume_pass.descriptor_pool, this->host.frame_count, descriptor_set_layouts.data()};
        this->volume_pass.descriptor_sets = vk::raii::DescriptorSets{*this->host.device, descriptor_set_allocate_info};
        if (this->volume_pass.descriptor_sets.size() != this->host.frame_count) throw std::runtime_error("Failed to allocate Spectra rasterizer volume descriptor sets");

        const vk::SamplerCreateInfo sampler_create_info{
            {},
            vk::Filter::eLinear,
            vk::Filter::eLinear,
            vk::SamplerMipmapMode::eLinear,
            vk::SamplerAddressMode::eClampToBorder,
            vk::SamplerAddressMode::eClampToBorder,
            vk::SamplerAddressMode::eClampToBorder,
            0.0f,
            VK_FALSE,
            1.0f,
            VK_FALSE,
            vk::CompareOp::eAlways,
            0.0f,
            0.0f,
            vk::BorderColor::eFloatTransparentBlack,
            VK_FALSE,
        };
        this->volume_pass.sampler = vk::raii::Sampler{*this->host.device, sampler_create_info};

        const vk::DescriptorSetLayout volume_descriptor_set_layout = *this->volume_pass.descriptor_set_layout;
        const vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(VolumePushConstantsData)};
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1u, &volume_descriptor_set_layout, 1u, &push_constant_range};
        this->volume_pass.pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_volume_vertex_spv_sizeInBytes, spectra_rasterizer_volume_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_volume_fragment_spv_sizeInBytes, spectra_rasterizer_volume_fragment_spv};
        const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
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
        graphics_pipeline_create_info.setLayout(*this->volume_pass.pipeline_layout);
        this->volume_pass.pipeline    = vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        this->volume_pass.frame_count = this->host.frame_count;
        this->volume_pass.frame_volumes.resize(this->host.frame_count);
    }

    std::vector<SceneMesh> Renderer::Impl::collect_render_meshes() const {
        const std::shared_ptr<const SceneDocument> scene = this->scene.workspace->document();
        std::vector<SceneMesh> meshes = scene->meshes;
        const std::optional<SceneFrameSnapshot> frame = this->scene.workspace->frame();
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

    std::vector<SceneParticleSet> Renderer::Impl::collect_render_particle_sets() const {
        const std::shared_ptr<const SceneDocument> scene = this->scene.workspace->document();
        std::vector<SceneParticleSet> particle_sets = scene->particleSets;
        const std::optional<SceneFrameSnapshot> frame = this->scene.workspace->frame();
        if (!frame.has_value()) return particle_sets;
        for (const SceneParticleSet& frame_particle_set : frame->particleSets) {
            bool replaced = false;
            for (SceneParticleSet& particle_set : particle_sets) {
                if (particle_set.name != frame_particle_set.name) continue;
                particle_set = frame_particle_set;
                replaced     = true;
                break;
            }
            if (!replaced) particle_sets.push_back(frame_particle_set);
        }
        return particle_sets;
    }

    std::vector<SceneVolumeGrid> Renderer::Impl::collect_render_volumes() const {
        const std::shared_ptr<const SceneDocument> scene = this->scene.workspace->document();
        std::vector<SceneVolumeGrid> volumes = scene->volumes;
        const std::optional<SceneFrameSnapshot> frame = this->scene.workspace->frame();
        if (!frame.has_value()) return volumes;
        for (const SceneVolumeGrid& frame_volume : frame->volumes) {
            bool replaced = false;
            for (SceneVolumeGrid& volume : volumes) {
                if (volume.name != frame_volume.name) continue;
                volume   = frame_volume;
                replaced = true;
                break;
            }
            if (!replaced) volumes.push_back(frame_volume);
        }
        return volumes;
    }

    SceneMaterial Renderer::Impl::resolve_material(const std::string_view material_name) const {
        if (material_name.empty()) throw std::runtime_error("Rasterizer material name must not be empty");
        const std::shared_ptr<const SceneDocument> scene = this->scene.workspace->document();
        for (const SceneMaterial& material : scene->materials) {
            if (material.name == material_name) return material;
        }
        throw std::runtime_error(std::format("Rasterizer material \"{}\" does not exist", material_name));
    }

    const SceneVolumeChannel& Renderer::Impl::require_volume_channel(const SceneVolumeGrid& volume, const std::string_view channel_name, const SceneVolumeChannelLayout layout) const {
        for (const SceneVolumeChannel& channel : volume.channels) {
            if (channel.name != channel_name) continue;
            if (channel.layout != layout) throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" has the wrong layout", channel.name));
            const std::uint64_t expected_count = static_cast<std::uint64_t>(channel.dimensions[0]) * static_cast<std::uint64_t>(channel.dimensions[1]) * static_cast<std::uint64_t>(channel.dimensions[2]);
            if (expected_count == 0u) throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" has zero dimensions", channel.name));
            if (expected_count != channel.values.size()) throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" value count does not match dimensions", channel.name));
            return channel;
        }
        throw std::runtime_error(std::format("Rasterizer volume \"{}\" does not contain required channel \"{}\"", volume.name, channel_name));
    }

    const SceneVolumeGrid* Renderer::Impl::select_render_volume_grid(const std::vector<SceneVolumeGrid>& volumes) const {
        if (volumes.empty()) return nullptr;
        if (volumes.size() != 1u) throw std::runtime_error("Spectra rasterizer first-stage volume pass supports exactly one volume grid");
        return &volumes.front();
    }

    void Renderer::Impl::upload_scene_resources(const std::uint32_t frame_index) {
        if (frame_index >= this->mesh_pass.frame_scenes.size()) throw std::runtime_error("Spectra rasterizer frame scene index is out of range");
        FrameSceneResources& frame_scene = this->mesh_pass.frame_scenes.at(frame_index);
        const SceneRevision scene_revision = this->scene.workspace->revision();
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

    void Renderer::Impl::upload_particle_resources(const std::uint32_t frame_index) {
        if (frame_index >= this->particle_pass.frame_particles.size()) throw std::runtime_error("Spectra rasterizer particle frame index is out of range");
        FrameParticleResources& frame_particles = this->particle_pass.frame_particles.at(frame_index);
        const SceneRevision scene_revision = this->scene.workspace->revision();
        if (frame_particles.uploadedRevision == scene_revision) return;

        std::vector<ParticleInstance> instances{};
        std::vector<ParticleDrawCommand> draw_commands{};
        for (const SceneParticleSet& particle_set : this->collect_render_particle_sets()) {
            if (particle_set.positions.empty()) continue;
            if (particle_set.radii.size() != particle_set.positions.size()) throw std::runtime_error(std::format("Rasterizer particle set \"{}\" must provide one radius per position", particle_set.name));
            if (particle_set.colors.size() != particle_set.positions.size()) throw std::runtime_error(std::format("Rasterizer particle set \"{}\" must provide one color per position", particle_set.name));
            if (instances.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Rasterizer particle instance count exceeds uint32 range");
            const std::uint32_t first_instance = static_cast<std::uint32_t>(instances.size());
            const MathMatrix4 transform = transform_matrix(particle_set.transform);
            instances.reserve(instances.size() + particle_set.positions.size());
            for (std::size_t particle_index = 0; particle_index < particle_set.positions.size(); ++particle_index) {
                if (!std::isfinite(particle_set.radii.at(particle_index)) || particle_set.radii.at(particle_index) <= 0.0f) throw std::runtime_error(std::format("Rasterizer particle set \"{}\" contains an invalid radius", particle_set.name));
                const MathVector3 position = transform_point(transform, to_math_vector(particle_set.positions.at(particle_index)));
                const SceneVector4 color = particle_set.colors.at(particle_index);
                instances.push_back(ParticleInstance{
                    .px = position.x,
                    .py = position.y,
                    .pz = position.z,
                    .radius = particle_set.radii.at(particle_index),
                    .r = color.x,
                    .g = color.y,
                    .b = color.z,
                    .a = color.w,
                });
            }
            static_cast<void>(this->resolve_material(particle_set.materialName));
            draw_commands.push_back(ParticleDrawCommand{
                .firstInstance = first_instance,
                .instanceCount = static_cast<std::uint32_t>(particle_set.positions.size()),
            });
        }

        frame_particles.drawCommands = std::move(draw_commands);
        if (!instances.empty()) {
            const vk::DeviceSize instance_bytes = static_cast<vk::DeviceSize>(instances.size() * sizeof(ParticleInstance));
            this->ensure_host_buffer(frame_particles.instanceBuffer, instance_bytes, vk::BufferUsageFlagBits::eVertexBuffer);
            std::memcpy(frame_particles.instanceBuffer.mapped, instances.data(), static_cast<std::size_t>(instance_bytes));
        }
        frame_particles.uploadedRevision = scene_revision;
    }

    void Renderer::Impl::upload_volume_resources(const std::uint32_t frame_index) {
        if (frame_index >= this->volume_pass.frame_volumes.size()) throw std::runtime_error("Spectra rasterizer volume frame index is out of range");
        FrameVolumeResources& frame_volume = this->volume_pass.frame_volumes.at(frame_index);
        const SceneRevision scene_revision = this->scene.workspace->revision();
        if (frame_volume.uploadedRevision == scene_revision) return;

        const std::vector<SceneVolumeGrid> volumes = this->collect_render_volumes();
        const SceneVolumeGrid* selected_volume = this->select_render_volume_grid(volumes);
        if (selected_volume == nullptr) {
            frame_volume.uploadedRevision = scene_revision;
            frame_volume.uploadPending    = false;
            frame_volume.descriptorValid  = false;
            frame_volume.drawCommand      = VolumeDrawCommand{};
            return;
        }
        const SceneVolumeGrid& volume = *selected_volume;
        if (volume.dimensions[0] == 0 || volume.dimensions[1] == 0 || volume.dimensions[2] == 0) throw std::runtime_error(std::format("Rasterizer volume \"{}\" has zero dimensions", volume.name));
        const SceneVolumeChannel& density_channel = this->require_volume_channel(volume, "density", SceneVolumeChannelLayout::CellCentered);
        const SceneVolumeChannel& temperature_channel = this->require_volume_channel(volume, "temperature", SceneVolumeChannelLayout::CellCentered);
        if (density_channel.dimensions != volume.dimensions) throw std::runtime_error(std::format("Rasterizer volume \"{}\" density channel dimensions must match the volume dimensions", volume.name));
        if (temperature_channel.dimensions != volume.dimensions) throw std::runtime_error(std::format("Rasterizer volume \"{}\" temperature channel dimensions must match the volume dimensions", volume.name));

        const vk::Extent3D image_extent{volume.dimensions[0], volume.dimensions[1], volume.dimensions[2]};
        if (!*frame_volume.densityImage.image || frame_volume.densityImage.extent != image_extent) {
            this->create_volume_image(frame_volume.densityImage, image_extent);
            this->create_volume_image(frame_volume.temperatureImage, image_extent);
            frame_volume.descriptorValid = false;
        }
        const vk::DeviceSize channel_bytes = static_cast<vk::DeviceSize>(density_channel.values.size() * sizeof(float));
        this->ensure_host_buffer(frame_volume.densityStagingBuffer, channel_bytes, vk::BufferUsageFlagBits::eTransferSrc);
        this->ensure_host_buffer(frame_volume.temperatureStagingBuffer, channel_bytes, vk::BufferUsageFlagBits::eTransferSrc);
        std::memcpy(frame_volume.densityStagingBuffer.mapped, density_channel.values.data(), static_cast<std::size_t>(channel_bytes));
        std::memcpy(frame_volume.temperatureStagingBuffer.mapped, temperature_channel.values.data(), static_cast<std::size_t>(channel_bytes));

        if (!frame_volume.descriptorValid) {
            const vk::DescriptorBufferInfo camera_buffer_info{*this->mesh_pass.uniform_buffers.at(frame_index).buffer, 0, sizeof(CameraUniformData)};
            const vk::DescriptorImageInfo density_image_info{{}, *frame_volume.densityImage.view, vk::ImageLayout::eShaderReadOnlyOptimal};
            const vk::DescriptorImageInfo temperature_image_info{{}, *frame_volume.temperatureImage.view, vk::ImageLayout::eShaderReadOnlyOptimal};
            const vk::DescriptorImageInfo sampler_info{*this->volume_pass.sampler, {}, vk::ImageLayout::eUndefined};
            const std::array descriptor_writes{
                vk::WriteDescriptorSet{*this->volume_pass.descriptor_sets.at(frame_index), 0u, 0u, 1u, vk::DescriptorType::eUniformBuffer, nullptr, &camera_buffer_info, nullptr},
                vk::WriteDescriptorSet{*this->volume_pass.descriptor_sets.at(frame_index), 1u, 0u, 1u, vk::DescriptorType::eSampledImage, &density_image_info, nullptr, nullptr},
                vk::WriteDescriptorSet{*this->volume_pass.descriptor_sets.at(frame_index), 2u, 0u, 1u, vk::DescriptorType::eSampledImage, &temperature_image_info, nullptr, nullptr},
                vk::WriteDescriptorSet{*this->volume_pass.descriptor_sets.at(frame_index), 3u, 0u, 1u, vk::DescriptorType::eSampler, &sampler_info, nullptr, nullptr},
            };
            this->host.device->updateDescriptorSets(descriptor_writes, {});
            frame_volume.descriptorValid = true;
        }
        frame_volume.drawCommand = VolumeDrawCommand{
            .volume   = volume,
            .material = this->resolve_material(volume.materialName),
        };
        frame_volume.uploadPending    = true;
        frame_volume.uploadedRevision = scene_revision;
    }

    void Renderer::Impl::record_pending_volume_upload(const vk::raii::CommandBuffer& command_buffer, FrameVolumeResources& frame_volume) {
        if (!frame_volume.uploadPending) return;
        if (!*frame_volume.densityImage.image || !*frame_volume.temperatureImage.image) throw std::runtime_error("Spectra rasterizer volume upload is missing GPU images");
        if (!*frame_volume.densityStagingBuffer.buffer || !*frame_volume.temperatureStagingBuffer.buffer) throw std::runtime_error("Spectra rasterizer volume upload is missing staging buffers");
        transition_image_layout(command_buffer, *frame_volume.densityImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.densityImage.layout, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        frame_volume.densityImage.layout = vk::ImageLayout::eTransferDstOptimal;
        transition_image_layout(command_buffer, *frame_volume.temperatureImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.temperatureImage.layout, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        frame_volume.temperatureImage.layout = vk::ImageLayout::eTransferDstOptimal;

        const vk::BufferImageCopy region{
            0,
            0,
            0,
            {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            {0, 0, 0},
            frame_volume.densityImage.extent,
        };
        const std::array regions{region};
        command_buffer.copyBufferToImage(*frame_volume.densityStagingBuffer.buffer, *frame_volume.densityImage.image, vk::ImageLayout::eTransferDstOptimal, regions);
        command_buffer.copyBufferToImage(*frame_volume.temperatureStagingBuffer.buffer, *frame_volume.temperatureImage.image, vk::ImageLayout::eTransferDstOptimal, regions);

        transition_image_layout(command_buffer, *frame_volume.densityImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.densityImage.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        frame_volume.densityImage.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        transition_image_layout(command_buffer, *frame_volume.temperatureImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.temperatureImage.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        frame_volume.temperatureImage.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        frame_volume.uploadPending = false;
    }

    void Renderer::Impl::update_camera_uniform(const std::uint32_t frame_index) {
        if (frame_index >= this->mesh_pass.uniform_buffers.size()) throw std::runtime_error("Spectra rasterizer uniform frame index is out of range");
        const std::shared_ptr<const SceneDocument> scene = this->scene.workspace->document();
        const CameraUniformData camera_uniform = make_camera_uniform(*scene, this->viewport.extent);
        std::memcpy(this->mesh_pass.uniform_buffers.at(frame_index).mapped, &camera_uniform, sizeof(camera_uniform));
    }

    FrameResult Renderer::Impl::begin_frame(HostView host, const FrameContext& frame) {
        this->update_host(host.physical_device(), host.device(), host.frame_count(), host.swapchain_extent());
        if (frame.frame_index >= this->host.frame_count) throw std::runtime_error("Spectra rasterizer frame index is out of range");
        this->lifecycle.active_frame_index = frame.frame_index;
        FrameResult result{};
        this->ensure_viewport_resources();
        this->ensure_mesh_resources();
        this->ensure_particle_resources();
        this->ensure_volume_resources();
        this->upload_scene_resources(frame.frame_index);
        this->upload_particle_resources(frame.frame_index);
        this->upload_volume_resources(frame.frame_index);
        this->update_camera_uniform(frame.frame_index);
        result.window_detail = this->window_detail();
        return result;
    }

    void Renderer::Impl::record_mesh_pass(const vk::raii::CommandBuffer& command_buffer) {
        FrameSceneResources& frame_scene = this->mesh_pass.frame_scenes.at(this->lifecycle.active_frame_index);
        if (frame_scene.drawCommands.empty()) return;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->mesh_pass.pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->mesh_pass.pipeline_layout, 0u, *this->mesh_pass.descriptor_sets.at(this->lifecycle.active_frame_index), {});
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

    void Renderer::Impl::record_volume_pass(const vk::raii::CommandBuffer& command_buffer) {
        if (this->lifecycle.active_frame_index >= this->volume_pass.frame_volumes.size()) throw std::runtime_error("Spectra rasterizer active volume frame index is out of range");
        FrameVolumeResources& frame_volume = this->volume_pass.frame_volumes.at(this->lifecycle.active_frame_index);
        if (!frame_volume.descriptorValid || frame_volume.drawCommand.volume.name.empty()) return;
        const SceneVolumeGrid& volume = frame_volume.drawCommand.volume;
        const SceneMaterial& material = frame_volume.drawCommand.material;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->volume_pass.pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->volume_pass.pipeline_layout, 0u, *this->volume_pass.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const VolumePushConstantsData push_constants{
            .originDensityScale = {volume.origin.x, volume.origin.y, volume.origin.z, 1.0f},
            .extentStepScale    = {volume.voxelSize.x * static_cast<float>(volume.dimensions[0]), volume.voxelSize.y * static_cast<float>(volume.dimensions[1]), volume.voxelSize.z * static_cast<float>(volume.dimensions[2]), 1.0f},
            .baseColor          = {material.baseColor.x, material.baseColor.y, material.baseColor.z, material.baseColor.w},
            .emission           = {material.emissionColor.x * material.emissionStrength, material.emissionColor.y * material.emissionStrength, material.emissionColor.z * material.emissionStrength, 0.0f},
        };
        command_buffer.pushConstants(*this->volume_pass.pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
        command_buffer.draw(36u, 1u, 0u, 0u);
    }

    void Renderer::Impl::record_particle_pass(const vk::raii::CommandBuffer& command_buffer) {
        if (this->lifecycle.active_frame_index >= this->particle_pass.frame_particles.size()) throw std::runtime_error("Spectra rasterizer active particle frame index is out of range");
        FrameParticleResources& frame_particles = this->particle_pass.frame_particles.at(this->lifecycle.active_frame_index);
        if (frame_particles.drawCommands.empty()) return;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->particle_pass.pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->particle_pass.pipeline_layout, 0u, *this->mesh_pass.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const std::array<vk::Buffer, 1> vertex_buffers{*frame_particles.instanceBuffer.buffer};
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
        for (const ParticleDrawCommand& draw_command : frame_particles.drawCommands) command_buffer.draw(6u, draw_command.instanceCount, 0u, draw_command.firstInstance);
    }

    void Renderer::Impl::record_frame(const vk::raii::CommandBuffer& command_buffer) {
        if (!*this->viewport.image) return;
        if (this->lifecycle.active_frame_index >= this->mesh_pass.frame_scenes.size()) throw std::runtime_error("Spectra rasterizer active frame index is out of range");
        if (this->lifecycle.active_frame_index >= this->volume_pass.frame_volumes.size()) throw std::runtime_error("Spectra rasterizer active frame volume index is out of range");
        this->record_pending_volume_upload(command_buffer, this->volume_pass.frame_volumes.at(this->lifecycle.active_frame_index));

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
        this->record_mesh_pass(command_buffer);
        this->record_volume_pass(command_buffer);
        this->record_particle_pass(command_buffer);
        command_buffer.endRendering();

        transition_image_layout(command_buffer, *this->viewport.image, vk::ImageAspectFlagBits::eColor, this->viewport.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        this->viewport.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    void Renderer::Impl::draw_viewport_window() {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x > 1.0f && available.y > 1.0f) this->ui.requested_extent = vk::Extent2D{static_cast<std::uint32_t>(available.x), static_cast<std::uint32_t>(available.y)};
        if (this->viewport.imgui_descriptor == VK_NULL_HANDLE) return;
        ImGui::Image(reinterpret_cast<ImTextureID>(this->viewport.imgui_descriptor), available, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f});

        const SimulationTimeline timeline = this->scene.workspace->timeline();
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

    void Renderer::Impl::commit_timeline_from_ui(SimulationTimeline timeline) {
        SceneEditBuilder edit{};
        edit.replaceTimeline(std::move(timeline));
        const SceneEditBatch batch = this->scene.workspace->commit(std::move(edit));
        if (!HasSceneDirtyFlag(batch.dirty, SceneDirtyFlags::Timeline)) throw std::runtime_error("Rasterizer timeline UI edit did not mark the timeline dirty");
    }

    bool Renderer::Impl::timeline_playing() const {
        return this->scene.workspace->timeline().playing;
    }

    void Renderer::Impl::toggle_timeline_playback() {
        SimulationTimeline timeline = this->scene.workspace->timeline();
        timeline.playing = !timeline.playing;
        this->commit_timeline_from_ui(std::move(timeline));
    }

    void Renderer::Impl::request_timeline_reset() {
        SimulationTimeline timeline = this->scene.workspace->timeline();
        ++timeline.resetRequestSerial;
        this->commit_timeline_from_ui(std::move(timeline));
    }

    void Renderer::Impl::draw_rasterizer_window() {
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_NoBordersInBodyUntilResize;
        if (this->ui.control_panel_extension) {
            this->ui.control_panel_extension();
            ImGui::Separator();
        }
        const std::shared_ptr<const SceneDocument> scene = this->scene.workspace->document();
        SimulationTimeline timeline = this->scene.workspace->timeline();
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

        const SimulationTimeline current_timeline = this->scene.workspace->timeline();
        ImGui::SeparatorText("Status");
        if (ImGui::BeginTable("SpectraRasterizerStatus", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_status_row("Renderer", "Mesh / Volume / Particle");
            draw_status_row("Scene", scene->title.empty() ? scene->name : scene->title);
            draw_status_row("Timeline", timeline_mode_text(current_timeline.mode));
            draw_status_row("Frame", std::format("{} / {:.3f}s", current_timeline.cursor.frameIndex, current_timeline.cursor.timeSeconds));
            draw_status_row("Recorded", std::format("{}", current_timeline.recordedFrames.size()));
            draw_status_row("Viewport", std::format("{} x {}", this->viewport.extent.width, this->viewport.extent.height));
            draw_status_row("Swapchain", std::format("{} x {}", this->host.swapchain_extent.width, this->host.swapchain_extent.height));
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
