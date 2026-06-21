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
export import spectra.dynamic_scene.host;
export import spectra.scene;

import std;

namespace spectra::rasterizer {
    export class Renderer final {
    public:
        static constexpr std::uint32_t MaxViewportDirectLights = 8u;

        Renderer(std::shared_ptr<scene::Scene> scene_instance, std::shared_ptr<scene::CameraWorkspace> camera_workspace, std::shared_ptr<dynamic_scene::HostServiceRouter> dynamic_host);
        Renderer(scene::SceneSource scene_source, std::shared_ptr<scene::CameraWorkspace> camera_workspace, std::shared_ptr<dynamic_scene::HostServiceRouter> dynamic_host);
        ~Renderer() noexcept;

        Renderer(const Renderer& other) = delete;
        Renderer(Renderer&& other) = delete;
        Renderer& operator=(const Renderer& other) = delete;
        Renderer& operator=(Renderer&& other) = delete;

        [[nodiscard]] static std::string_view name();
        void set_scene(std::shared_ptr<scene::Scene> scene_instance, std::shared_ptr<scene::CameraWorkspace> camera_workspace);

        void attach(HostView host);
        template <Host HostType>
        void attach(HostType& host) {
            this->attach(HostView{host});
        }
        void detach() noexcept;
        void before_imgui_shutdown() noexcept;
        void after_imgui_created();
        [[nodiscard]] FrameResult begin_frame(HostView host, const FrameContext& frame);
        template <Host HostType, typename HostFrameContext>
            requires requires(const HostFrameContext& frame) {
                { frame.frame_slot_index } -> std::convertible_to<std::uint32_t>;
                { frame.image_index } -> std::convertible_to<std::uint32_t>;
                { frame.frame_number } -> std::convertible_to<std::uint64_t>;
                { frame.delta_seconds } -> std::convertible_to<double>;
            }
        [[nodiscard]] FrameResult begin_frame(HostType& host, const HostFrameContext& frame) {
            return this->begin_frame(HostView{host}, FrameContext{
                .frame_index   = static_cast<std::uint32_t>(frame.frame_slot_index),
                .image_index   = static_cast<std::uint32_t>(frame.image_index),
                .frame_number  = static_cast<std::uint64_t>(frame.frame_number),
                .delta_seconds = static_cast<double>(frame.delta_seconds),
            });
        }
        void record_frame(const vk::raii::CommandBuffer& command_buffer);

    private:
        struct GpuBuffer {
            vk::raii::Buffer buffer{nullptr};
            vk::raii::DeviceMemory memory{nullptr};
            void* mapped{};
            vk::DeviceSize capacity{};
        };

        struct ExternalGpuBuffer {
            vk::raii::Buffer buffer{nullptr};
            vk::raii::DeviceMemory memory{nullptr};
            vk::DeviceSize capacity{};
        };

        struct ExternalStorageBuffer {
            ExternalGpuBuffer buffer{};
            std::uint64_t resource_id{};
            std::uint64_t byte_size{};
        };

        struct DeviceGpuBuffer {
            vk::raii::Buffer buffer{nullptr};
            vk::raii::DeviceMemory memory{nullptr};
            vk::DeviceSize capacity{};
        };

        struct GpuImage3D {
            vk::raii::Image image{nullptr};
            vk::raii::DeviceMemory memory{nullptr};
            vk::raii::ImageView view{nullptr};
            vk::Extent3D extent{};
            vk::Format format{};
            vk::ImageLayout layout{vk::ImageLayout::eUndefined};
        };

        struct GpuImage2D {
            vk::raii::Image image{nullptr};
            vk::raii::DeviceMemory memory{nullptr};
            vk::raii::ImageView view{nullptr};
            vk::Extent2D extent{};
            vk::Format format{};
            vk::ImageLayout layout{vk::ImageLayout::eUndefined};
        };

        enum class SelectableObjectKind {
            Mesh,
            PointCloud,
            VolumeGrid,
        };

        struct ObjectKey {
            SelectableObjectKind kind{SelectableObjectKind::Mesh};
            std::string name{};

            friend auto operator<=>(const ObjectKey&, const ObjectKey&) = default;
        };

        enum class SceneObjectKind {
            Camera,
            Light,
            Mesh,
            Sphere,
            PointCloud,
            VolumeGrid,
        };

        struct SceneObjectKey {
            SceneObjectKind kind{SceneObjectKind::Camera};
            std::string name{};

            friend auto operator<=>(const SceneObjectKey&, const SceneObjectKey&) = default;
        };

        struct SceneVolumeChannelSummary {
            std::string name{};
            std::array<std::uint32_t, 3> dimensions{};
            std::size_t value_count{};
        };

        struct SceneObjectRecord {
            SceneObjectKey key{};
            std::string material_name{};
            scene::Transform transform{};
            scene::Scene::SourceLocation source{};
            bool dynamic{};
            std::size_t vertex_count{};
            std::size_t index_count{};
            std::size_t point_count{};
            float minimum_radius{};
            float maximum_radius{};
            float sphere_radius{};
            std::array<std::uint32_t, 3> dimensions{};
            scene::Vector3 origin{};
            scene::Vector3 voxel_size{};
            std::vector<SceneVolumeChannelSummary> volume_channels{};
            scene::Vector3 camera_focus{};
            scene::Vector3 camera_forward{};
            scene::Vector3 camera_navigation_up{};
            bool camera_active{};
            scene::CameraProjectionKind camera_projection_kind{scene::CameraProjectionKind::Perspective};
            float camera_vertical_fov_degrees{};
            std::uint32_t camera_image_width{};
            std::uint32_t camera_image_height{};
            float camera_fx{};
            float camera_fy{};
            float camera_cx{};
            float camera_cy{};
            float camera_near_plane{};
            float camera_far_plane{};
            bool camera_visual_enabled{};
            float camera_visual_near{};
            float camera_visual_far{};
            std::string camera_image_source{};
            scene::Scene::PreviewLightKind light_kind{scene::Scene::PreviewLightKind::Directional};
            scene::Vector3 light_color{};
            float light_intensity{};
            float light_cone_angle_degrees{};
        };

        struct SceneUiCache {
            scene::Scene::Revision revision{};
            bool valid{false};
            std::vector<SceneObjectRecord> objects{};
        };

        struct CameraUniformData {
            std::array<float, 16> worldToClip{};
            std::array<float, 16> clipToWorld{};
            std::array<float, 4> cameraPosition{};
            std::array<float, 4> environmentColorIntensity{};
            std::array<std::array<float, 4>, MaxViewportDirectLights> lightDirections{};
            std::array<std::array<float, 4>, MaxViewportDirectLights> lightPositions{};
            std::array<std::array<float, 4>, MaxViewportDirectLights> lightColorIntensities{};
            std::array<std::uint32_t, 4> lightCounts{};
            std::array<float, 4> cameraForward{};
            std::array<float, 4> cameraRight{};
            std::array<float, 4> cameraUp{};
            std::array<float, 4> viewport{};
        };

        struct RenderDrawCommand {
            ObjectKey objectKey{};
            std::uint32_t objectId{};
            std::uint32_t firstIndex{};
            std::uint32_t indexCount{};
            scene::Vector3 sortPoint{};
            scene::Transform transform{};
            scene::Scene::PreviewMaterial material{};
        };

        struct PointCloudDrawCommand {
            ObjectKey objectKey{};
            std::uint32_t objectId{};
            std::uint32_t firstInstance{};
            std::uint32_t instanceCount{};
        };

        struct VolumeDrawCommand {
            ObjectKey objectKey{};
            std::uint32_t objectId{};
            scene::Scene::VolumeGrid volume{};
            scene::Scene::PreviewMaterial material{};
        };

        struct SceneBounds {
            scene::Vector3 minimum{};
            scene::Vector3 maximum{};
            bool valid{false};
        };

        struct ViewportImageRect {
            float x{};
            float y{};
            float width{};
            float height{};
        };

        struct FrameSceneResources {
            GpuBuffer vertexBuffer{};
            GpuBuffer indexBuffer{};
            scene::Scene::Revision uploadedRevision{};
            std::vector<RenderDrawCommand> drawCommands{};
        };

        struct FramePointCloudResources {
            GpuBuffer instanceBuffer{};
            scene::Scene::Revision uploadedRevision{};
            std::vector<PointCloudDrawCommand> drawCommands{};
        };

        struct ViewportSegmentDrawCommand {
            std::uint32_t firstInstance{};
            std::uint32_t instanceCount{};
            scene::Scene::ViewportSegmentDepthMode depthMode{scene::Scene::ViewportSegmentDepthMode::DepthTested};
        };

        struct FrameViewportSegmentResources {
            GpuBuffer instanceBuffer{};
            scene::Scene::Revision uploadedRevision{};
            std::vector<ViewportSegmentDrawCommand> drawCommands{};
        };

        struct ViewportImagePlaneDrawCommand {
            std::uint32_t firstInstance{};
            std::uint32_t instanceCount{};
            scene::Scene::ViewportSegmentDepthMode depthMode{scene::Scene::ViewportSegmentDepthMode::DepthTested};
            std::string imageKey{};
        };

        struct FrameViewportImagePlaneResources {
            GpuBuffer instanceBuffer{};
            scene::Scene::Revision uploadedRevision{};
            std::vector<ViewportImagePlaneDrawCommand> drawCommands{};
        };

        struct ViewportVoxelGridDrawCommand {
            std::uint64_t bufferId{};
            scene::Scene::ViewportVoxelGridSourceKind sourceKind{scene::Scene::ViewportVoxelGridSourceKind::IndexList};
            scene::Scene::ViewportVoxelGridIndexEncoding indexEncoding{scene::Scene::ViewportVoxelGridIndexEncoding::Linear};
            std::uint32_t indexCount{};
            std::uint32_t bitfieldByteCount{};
            std::uint32_t cellCount{};
            std::uint32_t frameIndex{};
            scene::Scene::ViewportSegmentDepthMode depthMode{scene::Scene::ViewportSegmentDepthMode::DepthTested};
            std::array<float, 16u> model{};
            std::array<float, 4u> originCellScale{};
            std::array<float, 4u> voxelSize{};
            std::array<float, 4u> color{};
            std::array<std::uint32_t, 4u> dimensions{};
        };

        struct FrameViewportVoxelGridResources {
            scene::Scene::Revision uploadedRevision{};
            std::vector<ViewportVoxelGridDrawCommand> drawCommands{};
        };

        struct ViewportVoxelBufferDescriptor {
            vk::raii::DescriptorSets descriptor_sets{nullptr};
            bool descriptor_valid{};
        };

        struct ViewportVoxelGridCompactionKey {
            std::uint64_t bufferId{};
            std::uint32_t frameIndex{};
            std::uint32_t dimX{};
            std::uint32_t dimY{};
            std::uint32_t dimZ{};
            scene::Scene::ViewportVoxelGridIndexEncoding indexEncoding{scene::Scene::ViewportVoxelGridIndexEncoding::Linear};

            friend auto operator<=>(const ViewportVoxelGridCompactionKey&, const ViewportVoxelGridCompactionKey&) = default;
        };

        struct ViewportVoxelGridCompactionResource {
            DeviceGpuBuffer compactedIndexBuffer{};
            DeviceGpuBuffer counterBuffer{};
            DeviceGpuBuffer indirectBuffer{};
            vk::raii::DescriptorSets compute_descriptor_sets{nullptr};
            vk::raii::DescriptorSets draw_descriptor_sets{nullptr};
            std::uint64_t source_buffer_id{};
            std::uint64_t source_byte_size{};
            std::uint32_t compacted_capacity{};
            bool compute_descriptor_valid{};
            bool draw_descriptor_valid{};
        };

        struct ViewportImagePlaneTexture {
            struct Source {
                std::uintptr_t data{};
                std::uint64_t byteSize{};
                std::uint32_t width{};
                std::uint32_t height{};
                std::uint64_t revision{};

                friend bool operator==(const Source&, const Source&) = default;
            };

            GpuBuffer stagingBuffer{};
            GpuImage2D image{};
            vk::raii::DescriptorPool descriptor_pool{nullptr};
            vk::raii::DescriptorSets descriptor_sets{nullptr};
            Source source{};
            bool uploadPending{};
        };

        struct FrameVolumeResources {
            GpuBuffer densityStagingBuffer{};
            GpuBuffer temperatureStagingBuffer{};
            GpuBuffer colorStagingBuffer{};
            GpuImage3D densityImage{};
            GpuImage3D temperatureImage{};
            GpuImage3D colorImage{};
            vk::raii::DescriptorSets externalDensityUploadDescriptorSets{nullptr};
            vk::raii::DescriptorSets externalColorUploadDescriptorSets{nullptr};
            scene::Scene::Revision uploadedRevision{};
            bool uploadPending{};
            bool externalDensityUploadPending{};
            bool externalColorUploadPending{};
            bool hasColorChannel{};
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
        void destroy_external_buffer(ExternalGpuBuffer& buffer) noexcept;
        void destroy_device_buffer(DeviceGpuBuffer& buffer) noexcept;
        void ensure_host_buffer(GpuBuffer& buffer, vk::DeviceSize required_size, vk::BufferUsageFlags usage);
        void ensure_device_buffer(DeviceGpuBuffer& buffer, vk::DeviceSize required_size, vk::BufferUsageFlags usage);
        [[nodiscard]] dynamic_scene::GpuResourceHandleKind external_storage_handle_kind() const;
        [[nodiscard]] ExternalStorageBuffer& external_storage_buffer(std::uint64_t resource_id, std::string_view context);
        [[nodiscard]] const ExternalStorageBuffer& external_storage_buffer(std::uint64_t resource_id, std::string_view context) const;
        [[nodiscard]] dynamic_scene::GpuBufferAllocation request_external_storage_buffer(std::uint32_t kind, std::uint64_t byte_size, std::string_view debug_name, std::string_view context);
        void release_external_storage_buffer(std::uint64_t resource_id, std::string_view context);
        [[nodiscard]] dynamic_scene::GpuBufferAllocation request_dynamic_gpu_buffer(const dynamic_scene::GpuBufferRequest& request);
        void release_dynamic_gpu_buffer(std::uint64_t resource_id);
        void ensure_viewport_voxel_buffer_descriptor(std::uint64_t resource_id, ViewportVoxelBufferDescriptor& descriptor);
        void ensure_viewport_voxel_grid_compaction_resource(const ViewportVoxelGridDrawCommand& draw_command);
        void connect_dynamic_scene_host();
        void disconnect_dynamic_scene_host() noexcept;
        void create_image_2d(GpuImage2D& image, vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect);
        void destroy_image_2d(GpuImage2D& image) noexcept;
        void create_volume_image(GpuImage3D& image, vk::Extent3D extent, vk::Format format);
        void destroy_volume_image(GpuImage3D& image) noexcept;
        void destroy_camera_resources() noexcept;
        void destroy_mesh_resources() noexcept;
        void destroy_viewport_grid_resources() noexcept;
        void destroy_point_cloud_resources() noexcept;
        void destroy_viewport_segment_resources() noexcept;
        void destroy_viewport_voxel_grid_resources() noexcept;
        void destroy_external_storage_buffers() noexcept;
        void destroy_viewport_voxel_grid_compaction_resource(ViewportVoxelGridCompactionResource& resource) noexcept;
        void destroy_viewport_image_plane_texture(ViewportImagePlaneTexture& texture) noexcept;
        void destroy_viewport_image_plane_resources() noexcept;
        void destroy_volume_resources() noexcept;
        void destroy_selection_resources() noexcept;
        void ensure_camera_resources();
        void ensure_mesh_resources();
        void ensure_viewport_grid_resources();
        void ensure_point_cloud_resources();
        void ensure_viewport_segment_resources();
        void ensure_viewport_voxel_grid_resources();
        void ensure_viewport_image_plane_resources();
        void ensure_volume_resources();
        void ensure_selection_resources();

        [[nodiscard]] scene::Scene::PreviewMaterial resolve_material(std::string_view material_name) const;
        [[nodiscard]] const scene::Scene::VolumeChannel* find_volume_channel(const scene::Scene::VolumeGrid& volume, std::string_view channel_name) const;
        [[nodiscard]] const scene::Scene::VolumeChannel& require_volume_channel(const scene::Scene::VolumeGrid& volume, std::string_view channel_name) const;
        [[nodiscard]] const scene::Scene::VolumeGrid* select_render_volume_grid(std::span<const scene::Scene::VolumeGrid> volumes) const;
        void sync_scene_source(double delta_seconds);
        void rebuild_scene_ui_cache_if_needed();
        void prune_scene_selection_to_cache();
        [[nodiscard]] const SceneObjectRecord* scene_object_record(const SceneObjectKey& key) const;
        [[nodiscard]] std::optional<ObjectKey> renderable_key_for_scene_object(const SceneObjectKey& key) const;
        [[nodiscard]] SceneObjectKey scene_object_key_for_renderable(const ObjectKey& key) const;
        void sync_renderable_selection_from_scene_selection();
        void select_scene_object(const SceneObjectKey& key, bool additive);
        [[nodiscard]] bool scene_object_selected(const SceneObjectKey& key) const;
        [[nodiscard]] bool scene_object_active(const SceneObjectKey& key) const;
        [[nodiscard]] std::string raw_scene_object_name(const SceneObjectRecord& object) const;
        [[nodiscard]] std::string compact_scene_object_name(const SceneObjectRecord& object) const;
        [[nodiscard]] std::string compact_scene_object_name(const SceneObjectKey& key) const;
        [[nodiscard]] std::string compact_source_location(const scene::Scene::SourceLocation& source) const;
        [[nodiscard]] std::string scene_object_label(const SceneObjectKey& key) const;
        [[nodiscard]] std::string scene_selection_summary() const;
        void rebuild_selection_registry_if_needed();
        void register_selectable_object(SelectableObjectKind kind, std::string_view name, std::set<ObjectKey>& unique_keys, std::uint32_t& next_id);
        [[nodiscard]] std::uint32_t object_id_for(const ObjectKey& key) const;
        [[nodiscard]] const ObjectKey* object_for_id(std::uint32_t object_id) const;
        void prune_selection_to_registry();
        [[nodiscard]] bool object_selected(const ObjectKey& key) const;
        [[nodiscard]] bool object_hovered(const ObjectKey& key) const;
        [[nodiscard]] bool object_active(const ObjectKey& key) const;
        [[nodiscard]] std::array<float, 4> selection_mask_color(const ObjectKey& key) const;
        [[nodiscard]] std::string object_label(const ObjectKey& key) const;
        void clear_selection();
        void apply_pick_result(std::uint32_t object_id, bool select, bool additive);

        void upload_scene_resources(std::uint32_t frame_index);
        void upload_point_cloud_resources(std::uint32_t frame_index);
        void upload_viewport_segment_resources(std::uint32_t frame_index);
        void upload_viewport_voxel_grid_resources(std::uint32_t frame_index);
        void upload_viewport_image_plane_resources(std::uint32_t frame_index);
        void upload_volume_resources(std::uint32_t frame_index);
        void record_pending_viewport_image_plane_uploads(const vk::raii::CommandBuffer& command_buffer);
        void record_pending_volume_upload(const vk::raii::CommandBuffer& command_buffer, FrameVolumeResources& frame_volume);
        void update_camera_uniform(std::uint32_t frame_index);

        void record_mesh_pass(const vk::raii::CommandBuffer& command_buffer);
        void record_transparent_mesh_pass(const vk::raii::CommandBuffer& command_buffer);
        void record_viewport_grid_pass(const vk::raii::CommandBuffer& command_buffer);
        void record_point_cloud_pass(const vk::raii::CommandBuffer& command_buffer);
        void record_volume_pass(const vk::raii::CommandBuffer& command_buffer);
        void record_viewport_voxel_grid_compactions(const vk::raii::CommandBuffer& command_buffer);
        void record_viewport_voxel_grid_pass(const vk::raii::CommandBuffer& command_buffer);
        void record_viewport_voxel_grid_compaction(const vk::raii::CommandBuffer& command_buffer, const ViewportVoxelGridDrawCommand& draw_command);
        void record_viewport_image_plane_pass(const vk::raii::CommandBuffer& command_buffer);
        void record_viewport_segment_pass(const vk::raii::CommandBuffer& command_buffer);
        void request_selection_pick(std::uint32_t x, std::uint32_t y, bool select, bool additive);
        void record_selection_pick_pass(const vk::raii::CommandBuffer& command_buffer);
        void record_selection_visuals(const vk::raii::CommandBuffer& command_buffer);
        void record_mesh_selection_pass(const vk::raii::CommandBuffer& command_buffer, bool picking);
        void record_point_cloud_selection_pass(const vk::raii::CommandBuffer& command_buffer, bool picking);
        void record_volume_selection_pass(const vk::raii::CommandBuffer& command_buffer, bool picking);
        void record_selection_outline_pass(const vk::raii::CommandBuffer& command_buffer);
        void consume_completed_selection_pick(std::uint32_t frame_index);
        void request_viewport_screenshot();
        void record_viewport_screenshot_copy(const vk::raii::CommandBuffer& command_buffer);
        void consume_completed_screenshot(std::uint32_t frame_index);

        [[nodiscard]] std::string active_scene_id() const;
        [[nodiscard]] scene::CameraState initial_camera_state_from_scene() const;
        [[nodiscard]] scene::CameraState current_viewport_camera_state() const;
        [[nodiscard]] float current_viewport_camera_distance() const;
        void ensure_viewport_camera_session();
        void reset_viewport_camera_session();
        void synchronize_viewport_camera();
        void apply_viewport_camera_state(const scene::CameraSnapshot& snapshot);
        void commit_viewport_camera_state(scene::CameraState state);
        void reset_viewport_camera_from_scene();
        void apply_viewport_coordinate_system(std::string_view coordinate_system_name);
        void frame_viewport_scene();
        void frame_selected_objects();
        void set_viewport_axis_view(scene::Vector3 direction);
        void orbit_viewport_camera(scene::ViewportCameraDelta delta);
        void pan_viewport_camera(scene::ViewportCameraDelta delta, scene::ViewportCameraSize viewport);
        void zoom_viewport_camera(float steps);
        void handle_viewport_input(ViewportImageRect image_rect);
        void draw_viewport_overlays(ViewportImageRect image_rect);
        void draw_viewport_toolbar(ViewportImageRect image_rect);
        void draw_orientation_gizmo(ViewportImageRect image_rect);
        [[nodiscard]] SceneBounds scene_bounds() const;
        [[nodiscard]] SceneBounds selected_scene_bounds() const;
        [[nodiscard]] CameraUniformData make_viewport_camera_uniform() const;

        void draw_viewport_window();
        void draw_rasterizer_window();
        void draw_scene_collection_panel();
        void draw_scene_object_group(std::string_view label, const char* icon, std::span<const SceneObjectKind> kinds);
        void draw_scene_object_row(const SceneObjectRecord& object);
        void draw_inspector_panel();
        void draw_inspector_material_block(std::string_view material_name);
        void draw_inspector_transform(const scene::Transform& transform);

        struct {
            const vk::raii::PhysicalDevice* physical_device{};
            const vk::raii::Device* device{};
            vk::Extent2D swapchain_extent{};
            std::uint32_t frame_count{};
            std::move_only_function<void(float, float, float, float)> draw_viewport_overlays{};
        } host;

        struct {
            std::uint32_t active_frame_index{};
            bool attached{false};
            bool imgui_ready{false};
        } lifecycle;

        struct {
            std::shared_ptr<scene::Scene> instance{};
            scene::SceneSource source{};
            std::shared_ptr<scene::CameraWorkspace> camera_workspace{};
            scene::CameraRevision observed_camera_revision{};
            std::shared_ptr<dynamic_scene::HostServiceRouter> dynamic_host{};
        } scene;

        struct {
            vk::Extent2D requested_extent{};
            float sidebar_split_ratio{0.55f};
            SceneUiCache scene_ui_cache{};
            std::string active_coordinate_system_name{};
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
            scene::CameraState camera_state{};
            float camera_near_plane{0.01f};
            float camera_far_plane{200.0f};
            bool camera_initialized{false};
            bool overlays_visible{true};
            bool grid_visible{false};
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
            vk::raii::Pipeline transparent_pipeline{nullptr};
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
            std::vector<FramePointCloudResources> frame_point_clouds{};
        } point_cloud_pass;

        struct {
            std::uint32_t frame_count{};
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::Pipeline depth_tested_pipeline{nullptr};
            vk::raii::Pipeline always_visible_pipeline{nullptr};
            std::vector<FrameViewportSegmentResources> frame_segments{};
        } viewport_segment_pass;

        struct {
            std::uint32_t frame_count{};
            vk::raii::DescriptorSetLayout descriptor_set_layout{nullptr};
            vk::raii::DescriptorSetLayout compaction_descriptor_set_layout{nullptr};
            vk::raii::DescriptorPool descriptor_pool{nullptr};
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::PipelineLayout compaction_pipeline_layout{nullptr};
            vk::raii::Pipeline compaction_pipeline{nullptr};
            vk::raii::Pipeline depth_tested_pipeline{nullptr};
            vk::raii::Pipeline always_visible_pipeline{nullptr};
            std::vector<FrameViewportVoxelGridResources> frame_voxel_grids{};
            std::map<std::uint64_t, ViewportVoxelBufferDescriptor> buffer_descriptors{};
            std::map<ViewportVoxelGridCompactionKey, ViewportVoxelGridCompactionResource> compactions{};
        } viewport_voxel_grid_pass;

        struct {
            std::map<std::uint64_t, ExternalStorageBuffer> buffers{};
            std::uint64_t next_resource_id{1u};
        } external_storage;

        struct {
            std::uint32_t frame_count{};
            vk::raii::DescriptorSetLayout descriptor_set_layout{nullptr};
            vk::raii::Sampler sampler{nullptr};
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::Pipeline depth_tested_pipeline{nullptr};
            vk::raii::Pipeline always_visible_pipeline{nullptr};
            std::vector<FrameViewportImagePlaneResources> frame_planes{};
            std::map<std::string, ViewportImagePlaneTexture> texture_cache{};
        } viewport_image_plane_pass;

        struct {
            std::uint32_t frame_count{};
            vk::raii::DescriptorSetLayout descriptor_set_layout{nullptr};
            vk::raii::DescriptorSetLayout upload_descriptor_set_layout{nullptr};
            vk::raii::DescriptorPool descriptor_pool{nullptr};
            vk::raii::DescriptorSets descriptor_sets{nullptr};
            vk::raii::Sampler sampler{nullptr};
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::PipelineLayout upload_pipeline_layout{nullptr};
            vk::raii::Pipeline pipeline{nullptr};
            vk::raii::Pipeline upload_pipeline{nullptr};
            vk::raii::Pipeline color_upload_pipeline{nullptr};
            std::vector<FrameVolumeResources> frame_volumes{};
        } volume_pass;

        struct {
            std::uint32_t frame_count{};
            GpuImage2D object_id_image{};
            GpuImage2D depth_image{};
            GpuImage2D mask_image{};
            vk::raii::Sampler mask_sampler{nullptr};
            vk::raii::DescriptorSetLayout outline_descriptor_set_layout{nullptr};
            vk::raii::DescriptorPool outline_descriptor_pool{nullptr};
            vk::raii::DescriptorSets outline_descriptor_sets{nullptr};
            vk::raii::PipelineLayout mesh_picking_pipeline_layout{nullptr};
            vk::raii::Pipeline mesh_picking_pipeline{nullptr};
            vk::raii::PipelineLayout point_cloud_picking_pipeline_layout{nullptr};
            vk::raii::Pipeline point_cloud_picking_pipeline{nullptr};
            vk::raii::PipelineLayout volume_picking_pipeline_layout{nullptr};
            vk::raii::Pipeline volume_picking_pipeline{nullptr};
            vk::raii::PipelineLayout mesh_mask_pipeline_layout{nullptr};
            vk::raii::Pipeline mesh_mask_pipeline{nullptr};
            vk::raii::PipelineLayout point_cloud_mask_pipeline_layout{nullptr};
            vk::raii::Pipeline point_cloud_mask_pipeline{nullptr};
            vk::raii::PipelineLayout volume_mask_pipeline_layout{nullptr};
            vk::raii::Pipeline volume_mask_pipeline{nullptr};
            vk::raii::PipelineLayout outline_pipeline_layout{nullptr};
            vk::raii::Pipeline outline_pipeline{nullptr};
            std::vector<GpuBuffer> readback_buffers{};
            scene::Scene::Revision registry_revision{};
            std::map<ObjectKey, std::uint32_t> object_ids{};
            std::map<std::uint32_t, ObjectKey> objects_by_id{};
            std::set<SceneObjectKey> selected_scene_objects{};
            std::optional<SceneObjectKey> active_scene_object{};
            std::set<ObjectKey> selected_objects{};
            std::optional<ObjectKey> active_object{};
            std::optional<ObjectKey> hovered_object{};
            bool registry_valid{false};
            bool visuals_visible{true};
            bool pick_requested{false};
            bool pick_pending{false};
            bool requested_select{false};
            bool requested_additive{false};
            bool pending_select{false};
            bool pending_additive{false};
            std::uint32_t requested_x{};
            std::uint32_t requested_y{};
            std::uint32_t pending_frame_index{};
        } selection;
    };
} // namespace spectra::rasterizer
