module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vulkan/vulkan_raii.hpp>

export module spectra.rasterizer.renderer;

export import spectra.rasterizer.host;
export import spectra.rasterizer.scene;

import std;

namespace spectra::rasterizer {
    export class Renderer final {
    public:
        explicit Renderer(std::shared_ptr<SceneWorkspace> scene_workspace);
        ~Renderer() noexcept;

        Renderer(const Renderer& other) = delete;
        Renderer(Renderer&& other) = delete;
        Renderer& operator=(const Renderer& other) = delete;
        Renderer& operator=(Renderer&& other) = delete;

        [[nodiscard]] static std::string_view name();
        void set_scene_workspace(std::shared_ptr<SceneWorkspace> scene_workspace);
        void set_control_panel_extension(std::move_only_function<void()> draw);

        void attach(HostView host);
        void detach() noexcept;
        void before_imgui_shutdown() noexcept;
        void after_imgui_created();
        [[nodiscard]] FrameResult begin_frame(HostView host, const FrameContext& frame);
        void record_frame(const vk::raii::CommandBuffer& command_buffer);

    private:
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

        struct CameraUniformData {
            std::array<float, 16> viewProjection{};
            std::array<float, 16> inverseViewProjection{};
            std::array<float, 4> cameraPosition{};
            std::array<float, 4> lightDirection{};
            std::array<float, 4> lightColorIntensity{};
            std::array<float, 4> cameraRight{};
            std::array<float, 4> cameraUp{};
            std::array<float, 4> viewport{};
        };

        struct RenderDrawCommand {
            std::string name{};
            std::uint32_t firstIndex{};
            std::uint32_t indexCount{};
            SceneTransform transform{};
            SceneMaterial material{};
        };

        struct ParticleDrawCommand {
            std::uint32_t firstInstance{};
            std::uint32_t instanceCount{};
        };

        struct VolumeDrawCommand {
            SceneVolumeGrid volume{};
            SceneMaterial material{};
        };

        enum class ViewProjection {
            Perspective,
            Orthographic,
        };

        struct SceneBounds {
            SceneVector3 minimum{};
            SceneVector3 maximum{};
            bool valid{false};
        };

        struct ViewportImageRect {
            float x{};
            float y{};
            float width{};
            float height{};
        };

        struct ViewportDragDelta {
            float x{};
            float y{};
        };

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
        void destroy_imgui_descriptor() noexcept;
        void destroy_viewport_resources() noexcept;
        void destroy_screenshot_resources() noexcept;
        void ensure_viewport_resources();
        void create_imgui_descriptor();

        void destroy_host_buffer(GpuBuffer& buffer) noexcept;
        void ensure_host_buffer(GpuBuffer& buffer, vk::DeviceSize required_size, vk::BufferUsageFlags usage);
        void create_volume_image(GpuImage3D& image, vk::Extent3D extent);
        void destroy_volume_image(GpuImage3D& image) noexcept;
        void destroy_camera_resources() noexcept;
        void destroy_mesh_resources() noexcept;
        void destroy_viewport_grid_resources() noexcept;
        void destroy_particle_resources() noexcept;
        void destroy_volume_resources() noexcept;
        void ensure_camera_resources();
        void ensure_mesh_resources();
        void ensure_viewport_grid_resources();
        void ensure_particle_resources();
        void ensure_volume_resources();

        [[nodiscard]] std::vector<SceneMesh> collect_render_meshes() const;
        [[nodiscard]] std::vector<SceneParticleSet> collect_render_particle_sets() const;
        [[nodiscard]] std::vector<SceneVolumeGrid> collect_render_volumes() const;
        [[nodiscard]] SceneMaterial resolve_material(std::string_view material_name) const;
        [[nodiscard]] const SceneVolumeChannel& require_volume_channel(const SceneVolumeGrid& volume, std::string_view channel_name, SceneVolumeChannelLayout layout) const;
        [[nodiscard]] const SceneVolumeGrid* select_render_volume_grid(const std::vector<SceneVolumeGrid>& volumes) const;

        void upload_scene_resources(std::uint32_t frame_index);
        void upload_particle_resources(std::uint32_t frame_index);
        void upload_volume_resources(std::uint32_t frame_index);
        void record_pending_volume_upload(const vk::raii::CommandBuffer& command_buffer, FrameVolumeResources& frame_volume);
        void update_camera_uniform(std::uint32_t frame_index);

        void record_mesh_pass(const vk::raii::CommandBuffer& command_buffer);
        void record_viewport_grid_pass(const vk::raii::CommandBuffer& command_buffer);
        void record_particle_pass(const vk::raii::CommandBuffer& command_buffer);
        void record_volume_pass(const vk::raii::CommandBuffer& command_buffer);
        void request_viewport_screenshot();
        void record_viewport_screenshot_copy(const vk::raii::CommandBuffer& command_buffer);
        void consume_completed_screenshot(std::uint32_t frame_index);

        void reset_viewport_camera_from_scene();
        void frame_viewport_scene();
        void set_viewport_axis_view(SceneVector3 direction);
        void toggle_viewport_projection();
        void orbit_viewport_camera(ViewportDragDelta delta);
        void pan_viewport_camera(ViewportDragDelta delta, float viewport_height);
        void zoom_viewport_camera(float steps);
        void handle_viewport_input(ViewportImageRect image_rect);
        void draw_viewport_overlays(ViewportImageRect image_rect);
        void draw_viewport_toolbar(ViewportImageRect image_rect);
        void draw_orientation_gizmo(ViewportImageRect image_rect);
        [[nodiscard]] SceneBounds scene_bounds() const;
        [[nodiscard]] CameraUniformData make_viewport_camera_uniform() const;

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
            SceneVector3 camera_target{};
            float camera_distance{1.0f};
            float camera_yaw{};
            float camera_pitch{};
            float camera_vertical_fov_degrees{45.0f};
            float camera_near_plane{0.01f};
            float camera_far_plane{200.0f};
            ViewProjection projection{ViewProjection::Perspective};
            bool camera_initialized{false};
            bool overlays_visible{true};
            bool grid_visible{true};
            bool hovered{false};
            bool screenshot_requested{false};
            bool screenshot_pending{false};
            std::uint32_t screenshot_frame_index{};
            vk::Extent2D screenshot_extent{};
            std::filesystem::path screenshot_path{};
            GpuBuffer screenshot_buffer{};
        } viewport;

        struct {
            std::uint32_t frame_count{};
            vk::raii::DescriptorSetLayout descriptor_set_layout{nullptr};
            vk::raii::DescriptorPool descriptor_pool{nullptr};
            vk::raii::DescriptorSets descriptor_sets{nullptr};
            std::vector<GpuBuffer> uniform_buffers{};
        } camera;

        struct {
            std::uint32_t frame_count{};
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::Pipeline pipeline{nullptr};
            std::vector<FrameSceneResources> frame_scenes{};
        } mesh_pass;

        struct {
            std::uint32_t frame_count{};
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::Pipeline pipeline{nullptr};
        } viewport_grid_pass;

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
} // namespace spectra::rasterizer
