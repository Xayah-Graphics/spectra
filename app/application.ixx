export module spectra.application;

import spectra;
import spectra.render;
import spectra.scene;
import std;
import vulkan;

namespace spectra::app {
    void write_linear_exr(const std::filesystem::path& path, std::span<const float> rgba, std::uint32_t width, std::uint32_t height, scene::SpectrumColorSpace color_space);
    void write_png(const std::filesystem::path& path, std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height);
    void write_render_readback_exr(const std::filesystem::path& path, const render::RenderReadback& readback, scene::SpectrumColorSpace color_space, bool camera_space);

    struct Presenter {
        Presenter(Spectra& runtime, const std::filesystem::path& shader_directory);
        ~Presenter();

        Presenter(const Presenter&)            = delete;
        Presenter(Presenter&&)                 = delete;
        Presenter& operator=(const Presenter&) = delete;
        Presenter& operator=(Presenter&&)      = delete;

        void record(const vk::raii::CommandBuffer& command_buffer, render::RenderOutput source, vk::Image target_image, vk::ImageView target_view, vk::Extent2D extent, vk::ImageLayout target_layout, vk::ImageLayout final_layout, float exposure);

    private:
        Spectra* runtime{};
        vk::raii::ShaderEXTs shaders{nullptr};
        DescriptorHandle sampler_descriptor{};
    };
} // namespace spectra::app

namespace spectra {
    export void run_application(std::optional<std::filesystem::path> scene_path, const std::filesystem::path& scene_library, std::vector<std::filesystem::path> scene_roots, const std::filesystem::path& shader_directory, std::optional<std::uint64_t> maximum_frame_count = std::nullopt, bool start_pathtracer = false);
} // namespace spectra
