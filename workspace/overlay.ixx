export module spectra.workspace.overlay;

import spectra;
import spectra.render.assets;
import spectra.rasterizer;
import std;
import vulkan;

namespace spectra::workspace {
    export struct OverlayRenderer {
        OverlayRenderer(GpuDevice& gpu, const rasterizer::RasterScene& scene, const std::filesystem::path& shader_directory);
        ~OverlayRenderer();

        OverlayRenderer(const OverlayRenderer&) = delete;
        OverlayRenderer(OverlayRenderer&&) = delete;
        OverlayRenderer& operator=(const OverlayRenderer&) = delete;
        OverlayRenderer& operator=(OverlayRenderer&&) = delete;

        void record(
            const vk::raii::CommandBuffer& command_buffer,
            vk::Image target_image,
            vk::ImageView target_view,
            vk::Extent2D extent,
            vk::Rect2D render_region,
            std::span<const std::uint32_t> selected_instances,
            std::span<const std::uint32_t> active_instances,
            std::span<const std::uint32_t> hovered_instances,
            std::uint32_t axes_plane,
            bool axes_visible,
            bool outline_visible);

    private:
        void create_images(vk::Extent2D extent);

        GpuDevice* gpu{};
        const rasterizer::RasterScene* scene{};
        vk::raii::ShaderEXTs mask_shaders{nullptr};
        vk::raii::ShaderEXTs axes_shaders{nullptr};
        vk::raii::ShaderEXTs outline_shaders{nullptr};
        GpuImage mask{};
        GpuImage depth{};
        DescriptorHandle mask_descriptor{};
        DescriptorHandle sampler_descriptor{};
        vk::ImageLayout mask_layout{vk::ImageLayout::eUndefined};
        vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
    };
}
