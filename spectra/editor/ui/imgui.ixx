module;

#include <imgui.h>

export module spectra.editor.ui.imgui;

import spectra.editor.platform.window;
import spectra.render.display;
import spectra.runtime;
import std;
import vulkan;

namespace spectra {
    export struct ImGuiBackend {
        struct FrameResources {
            GpuBuffer vertex_buffer{};
            GpuBuffer index_buffer{};
            DescriptorLease vertex_descriptor{};
            DescriptorLease index_descriptor{};
            std::size_t vertex_capacity{};
            std::size_t index_capacity{};
        };

        ImGuiBackend(WindowPlatform& platform, VulkanRuntime& runtime, DisplayPass& display, std::filesystem::path shader_directory) noexcept;
        ~ImGuiBackend();

        ImGuiBackend(const ImGuiBackend&)            = delete;
        ImGuiBackend(ImGuiBackend&&)                 = delete;
        ImGuiBackend& operator=(const ImGuiBackend&) = delete;
        ImGuiBackend& operator=(ImGuiBackend&&)      = delete;

        void initialize();
        void begin_frame();
        void end_frame();
        void resize_viewport(vk::Extent2D extent);
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, vk::Image target_image, vk::ImageView target_view, vk::Extent2D extent, vk::ImageLayout target_layout, vk::ImageLayout final_layout);

        struct {
            WindowPlatform& platform;
            VulkanRuntime& runtime;
            DisplayPass& display;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            vk::raii::ShaderEXTs shaders{nullptr};
            std::vector<FrameResources> frames{};
            DescriptorLease sampler_descriptor{};
            DescriptorLease viewport_descriptor{};
        } renderer;

        std::uint64_t viewport_texture_id{};
        bool initialized{};

    private:
        void initialize_renderer();
        void update_texture(ImTextureData& texture);
        void destroy_texture(ImTextureData& texture);
        void setup_render_state(const vk::raii::CommandBuffer& command_buffer, const ImDrawData& draw_data, const FrameResources& frame, vk::Extent2D extent);
    };
} // namespace spectra
