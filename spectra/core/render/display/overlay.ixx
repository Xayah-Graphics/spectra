export module spectra.render.display.overlay;

import spectra.render.display.types;
import spectra.render.types;
import spectra.render.gpu_scene;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

export namespace spectra::render {
    struct OverlayPass {
        OverlayPass(runtime::VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory);
        ~OverlayPass();

        OverlayPass(const OverlayPass&)            = delete;
        OverlayPass(OverlayPass&&)                 = delete;
        OverlayPass& operator=(const OverlayPass&) = delete;
        OverlayPass& operator=(OverlayPass&&)      = delete;

        void record(const vk::raii::CommandBuffer& command_buffer, ColorTarget target, const scene::Camera& camera, const OverlayRequest& request);

    private:
        struct {
            runtime::VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            vk::raii::ShaderEXTs mask_shaders{nullptr};
            vk::raii::ShaderEXTs axes_shaders{nullptr};
            vk::raii::ShaderEXTs outline_shaders{nullptr};
            runtime::GpuImage mask{};
            runtime::GpuImage depth{};
            runtime::DescriptorLease mask_descriptor{};
            runtime::DescriptorLease sampler_descriptor{};
            vk::ImageLayout mask_layout{vk::ImageLayout::eUndefined};
            vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
        } overlay;

        void configure_mask_render_state(const vk::raii::CommandBuffer& command_buffer, vk::Rect2D render_region, vk::CompareOp depth_compare, bool depth_write);
        void load_shaders();
        void create_overlay_images(vk::Extent2D extent);
        void record_impl(const vk::raii::CommandBuffer& command_buffer, vk::Image target_image, vk::ImageView target_view, vk::ImageLayout target_layout, vk::Extent2D extent, vk::Rect2D render_region, const scene::Camera& camera, std::span<const std::uint32_t> selected_instances, std::span<const std::uint32_t> active_instances, std::span<const std::uint32_t> hovered_instances, AxesPlane axes_plane, bool axes_visible, bool outline_visible);
    };
} // namespace spectra::render
