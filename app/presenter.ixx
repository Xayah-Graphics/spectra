export module spectra.app.presenter;

import spectra;
import spectra.render.output;
import std;
import vulkan;

namespace spectra::app {
    export struct Presenter {
        Presenter(GpuDevice& gpu, const std::filesystem::path& shader_directory);
        ~Presenter();

        Presenter(const Presenter&) = delete;
        Presenter(Presenter&&) = delete;
        Presenter& operator=(const Presenter&) = delete;
        Presenter& operator=(Presenter&&) = delete;

        void record(
            const vk::raii::CommandBuffer& command_buffer,
            render::RenderOutput source,
            vk::Image target_image,
            vk::ImageView target_view,
            vk::Extent2D extent,
            vk::ImageLayout target_layout,
            vk::ImageLayout final_layout,
            float exposure);

    private:
        GpuDevice* gpu{};
        vk::raii::ShaderEXTs shaders{nullptr};
        DescriptorHandle sampler_descriptor{};
    };
} // namespace spectra::app
