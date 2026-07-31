module;

#include <imgui.h>

export module spectra.ui;

import spectra;
import std;
import vulkan;

namespace spectra::ui {
    export struct ImGuiRenderer {
        ImGuiRenderer(GpuDevice& gpu, const std::filesystem::path& shader_directory, std::uint32_t frames_in_flight);
        ~ImGuiRenderer();

        ImGuiRenderer(const ImGuiRenderer&) = delete;
        ImGuiRenderer(ImGuiRenderer&&) = delete;
        ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;
        ImGuiRenderer& operator=(ImGuiRenderer&&) = delete;

        void record(
            ImDrawData& draw_data,
            const vk::raii::CommandBuffer& command_buffer,
            std::uint32_t frame_index,
            vk::Image target_image,
            vk::ImageView target_view,
            vk::Extent2D extent,
            vk::ImageLayout target_layout,
            vk::ImageLayout final_layout);

    private:
        struct FrameResources {
            GpuBuffer vertex_buffer{};
            GpuBuffer index_buffer{};
            DescriptorHandle vertex_descriptor{};
            DescriptorHandle index_descriptor{};
            std::size_t vertex_capacity{};
            std::size_t index_capacity{};
        };

        void update_texture(ImTextureData& texture);
        void destroy_texture(ImTextureData& texture);
        void setup_render_state(
            const vk::raii::CommandBuffer& command_buffer,
            const ImDrawData& draw_data,
            const FrameResources& frame,
            vk::Extent2D extent);

        GpuDevice* gpu{};
        vk::raii::ShaderEXTs shaders{nullptr};
        std::vector<FrameResources> frames{};
        DescriptorHandle sampler_descriptor{};
    };
} // namespace spectra::ui
