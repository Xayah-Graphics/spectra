export module spectra.editor:viewport.overlay;

import spectra.render.display;
import spectra.runtime;
import spectra.scene;
import spectra.render.scene;
import std;
import vulkan;

namespace spectra {
    export struct ViewportOverlayState {
        std::span<const scene::InstanceId> selected_instances{};
        std::optional<scene::InstanceId> active_instance{};
        std::optional<scene::InstanceId> hovered_instance{};
        std::uint32_t axes_plane{};
        bool axes_visible{};
        bool outline_visible{true};
    };

    export struct ViewportOverlay {
        ViewportOverlay(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory);
        ~ViewportOverlay();

        ViewportOverlay(const ViewportOverlay&)            = delete;
        ViewportOverlay(ViewportOverlay&&)                 = delete;
        ViewportOverlay& operator=(const ViewportOverlay&) = delete;
        ViewportOverlay& operator=(ViewportOverlay&&)      = delete;

        void initialize(scene::SceneView scene);
        void destroy_scene() noexcept;
        void synchronize(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void record(const vk::raii::CommandBuffer& command_buffer, DisplayPass& display, const scene::Camera& camera, const ViewportOverlayState& state);

        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            GpuBuffer primitives{};
            GpuBuffer transforms{};
            DescriptorHandle primitives_descriptor{};
            DescriptorHandle transforms_descriptor{};
            bool initialized{};
        } scene;

        struct {
            vk::raii::ShaderEXTs mask_shaders{nullptr};
            vk::raii::ShaderEXTs axes_shaders{nullptr};
            vk::raii::ShaderEXTs outline_shaders{nullptr};
            GpuImage mask{};
            GpuImage depth{};
            DescriptorHandle mask_descriptor{};
            DescriptorHandle sampler_descriptor{};
            vk::ImageLayout mask_layout{vk::ImageLayout::eUndefined};
            vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
            bool initialized{};
        } overlay;

    private:
        void upload(scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer = nullptr);
        void initialize_overlay();
        void destroy_overlay() noexcept;
        void create_overlay_images(vk::Extent2D extent);
        void configure_mask_render_state(const vk::raii::CommandBuffer& command_buffer, vk::Rect2D render_region, vk::CompareOp depth_compare, bool depth_write);
        void record_impl(const vk::raii::CommandBuffer& command_buffer, vk::Image target_image, vk::ImageView target_view, vk::ImageLayout target_layout, vk::Extent2D extent, vk::Rect2D render_region, const scene::Camera& camera, std::span<const std::uint32_t> selected_instances, std::span<const std::uint32_t> active_instances, std::span<const std::uint32_t> hovered_instances, std::uint32_t axes_plane, bool axes_visible, bool outline_visible);
    };
} // namespace spectra
