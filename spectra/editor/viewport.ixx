export module spectra.editor:viewport;

import :interaction;
import spectra.runtime;
import spectra.scene;
import spectra.scene.dynamics;
import spectra.render;
import std;
import vulkan;

namespace spectra {
    export struct EditorViewport {
        struct PickRequest {
            float normalized_x{};
            float normalized_y{};
            bool select{};
            bool additive{};
            std::optional<std::uint64_t> debug_object_id{};
            bool debug_xray{};
        };

        struct PickResult {
            bool ready{};
            std::optional<std::uint32_t> acceleration_instance_index{};
            std::optional<std::uint64_t> debug_object_id{};
            bool debug_xray{};
            bool select{};
            bool additive{};
        };

        struct PickFrameSlot {
            GpuBuffer result_buffer{};
            DescriptorHandle result_descriptor{};
            std::optional<PickRequest> submitted_request{};
        };

        EditorViewport(VulkanRuntime& runtime, GpuScene& gpu_scene, DynamicWorld& dynamics, Renderers& renderers, EditorInteraction& interaction, std::filesystem::path shader_directory);
        ~EditorViewport();

        EditorViewport(const EditorViewport&)            = delete;
        EditorViewport(EditorViewport&&)                 = delete;
        EditorViewport& operator=(const EditorViewport&) = delete;
        EditorViewport& operator=(EditorViewport&&)      = delete;

        void initialize(scene::SceneView scene);
        void destroy_scene() noexcept;
        void synchronize(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void set_camera(const scene::Camera& camera) noexcept;
        void resize(vk::Extent2D extent);
        void submit_pick(float normalized_x, float normalized_y, bool select, bool additive) noexcept;
        void consume_pick(std::uint32_t frame_slot_index, EditorInteraction& interaction) noexcept;
        void record_picker(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        void record_overlay(const vk::raii::CommandBuffer& command_buffer, bool show_axes, const EditorInteraction& interaction);

        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            DynamicWorld& dynamics;
            Renderers& renderers;
            EditorInteraction& interaction;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            GpuBuffer primitives{};
            GpuBuffer transforms{};
            GpuBuffer pick_primitives{};
            DescriptorHandle primitives_descriptor{};
            DescriptorHandle transforms_descriptor{};
            DescriptorHandle pick_primitives_descriptor{};
            scene::SceneRevision synchronized_revision{};
            scene::Camera camera{};
            bool initialized{};
        } scene;

        struct {
            vk::raii::ShaderEXT shader{nullptr};
            std::vector<PickFrameSlot> frame_slots{};
            std::optional<PickRequest> pending_request{};
        } picking;

        struct {
            vk::raii::ShaderEXTs mask_shaders{nullptr};
            vk::raii::ShaderEXTs axes_shaders{nullptr};
            vk::raii::ShaderEXTs outline_shaders{nullptr};
            vk::raii::ShaderEXTs debug_shaders{nullptr};
            vk::raii::ShaderEXTs volume_velocity_shaders{nullptr};
            GpuImage mask{};
            GpuImage depth{};
            GpuBuffer debug_buffer{};
            DescriptorHandle mask_descriptor{};
            DescriptorHandle sampler_descriptor{};
            DescriptorHandle debug_descriptor{};
            std::uint64_t debug_capacity{};
            vk::ImageLayout mask_layout{vk::ImageLayout::eUndefined};
            vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
            bool initialized{};
        } overlay;

        struct {
            GpuImage image{};
            DescriptorHandle descriptor{};
            vk::ImageLayout layout{vk::ImageLayout::eUndefined};
            std::uint64_t texture_id{};
        } target;

    private:
        void submit(PickRequest request) noexcept;
        void upload(scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer = nullptr);
        void initialize_picker();
        void destroy_picker() noexcept;
        [[nodiscard]] PickResult take_pick_result(std::uint32_t frame_slot_index) noexcept;
        void initialize_overlay();
        void destroy_overlay() noexcept;
        void create_overlay_images(vk::Extent2D extent);
        void configure_mask_render_state(const vk::raii::CommandBuffer& command_buffer, vk::Rect2D render_region, vk::CompareOp depth_compare, bool depth_write);
        void record_overlay_impl(const vk::raii::CommandBuffer& command_buffer, vk::Image target_image, vk::ImageView target_view, vk::Extent2D extent, vk::Rect2D render_region, std::span<const std::uint32_t> selected_instances, std::span<const std::uint32_t> active_instances, std::span<const std::uint32_t> hovered_instances, std::uint32_t axes_plane, bool axes_visible, bool outline_visible, bool raster_visualizations, std::span<const dynamics::DebugPrimitive> debug_primitives, std::span<const GpuVolumeVelocityField> volume_velocity_fields);
    };
} // namespace spectra
